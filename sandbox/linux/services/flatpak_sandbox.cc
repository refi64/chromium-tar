// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox/linux/services/flatpak_sandbox.h"

#include <signal.h>
#include <sstream>
#include <string>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/threading/thread_restrictions.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "dbus/property.h"

namespace sandbox {

namespace {
const base::FilePath kFlatpakAppPath("/app");
const base::FilePath kFlatpakInfoPath("/.flatpak-info");

const char kFlatpakPortalServiceName[] = "org.freedesktop.portal.Flatpak";
const char kFlatpakPortalObjectPath[] = "/org/freedesktop/portal/Flatpak";
const char kFlatpakPortalInterfaceName[] = "org.freedesktop.portal.Flatpak";

#ifndef NDEBUG
const char kDisableFullFlatpakSandbox[] = "disable-full-flatpak-sandbox";
#endif

struct PortalProperties : dbus::PropertySet {
  dbus::Property<uint32_t> version;
  dbus::Property<uint32_t> supports;

  enum FlatpakPortalSupports {
    kFlatpakPortal_ExposePids = 1 << 0,
  };

  explicit PortalProperties(dbus::ObjectProxy* object_proxy)
      : dbus::PropertySet(object_proxy, kFlatpakPortalInterfaceName, {}) {
    RegisterProperty("version", &version);
    RegisterProperty("supports", &supports);
  }

  ~PortalProperties() override = default;
};

void WriteStringAsByteArray(dbus::MessageWriter* writer,
                            const std::string& str) {
  writer->AppendArrayOfBytes(reinterpret_cast<const uint8_t*>(str.c_str()),
                             str.size() + 1);
}

void WriteFdPairMap(dbus::MessageWriter* writer, int source_fd, int dest_fd) {
  dbus::MessageWriter entry_writer(nullptr);
  writer->OpenDictEntry(&entry_writer);

  entry_writer.AppendUint32(dest_fd);
  entry_writer.AppendFileDescriptor(source_fd);

  writer->CloseContainer(&entry_writer);
}

}  // namespace

constexpr base::TimeDelta FlatpakSandbox::kDefaultKillDelay;

enum FlatpakSpawnFlags {
  kFlatpakSpawn_ClearEnvironment = 1 << 0,
  kFlatpakSpawn_Latest = 1 << 1,
  kFlatpakSpawn_Sandbox = 1 << 2,
  kFlatpakSpawn_NoNetwork = 1 << 3,
  kFlatpakSpawn_WatchBus = 1 << 4,
  kFlatpakSpawn_ExposePids = 1 << 5,
  kFlatpakSpawn_NotifyStart = 1 << 6,
};

enum FlatpakSpawnSandboxFlags {
  kFlatpakSpawnSandbox_ShareDisplay = 1 << 0,
  kFlatpakSpawnSandbox_ShareSound = 1 << 1,
  kFlatpakSpawnSandbox_ShareGpu = 1 << 2,
  kFlatpakSpawnSandbox_ShareSessionBus = 1 << 3,
  kFlatpakSpawnSandbox_ShareA11yBus = 1 << 4,
};

FlatpakSandbox::FlatpakSandbox()
    : bus_thread_("FlatpakPortalBus"), process_info_cv_(&process_info_lock_) {}

// static
FlatpakSandbox* FlatpakSandbox::GetInstance() {
  static base::NoDestructor<FlatpakSandbox> instance;
  return instance.get();
}

FlatpakSandbox::SandboxLevel FlatpakSandbox::GetSandboxLevel() {
  if (sandbox_level_) {
    return *sandbox_level_;
  }

  // XXX: These operations shouldn't actually have a major blocking time,
  // as .flatpak-info is on a tmpfs.
  base::ScopedAllowBlocking scoped_allow_blocking;

  if (!base::PathExists(kFlatpakInfoPath)) {
    sandbox_level_ = SandboxLevel::kNone;
  } else {
    // chrome has an INI parser, but sandbox can't depend on anything inside
    // chrome, so the .flatpak-info INI is manually checked for the sandbox
    // option.

    std::string contents;
    CHECK(ReadFileToString(kFlatpakInfoPath, &contents));
    DCHECK(!contents.empty());

    std::istringstream iss(contents);
    std::string line;
    bool in_instance = false;
    while (std::getline(iss, line)) {
      if (!line.empty() && line[0] == '[') {
        DCHECK(line.back() == ']');

        if (line == "[Instance]") {
          DCHECK(!in_instance);
          in_instance = true;
        } else if (in_instance) {
          // Leaving the Instance section, sandbox=true can't come now.
          break;
        }
      } else if (in_instance && line == "sandbox=true") {
        sandbox_level_ = SandboxLevel::kRestricted;
        break;
      }
    }

    if (!sandbox_level_) {
      sandbox_level_ = SandboxLevel::kFlatpak;
    }
  }

#ifndef NDEBUG
  if (sandbox_level_ == SandboxLevel::kFlatpak &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          kDisableFullFlatpakSandbox)) {
    sandbox_level_ = SandboxLevel::kRestricted;
  }
#endif

