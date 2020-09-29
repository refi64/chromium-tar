#include "ui/gtk/select_file_dialog_impl_portal.h"
#include "base/bind.h"
#include "base/logging.h"
#include "base/nix/mime_util_xdg.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_piece_forward.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "components/dbus/thread_linux/dbus_thread_linux.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "dbus/property.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/x/x11.h"
#include "ui/gtk/select_file_dialog_impl.h"
#include "ui/strings/grit/ui_strings.h"
#include "url/url_util.h"

namespace gtk {

namespace {

constexpr char kDBusMethodNameHasOwner[] = "NameHasOwner";
constexpr char kDBusMethodListActivatableNames[] = "ListActivatableNames";

constexpr char kXdgPortalService[] = "org.freedesktop.portal.Desktop";
const dbus::ObjectPath kXdgPortalObject("/org/freedesktop/portal/desktop");

constexpr int kXdgPortalRequiredVersion = 3;

constexpr char kXdgPortalRequestInterfaceName[] =
    "org.freedesktop.portal.Request";
constexpr char kXdgPortalResponseSignal[] = "Response";

constexpr char kFileChooserInterfaceName[] =
    "org.freedesktop.portal.FileChooser";

constexpr char kFileChooserMethodOpenFile[] = "OpenFile";
constexpr char kFileChooserMethodSaveFile[] = "SaveFile";

constexpr char kFileChooserOptionHandleToken[] = "handle_token";
constexpr char kFileChooserOptionAcceptLabel[] = "accept_label";
constexpr char kFileChooserOptionMultiple[] = "multiple";
constexpr char kFileChooserOptionDirectory[] = "directory";
constexpr char kFileChooserOptionFilters[] = "filters";
constexpr char kFileChooserOptionCurrentFilter[] = "current_filter";
constexpr char kFileChooserOptionCurrentFolder[] = "current_folder";
constexpr char kFileChooserOptionCurrentFile[] = "current_file";

constexpr int kFileChooserFilterKindGlob = 0;

constexpr char kOpenLabel[] = "_Open";
constexpr char kSaveLabel[] = "_Save";

constexpr char kFileUriPrefix[] = "file://";

struct FileChooserProperties : dbus::PropertySet {
  dbus::Property<uint32_t> version;

  explicit FileChooserProperties(dbus::ObjectProxy* object_proxy)
      : dbus::PropertySet(object_proxy, kFileChooserInterfaceName, {}) {
    RegisterProperty("version", &version);
  }

  ~FileChooserProperties() override = default;
};

}  // namespace

SelectFileDialogImpl* SelectFileDialogImpl::NewSelectFileDialogImplPortal(
    Listener* listener,
    std::unique_ptr<ui::SelectFilePolicy> policy) {
  return new SelectFileDialogImplPortal(listener, std::move(policy));
}

SelectFileDialogImplPortal::SelectFileDialogImplPortal(
    Listener* listener,
    std::unique_ptr<ui::SelectFilePolicy> policy)
    : SelectFileDialogImpl(listener, std::move(policy)) {}

SelectFileDialogImplPortal::~SelectFileDialogImplPortal() = default;

// static
bool SelectFileDialogImplPortal::IsPortalAvailable() {
  bool available = false;
  base::WaitableEvent event;

  dbus_thread_linux::GetTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(&IsPortalAvailableOnTaskRunner,
                     base::Unretained(&available), base::Unretained(&event)));

  // TODO: make this work
  // LOG(INFO) << "Waiting for bus thread!";
  // event.Wait();
  available = true;

  VLOG(1) << "File chooser portal available: " << (available ? "yes" : "no");
  return available;
}

// static
void SelectFileDialogImplPortal::DestroyPortalConnection() {
  dbus_thread_linux::GetTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce([]() { AcquireBusOnTaskRunner()->ShutdownAndBlock(); }));
}

