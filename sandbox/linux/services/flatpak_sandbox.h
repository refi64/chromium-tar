// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SANDBOX_LINUX_SERVICES_FLATPAK_SANDBOX_H_
#define SANDBOX_LINUX_SERVICES_FLATPAK_SANDBOX_H_

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/no_destructor.h"
#include "base/optional.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "sandbox/sandbox_export.h"

namespace sandbox {

// Manages the state of and access to the Flatpak sandbox.
// Note that there is a distinction between external and internal PIDs:
// - External PIDs are the PIDs relative to the world outside the sandbox.
// - Internal PIDs are the PIDs relative to the current PID namespace.
// Flatpak's sandbox APIs work primarily with external PIDs, and an
// internal PID must be retrieved from the SpawnStarted signal before
// it is known inside the sandbox's PID namespace.
class SANDBOX_EXPORT FlatpakSandbox {
 public:
  static FlatpakSandbox* GetInstance();

  static constexpr base::TimeDelta kDefaultKillDelay =
      base::TimeDelta::FromSeconds(2);

  // Represents the level of sandboxing inside a Flatpak. kNone means this is
  // not a Flatpak, kFlatpak means it's inside a Flatpak sandbox, and
  // kRestricted means that this is inside a nested Flatpak sandbox with most
  // permissions revoked.
  enum class SandboxLevel { kNone, kFlatpak, kRestricted };

  // Get the current level of sandboxing in this Flatpak.
  SandboxLevel GetSandboxLevel();

  // Launch the given process inside of a Flatpak sandbox. If allow_x11 is true,
  // then the process will be given access to the host's X11 display. On
  // failure, returns kNullProcessId. Note that the return value is the PID
  // relative to the host i.e. outside the sandbox, to get the internal one call
  // GetRelativePid. This is the reason why a vanilla ProcessId is returned
  // rather than a base::Process instance.
  base::ProcessId LaunchProcess(const base::CommandLine& cmdline,
                                const base::LaunchOptions& launch_options,
                                bool allow_x11 = false);

  // Given an external PID, find the PID that the given process appears as
  // inside the current PID namespace. This will block and wait for the
  // SpawnStarted signal to be emitted. If the process has already died
  // or never was started, an invalid process will be returned.
  base::Process GetRelativePid(base::ProcessId external_pid);

  // Sends the given POSIX |signal| to the process with the given external
  // PID. If |process_group| is true, then the signal will be sent to an
  // entire process group.
  bool SendSignal(base::ProcessId external_pid, int signal,
                  bool process_group = false);

  // Gets the termination status and exit code of the given process.
  base::TerminationStatus GetTerminationStatus(base::ProcessId external_pid,
                                               int* exit_code);
  // Sends a kill signal to the given process, then waits for it to
  // terminate and retrieves the termination status and exit code.
  base::TerminationStatus GetKnownDeadTerminationStatus(
      base::ProcessId external_pid, int* exit_code);

  // Forces termination of the given process. SIGTERM will be sent and,
  // if the process has not died by the time |kill_delay| has elapsed,
  // then SIGKILL will be sent. The signal send is synchronous, but the
  // delay is asynchronous.
  // If |kill_delay| is base::TimeDelta::Min(), then SIGKILL will be
  // sent the first time.
  void ForceTermination(base::ProcessId external_pid,
                        base::TimeDelta kill_delay = kDefaultKillDelay);

 private:
  friend class base::NoDestructor<FlatpakSandbox>;

  FlatpakSandbox();
  ~FlatpakSandbox();

  DISALLOW_COPY_AND_ASSIGN(FlatpakSandbox);

  void StartBusThread();
  dbus::Bus* AcquireBusFromBusThread();
  dbus::ObjectProxy* GetPortalObjectProxy();

  void InitializeBusThread();
  void OnSignalConnected(const std::string& interface, const std::string& signal,
                         bool connected);
  void OnSpawnStartedSignal(dbus::Signal* signal);
  void OnSpawnExitedSignal(dbus::Signal* signal);

  void LaunchProcessOnBusThread(base::ProcessId* out_external_pid,
                                base::WaitableEvent* event,
                                const base::CommandLine& cmdline,
                                const base::LaunchOptions& launch_options,
                                bool allow_x11);
  void OnLaunchProcessResponse(base::ProcessId* out_external_pid,
                               base::WaitableEvent* event,
                               dbus::Response* response,
                               dbus::ErrorResponse* error_response);

  void SendSignalOnBusThread(bool* out_success, base::WaitableEvent* event,
                             base::ProcessId external_pid, int signal,
                             bool process_group);
  void OnSendSignalResponse(bool* out_success, base::WaitableEvent* event,
                            dbus::Response* response,
                            dbus::ErrorResponse* error_response);

  void ContinueForceTerminationOnBusThread(base::ProcessId external_pid);

  base::Optional<SandboxLevel> sandbox_level_;
  base::Thread bus_thread_;

  base::Lock process_info_lock_;
  // Note that broadcast is used in the source, because in general
  // very few threads will be contending for the lock.
  base::ConditionVariable process_info_cv_;
  // Map of running external process IDs to their relative PIDs.
  std::map<base::ProcessId, base::ProcessId> running_processes_;
  // Map of a process that has exited to its waitpid status.
  std::map<base::ProcessId, int> exited_process_statuses_;
  // Processes should have their statuses ignored on exit.
  std::set<base::ProcessId> ignore_status_;
};

}  // namespace sandbox

#endif  // SANDBOX_LINUX_SERVICES_FLATPAK_SANDBOX_H_