  return *sandbox_level_;
}

base::ProcessId FlatpakSandbox::LaunchProcess(
    const base::CommandLine& cmdline,
    const base::LaunchOptions& launch_options,
    bool allow_x11 /*= false*/) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  base::ScopedAllowBaseSyncPrimitives allow_wait;

  StartBusThread();

  VLOG(1) << "Running via Flatpak: " << cmdline.GetCommandLineString();

  DCHECK(GetSandboxLevel() != SandboxLevel::kNone);

  // These options are not supported with the Flatpak sandbox.
  DCHECK(launch_options.clone_flags == 0);
  DCHECK(!launch_options.wait);
  DCHECK(!launch_options.allow_new_privs);
  DCHECK(launch_options.real_path.empty());
  DCHECK(launch_options.pre_exec_delegate == nullptr);
  DCHECK(launch_options.maximize_rlimits == nullptr);

  base::ProcessId external_pid = base::kNullProcessId;
  base::WaitableEvent event;

  bus_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&FlatpakSandbox::LaunchProcessOnBusThread,
                     base::Unretained(this), base::Unretained(&external_pid),
                     base::Unretained(&event), cmdline, launch_options,
                     allow_x11));
  event.Wait();

  return external_pid;
}

base::Process FlatpakSandbox::GetRelativePid(base::ProcessId external_pid) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  base::ScopedAllowBaseSyncPrimitives allow_wait;

  base::AutoLock locker(process_info_lock_);

  for (;;) {
    auto it = running_processes_.find(external_pid);
    if (it == running_processes_.end()) {
      // Process already died, most likely it never started.
      // Collect / ignore its exit status.
      if (exited_process_statuses_.erase(external_pid) == 0) {
        ignore_status_.insert(external_pid);
      }

      LOG(INFO) << "Already died: " << external_pid;
      return base::Process();
    } else if (it->second == 0) {
      // No relative PID is known yet.
      VLOG(1) << "Waiting for " << external_pid;
      process_info_cv_.Wait();
      continue;
    } else {
      VLOG(1) << "Got " << external_pid << " => " << it->second;
      return base::Process(it->second);
    }
  }
}

bool FlatpakSandbox::SendSignal(base::ProcessId external_pid,
                                int signal,
                                bool process_group /* = false*/) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  base::ScopedAllowBaseSyncPrimitives allow_wait;

  StartBusThread();

  bool success = false;
  base::WaitableEvent event;

  bus_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&FlatpakSandbox::SendSignalOnBusThread,
                     base::Unretained(this), base::Unretained(&success),
                     base::Unretained(&event), external_pid, signal,
                     process_group));
  event.Wait();
  return success;
}

base::TerminationStatus FlatpakSandbox::GetTerminationStatus(
    base::ProcessId external_pid,
    int* exit_code) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  base::AutoLock locker(process_info_lock_);

  auto it = exited_process_statuses_.find(external_pid);
  if (it == exited_process_statuses_.end()) {
    if (running_processes_.find(external_pid) == running_processes_.end()) {
      LOG(ERROR) << "PID " << external_pid << " is not currently tracked";
    }

    *exit_code = 0;
    return base::TERMINATION_STATUS_STILL_RUNNING;
  }

  *exit_code = it->second;
  exited_process_statuses_.erase(it);
  return base::GetTerminationStatusForExitCode(*exit_code);
}

base::TerminationStatus FlatpakSandbox::GetKnownDeadTerminationStatus(
    base::ProcessId external_pid,
    int* exit_code) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  SendSignal(external_pid, SIGKILL);

  base::AutoLock locker(process_info_lock_);

  for (;;) {
    auto it = exited_process_statuses_.find(external_pid);
    if (it == exited_process_statuses_.end()) {
      if (running_processes_.find(external_pid) == running_processes_.end()) {
        LOG(ERROR) << "PID " << external_pid << " is not currently tracked";
        *exit_code = 0;
        return base::TERMINATION_STATUS_NORMAL_TERMINATION;
      }

      process_info_cv_.Wait();
      continue;
    } else {
      *exit_code = it->second;
      exited_process_statuses_.erase(it);
      return base::GetTerminationStatusForExitCode(*exit_code);
    }
  }
}