// static
void SelectFileDialogImplPortal::IsPortalAvailableOnTaskRunner(
    bool* out_available,
    base::WaitableEvent* event) {
  // LOG(INFO) << "On bus thread!";
  dbus::Bus* bus = AcquireBusOnTaskRunner();

  dbus::ObjectProxy* dbus_proxy =
      bus->GetObjectProxy(DBUS_SERVICE_DBUS, dbus::ObjectPath(DBUS_PATH_DBUS));

  if (IsPortalRunningOnTaskRunner(dbus_proxy) ||
      IsPortalActivatableOnTaskRunner(dbus_proxy)) {
    dbus::ObjectProxy* portal =
        bus->GetObjectProxy(kXdgPortalService, kXdgPortalObject);

    FileChooserProperties properties(portal);
    if (!properties.GetAndBlock(&properties.version)) {
      LOG(ERROR) << "Failed to read portal version property";
    } else if (properties.version.value() >= kXdgPortalRequiredVersion) {
      *out_available = true;
    }
  }

  // TODO
  // event->Signal();
}

// static
bool SelectFileDialogImplPortal::IsPortalRunningOnTaskRunner(
    dbus::ObjectProxy* dbus_proxy) {
  dbus::MethodCall method_call(DBUS_INTERFACE_DBUS, kDBusMethodNameHasOwner);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(kXdgPortalService);

  std::unique_ptr<dbus::Response> response = dbus_proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response) {
    return false;
  }

  dbus::MessageReader reader(response.get());
  bool owned = false;
  if (!reader.PopBool(&owned)) {
    LOG(ERROR) << "Failed to read response";
    return false;
  }

  return owned;
}

// static
bool SelectFileDialogImplPortal::IsPortalActivatableOnTaskRunner(
    dbus::ObjectProxy* dbus_proxy) {
  dbus::MethodCall method_call(DBUS_INTERFACE_DBUS,
                               kDBusMethodListActivatableNames);

  std::unique_ptr<dbus::Response> response = dbus_proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response) {
    return false;
  }

  dbus::MessageReader reader(response.get());
  std::vector<std::string> names;
  if (!reader.PopArrayOfStrings(&names)) {
    LOG(ERROR) << "Failed to read response";
    return false;
  }

  return base::Contains(names, kXdgPortalService);
}

bool SelectFileDialogImplPortal::IsRunning(
    gfx::NativeWindow parent_window) const {
  if (parent_window && parent_window->GetHost()) {
    auto window = parent_window->GetHost()->GetAcceleratedWidget();

    base::AutoLock locker(parents_lock_);
    return parents_.find(window) != parents_.end();
  }

  return false;
}

void SelectFileDialogImplPortal::SelectFileImpl(
    Type type,
    const base::string16& title,
    const base::FilePath& default_path,
    const FileTypeInfo* file_types,
    int file_type_index,
    const base::FilePath::StringType& default_extension,
    gfx::NativeWindow owning_window,
    void* params) {
  auto info = std::make_unique<CallInfo>();
  info->type = type;
  info->listener_task_runner = base::SequencedTaskRunnerHandle::Get();
  info->params = params;

  if (owning_window && owning_window->GetHost()) {
    info->parent = owning_window->GetHost()->GetAcceleratedWidget();

    base::AutoLock locker(parents_lock_);
    parents_.insert(*info->parent);
  }

  if (file_types) {
    file_types_ = *file_types;
  }

  file_type_index_ = file_type_index;

  PortalFilterSet filter_set = BuildFilterSet();
  dbus_thread_linux::GetTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SelectFileDialogImplPortal::SelectFileImplOnTaskRunner,
                     base::Unretained(this), base::Unretained(info.release()),
                     std::move(title), std::move(default_path),
                     std::move(filter_set), std::move(default_extension)));
}

bool SelectFileDialogImplPortal::HasMultipleFileTypeChoicesImpl() {
  return file_types_.extensions.size() > 1;
}

SelectFileDialogImplPortal::AutoCancel::AutoCancel(
    SelectFileDialogImplPortal* portal,
    CallInfo* info)
    : portal_(portal), info_(info) {}

SelectFileDialogImplPortal::AutoCancel::~AutoCancel() {
  if (info_ && portal_->listener_) {
    info_->listener_task_runner->PostTask(
        FROM_HERE, base::BindOnce(
                       [](Listener* listener, void* params) {
                         listener->FileSelectionCanceled(params);
                       },
                       base::Unretained(portal_->listener_),
                       base::Unretained(info_->params)));
  }
}

