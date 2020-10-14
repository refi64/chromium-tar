// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox/policy/linux/bpf_speech_recognition_policy_linux.h"

#include "sandbox/linux/bpf_dsl/bpf_dsl.h"
#include "sandbox/linux/syscall_broker/broker_process.h"
#include "sandbox/linux/system_headers/linux_syscalls.h"
#include "sandbox/policy/linux/sandbox_linux.h"

using sandbox::bpf_dsl::Allow;
using sandbox::bpf_dsl::ResultExpr;
using sandbox::bpf_dsl::Trap;
using sandbox::syscall_broker::BrokerProcess;

namespace sandbox {
namespace policy {

SpeechRecognitionProcessPolicy::SpeechRecognitionProcessPolicy() = default;
SpeechRecognitionProcessPolicy::~SpeechRecognitionProcessPolicy() = default;

ResultExpr SpeechRecognitionProcessPolicy::EvaluateSyscall(
    int system_call_number) const {
  switch (system_call_number) {
    default:
      auto* broker_process = SandboxLinux::GetInstance()->broker_process();
      if (broker_process->IsSyscallAllowed(system_call_number))
        return Trap(BrokerProcess::SIGSYS_Handler, broker_process);

      // Default on the content baseline policy.
      return BPFBasePolicy::EvaluateSyscall(system_call_number);
  }
}

}  // namespace policy
}  // namespace sandbox