void FlatpakSandbox::ForceTermination(
    base::ProcessId external_pid,
    base::TimeDelta kill_delay /* = kDefaultKillDelay*/) {
  {
    base::AutoLock locker(process_info_lock_);

    if (running_processes_.find(external_pid) == running_processes_.end()) {
      // Already died, don't bother signaling and just collect any statuses.
      exited_process_statuses_.erase(external_pid);
      return;
    }

    // Collect the process on death if this was SIGKILL.
    if (kill_delay == base::TimeDelta::Min()) {
      ignore_status_.insert(external_pid);
    }
  }

  if (!SendSignal(external_pid,
                  kill_delay != base::TimeDelta::Min() ? SIGTERM : SIGKILL)) {
    DLOG(ERROR) << "Unable to terminate process with external PID "
                << external_pid;
  }

  if (kill_delay != base::TimeDelta::Min()) {
    bus_thread_.task_runner()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&FlatpakSandbox::ContinueForceTerminationOnBusThread,
                       base::Unretained(this), external_pid),
        kill_delay);
  }
}

void FlatpakSandbox::StartBusThread() {
  if (!bus_thread_.IsRunning()) {
    base::Thread::Options options;
    options.message_pump_type = base::MessagePumpType::IO;
    CHECK(bus_thread_.StartWithOptions(options));

    bus_thread_.task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&FlatpakSandbox::InitializeBusThread,
                                  base::Unretained(this)));
  }
}

dbus::Bus* FlatpakSandbox::AcquireBusFromBusThread() {
  // Note that destruction of the bus is not a concern, because once the
  // thread dies its bus connection will be terminated anyway and the
  // portal will notice.
  static base::NoDestructor<dbus::Bus*> bus([] {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SESSION;
    options.connection_type = dbus::Bus::PRIVATE;
    options.dbus_task_runner = base::SequencedTaskRunnerHandle::Get();

    return new dbus::Bus(options);
  }());

  return *bus;
}

dbus::ObjectProxy* FlatpakSandbox::GetPortalObjectProxy() {
  return AcquireBusFromBusThread()->GetObjectProxy(
      kFlatpakPortalServiceName, dbus::ObjectPath(kFlatpakPortalObjectPath));
}

void FlatpakSandbox::InitializeBusThread() {
  dbus::ObjectProxy* object_proxy = GetPortalObjectProxy();

  PortalProperties properties(object_proxy);
  properties.ConnectSignals();

  CHECK(properties.GetAndBlock(&properties.version))
      << "Failed to get portal version";
  CHECK(properties.GetAndBlock(&properties.supports))
      << "Failed to get portal supports";

  if (properties.version.value() < 4) {
    LOG(FATAL) << "Your Flatpak version is too old, please update it";
  }

  if (!(properties.supports.value() &
        PortalProperties::kFlatpakPortal_ExposePids)) {
    LOG(FATAL) << "Your Flatpak installation is setuid, which is not supported";
  }

  object_proxy->ConnectToSignal(
      kFlatpakPortalInterfaceName, "SpawnStarted",
      base::BindRepeating(&FlatpakSandbox::OnSpawnStartedSignal,
                          base::Unretained(this)),
      base::BindOnce(&FlatpakSandbox::OnSignalConnected,
                     base::Unretained(this)));

  object_proxy->ConnectToSignal(
      kFlatpakPortalInterfaceName, "SpawnExited",
      base::BindRepeating(&FlatpakSandbox::OnSpawnExitedSignal,
                          base::Unretained(this)),
      base::BindOnce(&FlatpakSandbox::OnSignalConnected,
                     base::Unretained(this)));
}

void FlatpakSandbox::OnSignalConnected(const std::string& interface,
                                       const std::string& signal,
                                       bool connected) {
  // It's not safe to spawn processes without being able to track their deaths.
  CHECK(connected) << "Failed to connect to signal " << signal;
}