void SelectFileDialogImplPortal::AutoCancel::Release() {
  info_.release();
}

SelectFileDialogImplPortal::PortalFilterSet
SelectFileDialogImplPortal::BuildFilterSet() {
  PortalFilterSet filter_set;

  for (size_t i = 0; i < file_types_.extensions.size(); ++i) {
    PortalFilter filter;

    for (const std::string& extension : file_types_.extensions[i]) {
      if (extension.empty()) {
        continue;
      }

      filter.patterns.insert(base::StringPrintf("*.%s", extension.c_str()));
    }

    if (filter.patterns.empty()) {
      continue;
    }

    // The description vector may be blank, in which case we are supposed to
    // use some sort of default description based on the filter.
    if (i < file_types_.extension_description_overrides.size()) {
      filter.name =
          base::UTF16ToUTF8(file_types_.extension_description_overrides[i]);
    } else {
      std::vector<std::string> patterns_vector(filter.patterns.begin(),
                                               filter.patterns.end());
      filter.name = base::JoinString(patterns_vector, ",");
    }

    if (i == file_type_index_) {
      filter_set.default_filter = filter;
    }

    filter_set.filters.push_back(std::move(filter));
  }

  if (file_types_.include_all_files && !filter_set.filters.empty()) {
    // Add the *.* filter, but only if we have added other filters (otherwise it
    // is implied).
    PortalFilter filter;
    filter.name = l10n_util::GetStringUTF8(IDS_SAVEAS_ALL_FILES);
    filter.patterns.insert("*.*");

    filter_set.filters.push_back(std::move(filter));
  }

  return filter_set;
}

// static
dbus::Bus* SelectFileDialogImplPortal::AcquireBusOnTaskRunner() {
  static base::NoDestructor<dbus::Bus*> bus([] {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SESSION;
    options.connection_type = dbus::Bus::PRIVATE;
    options.dbus_task_runner = dbus_thread_linux::GetTaskRunner();

    return new dbus::Bus(options);
  }());

  return *bus;
}

