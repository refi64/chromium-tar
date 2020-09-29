// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GTK_SELECT_FILE_DIALOG_IMPL_PORTAL_H_
#define UI_GTK_SELECT_FILE_DIALOG_IMPL_PORTAL_H_

#include "base/memory/scoped_refptr.h"
#include "base/optional.h"
#include "base/sequenced_task_runner.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_proxy.h"
#include "ui/gtk/select_file_dialog_impl.h"

namespace gtk {

// Implementation of SelectFileDialog that shows a KDE common dialog for
// choosing a file or folder. This acts as a modal dialog.
class SelectFileDialogImplPortal : public SelectFileDialogImpl {
 public:
  SelectFileDialogImplPortal(Listener* listener,
                             std::unique_ptr<ui::SelectFilePolicy> policy);

  static bool IsPortalAvailable();
  static void DestroyPortalConnection();

 protected:
  ~SelectFileDialogImplPortal() override;

  // BaseShellDialog implementation:
  bool IsRunning(gfx::NativeWindow parent_window) const override;

  // SelectFileDialog implementation.
  // |params| is user data we pass back via the Listener interface.
  void SelectFileImpl(Type type,
                      const base::string16& title,
                      const base::FilePath& default_path,
                      const FileTypeInfo* file_types,
                      int file_type_index,
                      const base::FilePath::StringType& default_extension,
                      gfx::NativeWindow owning_window,
                      void* params) override;

  bool HasMultipleFileTypeChoicesImpl() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(SelectFileDialogImplPortal);

  struct PortalFilter {
    std::string name;
    std::set<std::string> patterns;
  };

  struct PortalFilterSet {
    std::vector<PortalFilter> filters;
    base::Optional<PortalFilter> default_filter;
  };

  struct CallInfo {
    dbus::ObjectProxy* handle = nullptr;
    base::Optional<gfx::AcceleratedWidget> parent;
    Type type;
    scoped_refptr<base::SequencedTaskRunner> listener_task_runner;
    void* params = nullptr;
  };

  class AutoCancel {
   public:
    AutoCancel(SelectFileDialogImplPortal* portal, CallInfo* info);
    ~AutoCancel();

    void Release();

   private:
    SelectFileDialogImplPortal* portal_;
    std::unique_ptr<CallInfo> info_;
  };

  PortalFilterSet BuildFilterSet();

  static dbus::Bus* AcquireBusOnTaskRunner();

  static void IsPortalAvailableOnTaskRunner(bool* out_available,
                                            base::WaitableEvent* event);

  static bool IsPortalRunningOnTaskRunner(dbus::ObjectProxy* dbus_proxy);
  static bool IsPortalActivatableOnTaskRunner(dbus::ObjectProxy* dbus_proxy);

  void SelectFileImplOnTaskRunner(CallInfo* info,
                                  base::string16 title,
                                  base::FilePath default_path,
                                  PortalFilterSet filter_set,
                                  base::FilePath::StringType default_extension);

  void AppendStringOption(dbus::MessageWriter* writer,
                          const std::string& name,
                          const std::string& value);
  void AppendByteStringOption(dbus::MessageWriter* writer,
                              const std::string& name,
                              const std::string& value);
  void AppendBoolOption(dbus::MessageWriter* writer,
                        const std::string& name,
                        bool value);

  void AppendFiltersOption(dbus::MessageWriter* writer,
                           const std::vector<PortalFilter>& filters);
  void AppendFilterStruct(dbus::MessageWriter* writer,
                          const PortalFilter& filter);

  void ConnectToHandle(CallInfo* info);
  void DetachAndUnparent(CallInfo* info);

  void OnCallResponse(dbus::Bus* bus,
                      CallInfo* info,
                      dbus::Response* response,
                      dbus::ErrorResponse* error_response);

  void OnResponseSignalEmitted(CallInfo* info, dbus::Signal* signal);

  void OnResponseSignalConnected(CallInfo* info,
                                 const std::string& interface,
                                 const std::string& signal,
                                 bool connected);

  mutable base::Lock parents_lock_;
  std::set<gfx::AcceleratedWidget> parents_;

  static int handle_token_counter_;
};

}  // namespace gtk

#endif  // UI_GTK_SELECT_FILE_DIALOG_IMPL_PORTAL_H_