void FlatpakSandbox::OnSpawnStartedSignal(dbus::Signal* signal) {
  dbus::MessageReader reader(signal);
  uint32_t external_pid, relative_pid;

  if (!reader.PopUint32(&external_pid) || !reader.PopUint32(&relative_pid)) {
    LOG(ERROR) << "Invalid SpawnStarted signal";
    return;
  }

  VLOG(1) << "Received SpawnStarted: " << external_pid << ' ' << relative_pid;

  if (relative_pid == 0) {
    // Process likely already died, so just wait for SpawnExited.
    LOG(INFO) << "PID " << external_pid << " has no relative PID";
    return;
  }

  base::AutoLock locker(process_info_lock_);

  auto it = running_processes_.find(external_pid);
  if (it == running_processes_.end()) {
    LOG(ERROR) << "Process " << external_pid
               << " is already dead or not tracked";
    return;
  }

  it->second = relative_pid;

  process_info_cv_.Broadcast();
}

void FlatpakSandbox::OnSpawnExitedSignal(dbus::Signal* signal) {
  dbus::MessageReader reader(signal);
  uint32_t external_pid, exit_status;

  if (!reader.PopUint32(&external_pid) || !reader.PopUint32(&exit_status)) {
    LOG(ERROR) << "Invalid SpawnExited signal";
    return;
  }

  VLOG(1) << "Received SpawnExited: " << external_pid << ' ' << exit_status;

  base::AutoLock locker(process_info_lock_);

  auto it = ignore_status_.find(external_pid);
  if (it == ignore_status_.end()) {
    exited_process_statuses_[external_pid] = exit_status;
  } else {
    ignore_status_.erase(it);
  }

  running_processes_.erase(external_pid);

  process_info_cv_.Broadcast();
}

void FlatpakSandbox::LaunchProcessOnBusThread(
    base::ProcessId* out_external_pid,
    base::WaitableEvent* event,
    const base::CommandLine& cmdline,
    const base::LaunchOptions& launch_options,
    bool allow_x11) {
  dbus::ObjectProxy* object_proxy = GetPortalObjectProxy();
  dbus::MethodCall method_call(kFlatpakPortalInterfaceName, "Spawn");
  dbus::MessageWriter writer(&method_call);

  const base::FilePath& current_directory =
      !launch_options.current_directory.empty()
          ? launch_options.current_directory
          // Change to /app since it's guaranteed to always be present in
          // the sandbox.
          : kFlatpakAppPath;
  WriteStringAsByteArray(&writer, current_directory.value());

  dbus::MessageWriter argv_writer(nullptr);
  writer.OpenArray("ay", &argv_writer);

  for (const std::string& arg : cmdline.argv()) {
    WriteStringAsByteArray(&argv_writer, arg);
  }

#ifndef NDEBUG
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          kDisableFullFlatpakSandbox)) {
    std::string arg = "--";
    arg += kDisableFullFlatpakSandbox;
    WriteStringAsByteArray(&argv_writer, arg);
  }
#endif

  writer.CloseContainer(&argv_writer);

  dbus::MessageWriter fds_writer(nullptr);
  writer.OpenArray("{uh}", &fds_writer);

  WriteFdPairMap(&fds_writer, STDIN_FILENO, STDIN_FILENO);
  WriteFdPairMap(&fds_writer, STDOUT_FILENO, STDOUT_FILENO);
  WriteFdPairMap(&fds_writer, STDERR_FILENO, STDERR_FILENO);

  for (const auto& pair : launch_options.fds_to_remap) {
    WriteFdPairMap(&fds_writer, pair.first, pair.second);
  }

  writer.CloseContainer(&fds_writer);

  dbus::MessageWriter env_writer(nullptr);
  writer.OpenArray("{ss}", &env_writer);

  for (const auto& pair : launch_options.environment) {
    dbus::MessageWriter entry_writer(nullptr);
    env_writer.OpenDictEntry(&entry_writer);

    entry_writer.AppendString(pair.first);
    entry_writer.AppendString(pair.second);

    env_writer.CloseContainer(&entry_writer);
  }

  writer.CloseContainer(&env_writer);

  int spawn_flags = kFlatpakSpawn_Sandbox | kFlatpakSpawn_ExposePids |
                    kFlatpakSpawn_NotifyStart;
  int sandbox_flags = 0;

#ifndef NDEBUG
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          kDisableFullFlatpakSandbox)) {
    spawn_flags &= ~kFlatpakSpawn_Sandbox;
  }