void SelectFileDialogImplPortal::SelectFileImplOnTaskRunner(
    CallInfo* info,
    base::string16 title,
    base::FilePath default_path,
    PortalFilterSet filter_set,
    base::FilePath::StringType default_extension) {
  dbus::Bus* bus = AcquireBusOnTaskRunner();

  std::string method;

  switch (info->type) {
    case SELECT_FOLDER:
    case SELECT_UPLOAD_FOLDER:
    case SELECT_EXISTING_FOLDER:
    case SELECT_OPEN_FILE:
    case SELECT_OPEN_MULTI_FILE:
      method = kFileChooserMethodOpenFile;
      break;
    case SELECT_SAVEAS_FILE:
      method = kFileChooserMethodSaveFile;
      break;
    case SELECT_NONE:
      NOTREACHED();
      break;
  }

  dbus::MethodCall method_call(kFileChooserInterfaceName, method);
  dbus::MessageWriter writer(&method_call);

  if (info->parent) {
    // XXX: Assumes X11.
    std::string parent_window =
        base::StringPrintf("x11:%u", static_cast<std::uint32_t>(*info->parent));
    writer.AppendString(parent_window);
  } else {
    writer.AppendString("");
  }

  if (!title.empty()) {
    writer.AppendString(base::UTF16ToUTF8(title));
  } else {
    int message_id;
    if (info->type == SELECT_SAVEAS_FILE) {
      message_id = IDS_SAVEAS_ALL_FILES;
    } else if (info->type == SELECT_OPEN_MULTI_FILE) {
      message_id = IDS_OPEN_FILES_DIALOG_TITLE;
    } else {
      message_id = IDS_OPEN_FILE_DIALOG_TITLE;
    }
    writer.AppendString(l10n_util::GetStringUTF8(message_id));
  }

  dbus::MessageWriter options_writer(nullptr);
  writer.OpenArray("{sv}", &options_writer);

  std::string handle_token =
      base::StringPrintf("handle_%d", handle_token_counter_++);
  AppendStringOption(&options_writer, kFileChooserOptionHandleToken,
                     handle_token);

  switch (info->type) {
    case SELECT_SAVEAS_FILE:
      AppendStringOption(&options_writer, kFileChooserOptionAcceptLabel,
                         kSaveLabel);
      break;
    case SELECT_UPLOAD_FOLDER:
      AppendStringOption(&options_writer, kFileChooserOptionAcceptLabel,
                         l10n_util::GetStringUTF8(
                             IDS_SELECT_UPLOAD_FOLDER_DIALOG_UPLOAD_BUTTON));
      break;
    default:
      AppendStringOption(&options_writer, kFileChooserOptionAcceptLabel,
                         kOpenLabel);
      break;
  }

  if (info->type == SELECT_UPLOAD_FOLDER ||
      info->type == SELECT_EXISTING_FOLDER) {
    AppendBoolOption(&options_writer, kFileChooserOptionDirectory, true);
  } else if (info->type == SELECT_OPEN_MULTI_FILE) {
    AppendBoolOption(&options_writer, kFileChooserOptionMultiple, true);
  }

  AppendFiltersOption(&options_writer, filter_set.filters);
  if (filter_set.default_filter) {
    dbus::MessageWriter option_writer(nullptr);
    options_writer.OpenDictEntry(&option_writer);

    option_writer.AppendString(kFileChooserOptionCurrentFilter);

    dbus::MessageWriter value_writer(nullptr);
    option_writer.OpenVariant("(sa(us))", &value_writer);

    AppendFilterStruct(&value_writer, *filter_set.default_filter);

    option_writer.CloseContainer(&value_writer);
    options_writer.CloseContainer(&option_writer);
  }

  if (info->type == SELECT_SAVEAS_FILE) {
    if (CallDirectoryExistsOnUIThread(default_path)) {
      AppendByteStringOption(&options_writer, kFileChooserOptionCurrentFolder,
                             default_path.value());
    } else {
      AppendByteStringOption(&options_writer, kFileChooserOptionCurrentFile,
                             default_path.value());
    }
  }

  writer.CloseContainer(&options_writer);

  // The sender part of the handle object contains the D-Bus connection name
  // without the prefix colon and with all dots replaced with underscores.
  std::string sender_part;
  base::ReplaceChars(bus->GetConnectionName().substr(1), ".", "_",
                     &sender_part);

  dbus::ObjectPath expected_handle_path(
      base::StringPrintf("/org/freedesktop/portal/desktop/request/%s/%s",
                         sender_part.c_str(), handle_token.c_str()));

  info->handle = bus->GetObjectProxy(kXdgPortalService, expected_handle_path);
  ConnectToHandle(info);

  dbus::ObjectProxy* portal =
      bus->GetObjectProxy(kXdgPortalService, kXdgPortalObject);
  portal->CallMethodWithErrorResponse(
      &method_call, dbus::ObjectProxy::TIMEOUT_INFINITE,
      base::BindOnce(&SelectFileDialogImplPortal::OnCallResponse,
                     base::Unretained(this), base::Unretained(bus),
                     base::Unretained(info)));
}

void SelectFileDialogImplPortal::AppendStringOption(dbus::MessageWriter* writer,
                                                    const std::string& name,
                                                    const std::string& value) {
  dbus::MessageWriter option_writer(nullptr);
  writer->OpenDictEntry(&option_writer);

  option_writer.AppendString(name);
  option_writer.AppendVariantOfString(value);

  writer->CloseContainer(&option_writer);
}

void SelectFileDialogImplPortal::AppendByteStringOption(
    dbus::MessageWriter* writer,
    const std::string& name,
    const std::string& value) {
  dbus::MessageWriter option_writer(nullptr);
  writer->OpenDictEntry(&option_writer);

  option_writer.AppendString(name);

  dbus::MessageWriter value_writer(nullptr);
  option_writer.OpenVariant("ay", &value_writer);

  value_writer.AppendArrayOfBytes(
      reinterpret_cast<const std::uint8_t*>(value.c_str()),
      // size + 1 will include the null terminator.
      value.size() + 1);

  option_writer.CloseContainer(&value_writer);
  writer->CloseContainer(&option_writer);
}

