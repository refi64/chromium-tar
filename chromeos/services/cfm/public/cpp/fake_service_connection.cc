// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/cfm/public/cpp/fake_service_connection.h"

#include "base/bind.h"
#include "base/files/file.h"
#include "base/macros.h"
#include "base/no_destructor.h"
#include "base/sequence_checker.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/invitation.h"

namespace chromeos {
namespace cfm {

FakeServiceConnectionImpl::FakeServiceConnectionImpl() = default;
FakeServiceConnectionImpl::~FakeServiceConnectionImpl() = default;

// Bind to the CfM Service Context Daemon
void FakeServiceConnectionImpl::BindServiceContext(
    mojo::PendingReceiver<::chromeos::cfm::mojom::CfmServiceContext> receiver) {
  CfmHotlineClient::Get()->WaitForServiceToBeAvailable(
      base::BindOnce(&FakeServiceConnectionImpl::CfMContextServiceStarted,
                     base::Unretained(this), std::move(receiver)));
}

void FakeServiceConnectionImpl::CfMContextServiceStarted(
    mojo::PendingReceiver<::chromeos::cfm::mojom::CfmServiceContext> receiver,
    bool is_available) {
  if (!is_available || callback_.is_null()) {
    receiver.reset();
    std::move(callback_).Run(false);
    return;
  }

  // The easiest source of fds is opening /dev/null.
  base::File file = base::File(base::FilePath("/dev/null"),
                               base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  DCHECK(file.IsValid());

  CfmHotlineClient::Get()->BootstrapMojoConnection(
      base::ScopedFD(file.TakePlatformFile()), std::move(callback_));
}

void FakeServiceConnectionImpl::SetCallback(FakeBootstrapCallback callback) {
  callback_ = std::move(callback);
}

}  // namespace cfm
}  // namespace chromeos