#else
#endif

  if (launch_options.clear_environment) {
    spawn_flags |= kFlatpakSpawn_ClearEnvironment;
  }

  if (launch_options.kill_on_parent_death) {
    spawn_flags |= kFlatpakSpawn_WatchBus;
  }

  if (allow_x11) {
    sandbox_flags |= kFlatpakSpawnSandbox_ShareDisplay;
    sandbox_flags |= kFlatpakSpawnSandbox_ShareGpu;
  }

  writer.AppendUint32(spawn_flags);

  dbus::MessageWriter options_writer(nullptr);
  writer.OpenArray("{sv}", &options_writer);

  if (sandbox_flags != 0) {
    dbus::MessageWriter entry_writer(nullptr);
    options_writer.OpenDictEntry(&entry_writer);

    entry_writer.AppendString("sandbox-flags");

    dbus::MessageWriter variant_writer(nullptr);
    entry_writer.OpenVariant("u", &variant_writer);

    variant_writer.AppendUint32(sandbox_flags);

    entry_writer.CloseContainer(&variant_writer);
    options_writer.CloseContainer(&entry_writer);
  }

  writer.CloseContainer(&options_writer);

  object_proxy->CallMethodWithErrorResponse(
      &method_call, dbus::ObjectProxy::TIMEOUT_INFINITE,
      base::BindOnce(&FlatpakSandbox::OnLaunchProcessResponse,
                     base::Unretained(this), base::Unretained(out_external_pid),
                     base::Unretained(event)));
}

void FlatpakSandbox::OnLaunchProcessResponse(
    base::ProcessId* out_external_pid,
    base::WaitableEvent* event,
    dbus::Response* response,
    dbus::ErrorResponse* error_response) {
  if (response) {
    dbus::MessageReader reader(response);
    uint32_t external_pid;
    if (!reader.PopUint32(&external_pid)) {
      LOG(ERROR) << "Invalid Spawn() response";
    } else {
      VLOG(1) << "Spawn() returned PID " << external_pid;
      if (out_external_pid != nullptr) {
        *out_external_pid = external_pid;
      }

      base::AutoLock locker(process_info_lock_);
      running_processes_[external_pid] = 0;
    }
  } else if (error_response) {
    std::string error_name = error_response->GetErrorName();
    std::string error_message;
    dbus::MessageReader reader(error_response);
    reader.PopString(&error_message);

    LOG(ERROR) << "Error calling Spawn(): " << error_name << ": "
               << error_message;
  } else {
    LOG(ERROR) << "Unknown error occurred calling Spawn()";
  }

  if (event != nullptr) {
    event->Signal();
  }
}

void FlatpakSandbox::SendSignalOnBusThread(bool* out_success,
                                           base::WaitableEvent* event,
                                           base::ProcessId external_pid,
                                           int signal,
                                           bool process_group) {
  dbus::ObjectProxy* object_proxy = GetPortalObjectProxy();

  dbus::MethodCall method_call(kFlatpakPortalInterfaceName, "SpawnSignal");
  dbus::MessageWriter writer(&method_call);

  writer.AppendUint32(external_pid);
  writer.AppendUint32(signal);
  writer.AppendBool(process_group);

  object_proxy->CallMethodWithErrorResponse(
      &method_call, dbus::ObjectProxy::TIMEOUT_INFINITE,
      base::BindOnce(&FlatpakSandbox::OnSendSignalResponse,
                     base::Unretained(this), base::Unretained(out_success),
                     base::Unretained(event)));
}

void FlatpakSandbox::OnSendSignalResponse(bool* out_success,
                                          base::WaitableEvent* event,
                                          dbus::Response* response,
                                          dbus::ErrorResponse* error_response) {
  if (response) {
    if (out_success != nullptr) {
      *out_success = true;
    }
  } else if (error_response) {
    std::string error_name = error_response->GetErrorName();
    std::string error_message;
    dbus::MessageReader reader(error_response);
    reader.PopString(&error_message);

    LOG(ERROR) << "Error calling SendSignal(): " << error_name << ": "
               << error_message;
  } else {
    LOG(ERROR) << "Unknown error occurred calling SendSignal()";
  }

  if (event != nullptr) {
    event->Signal();
  }
}

void FlatpakSandbox::ContinueForceTerminationOnBusThread(
    base::ProcessId external_pid) {
  bool do_kill = false;

  {
    base::AutoLock locker(process_info_lock_);

    if (exited_process_statuses_.erase(external_pid) == 0) {
      ignore_status_.insert(external_pid);
      do_kill = true;
    }
  }

  if (do_kill) {
    SendSignalOnBusThread(nullptr, nullptr, external_pid, SIGKILL, false);
  }
}

}  // namespace sandbox