void SelectFileDialogImplPortal::AppendBoolOption(dbus::MessageWriter* writer,
                                                  const std::string& name,
                                                  bool value) {
  dbus::MessageWriter option_writer(nullptr);
  writer->OpenDictEntry(&option_writer);

  option_writer.AppendString(name);
  option_writer.AppendVariantOfBool(value);

  writer->CloseContainer(&option_writer);
}

void SelectFileDialogImplPortal::AppendFiltersOption(
    dbus::MessageWriter* writer,
    const std::vector<PortalFilter>& filters) {
  dbus::MessageWriter option_writer(nullptr);
  writer->OpenDictEntry(&option_writer);

  option_writer.AppendString(kFileChooserOptionFilters);

  dbus::MessageWriter variant_writer(nullptr);
  option_writer.OpenVariant("a(sa(us))", &variant_writer);

  dbus::MessageWriter filters_writer(nullptr);
  variant_writer.OpenArray("(sa(us))", &filters_writer);

  for (const PortalFilter& filter : filters) {
    AppendFilterStruct(&filters_writer, filter);
  }

  variant_writer.CloseContainer(&filters_writer);
  option_writer.CloseContainer(&variant_writer);
  writer->CloseContainer(&option_writer);
}

void SelectFileDialogImplPortal::AppendFilterStruct(
    dbus::MessageWriter* writer,
    const PortalFilter& filter) {
  dbus::MessageWriter filter_writer(nullptr);
  writer->OpenStruct(&filter_writer);

  filter_writer.AppendString(filter.name);

  dbus::MessageWriter patterns_writer(nullptr);
  filter_writer.OpenArray("(us)", &patterns_writer);

  for (const std::string& pattern : filter.patterns) {
    dbus::MessageWriter pattern_writer(nullptr);
    patterns_writer.OpenStruct(&pattern_writer);

    pattern_writer.AppendUint32(kFileChooserFilterKindGlob);
    pattern_writer.AppendString(pattern);

    patterns_writer.CloseContainer(&pattern_writer);
  }

  filter_writer.CloseContainer(&patterns_writer);
  writer->CloseContainer(&filter_writer);
}

void SelectFileDialogImplPortal::ConnectToHandle(CallInfo* info) {
  info->handle->ConnectToSignal(
      kXdgPortalRequestInterfaceName, kXdgPortalResponseSignal,
      base::BindRepeating(&SelectFileDialogImplPortal::OnResponseSignalEmitted,
                          base::Unretained(this), base::Unretained(info)),
      base::BindOnce(&SelectFileDialogImplPortal::OnResponseSignalConnected,
                     base::Unretained(this), base::Unretained(info)));
}

void SelectFileDialogImplPortal::DetachAndUnparent(CallInfo* info) {
  LOG(INFO) << "DetachAndUnparent " << info;
  if (info->handle) {
    LOG(INFO) << "Detach handle";
    info->handle->Detach();
    info->handle = nullptr;
  }

  if (info->parent) {
    LOG(INFO) << "Detach parent";
    base::AutoLock locker(parents_lock_);
    parents_.erase(*info->parent);
    info->parent.reset();
  }

  LOG(INFO) << "DetachAndUnparent end";
}

void SelectFileDialogImplPortal::OnCallResponse(
    dbus::Bus* bus,
    CallInfo* info,
    dbus::Response* response,
    dbus::ErrorResponse* error_response) {
  AutoCancel canceller(this, info);

  if (response) {
    dbus::MessageReader reader(response);
    dbus::ObjectPath actual_handle_path;
    if (!reader.PopObjectPath(&actual_handle_path)) {
      LOG(ERROR) << "Invalid portal response";
    } else {
      if (info->handle->object_path() != actual_handle_path) {
        info->handle->Detach();
        info->handle =
            bus->GetObjectProxy(kXdgPortalService, actual_handle_path);
        ConnectToHandle(info);
      }

      canceller.Release();
      // Return before the detach calls are performed.
      return;
    }
  } else if (error_response) {
    std::string error_name = error_response->GetErrorName();
    std::string error_message;
    dbus::MessageReader reader(error_response);
    reader.PopString(&error_message);

    LOG(ERROR) << "Portal returned error: " << error_name << ": "
               << error_message;
  } else {
    NOTREACHED();
  }

  // All error paths end up here.
  DetachAndUnparent(info);
}

void SelectFileDialogImplPortal::OnResponseSignalEmitted(CallInfo* info,
                                                         dbus::Signal* signal) {
  DetachAndUnparent(info);
  AutoCancel canceller(this, info);

  dbus::MessageReader reader(signal);

  std::uint32_t response;
  if (!reader.PopUint32(&response)) {
    LOG(ERROR) << "Failed to read response ID";
    return;
  }

  if (response != 0) {
    return;
  }

  dbus::MessageReader results_reader(nullptr);
  if (!reader.PopArray(&results_reader)) {
    LOG(ERROR) << "Failed to read file chooser variant";
    return;
  }

  while (results_reader.HasMoreData()) {
    dbus::MessageReader entry_reader(nullptr);
    std::string key;
    if (!results_reader.PopDictEntry(&entry_reader) ||
        !entry_reader.PopString(&key)) {
      LOG(ERROR) << "Failed to read response entry";
      return;
    }

    if (key == "uris") {
      dbus::MessageReader uris_reader(nullptr);
      std::vector<std::string> uris;
      if (!entry_reader.PopVariant(&uris_reader) ||
          !uris_reader.PopArrayOfStrings(&uris)) {
        LOG(ERROR) << "Failed to read response entry value";
        return;
      }

      std::vector<base::FilePath> paths;
      for (const std::string& uri : uris) {
        // GURL url(uri);
        // if (!url.is_valid() || !url.SchemeIsFile()) {
        //   LOG(ERROR) << "Ignoring unknown/invalid file chooser URI: " << uri;
        //   continue;
        // }

        if (!base::StartsWith(uri, kFileUriPrefix,
                              base::CompareCase::SENSITIVE)) {
          LOG(ERROR) << "Ignoring unknown file chooser URI: " << uri;
          continue;
        }

        base::StringPiece encoded_path(uri);
        encoded_path.remove_prefix(strlen(kFileUriPrefix));

        url::RawCanonOutputT<base::char16> decoded_uri;
        url::DecodeURLEscapeSequences(encoded_path.data(), encoded_path.size(),
                                      url::DecodeURLMode::kUTF8OrIsomorphic,
                                      &decoded_uri);
        paths.emplace_back(base::UTF16ToUTF8(
            base::StringPiece16(decoded_uri.data(), decoded_uri.length())));
        // paths.emplace_back(url.path());
      }

      // All the URIs failed, do nothing, then AutoCancel will cancel as usual.
      if (!paths.empty()) {
        canceller.Release();
        // Because AutoCancel has now lost ownership of the CallInfo, we wrap it
        // in a unique_ptr to ensure it still is deleted.
        std::unique_ptr<CallInfo> info_ownership(info);

        if (listener_) {
          info->listener_task_runner->PostTask(
              FROM_HERE,
              base::BindOnce(
                  [](Type type, Listener* listener,
                     std::vector<base::FilePath> paths, void* params) {
                    if (type == SELECT_OPEN_MULTI_FILE) {
                      listener->MultiFilesSelected(paths, params);
                    } else if (paths.size() > 1) {
                      LOG(ERROR)
                          << "Got >1 file URI from a single-file chooser";
                    } else {
                      // The meaning of the index isn't clear, see
                      // select_file_dialog_impl_kde.cc.
                      listener->FileSelected(paths.front(), 1, params);
                    }
                  },
                  info->type, base::Unretained(listener_), std::move(paths),
                  base::Unretained(info->params)));
        }
      }
    }
  }
}

void SelectFileDialogImplPortal::OnResponseSignalConnected(
    CallInfo* info,
    const std::string& interface,
    const std::string& signal,
    bool connected) {
  AutoCancel canceller(this, info);

  if (!connected) {
    LOG(ERROR) << "Could not connect to Response signal";
    DetachAndUnparent(info);
  } else {
    canceller.Release();
  }
}

int SelectFileDialogImplPortal::handle_token_counter_ = 0;

}  // namespace gtk
