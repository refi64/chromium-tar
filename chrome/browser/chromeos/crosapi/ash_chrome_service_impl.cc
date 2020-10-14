// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/crosapi/ash_chrome_service_impl.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "chrome/browser/chromeos/crosapi/attestation_ash.h"
#include "chrome/browser/chromeos/crosapi/message_center_ash.h"
#include "chrome/browser/chromeos/crosapi/screen_manager_ash.h"
#include "chrome/browser/chromeos/crosapi/select_file_ash.h"
#include "chromeos/crosapi/mojom/attestation.mojom.h"
#include "chromeos/crosapi/mojom/message_center.mojom.h"
#include "chromeos/crosapi/mojom/screen_manager.mojom.h"
#include "chromeos/crosapi/mojom/select_file.mojom.h"

namespace crosapi {

AshChromeServiceImpl::AshChromeServiceImpl(
    mojo::PendingReceiver<mojom::AshChromeService> pending_receiver)
    : receiver_(this, std::move(pending_receiver)),
      screen_manager_ash_(std::make_unique<ScreenManagerAsh>()) {
  // TODO(hidehiko): Remove non-critical log from here.
  // Currently this is the signal that the connection is established.
  LOG(WARNING) << "AshChromeService connected.";
}

AshChromeServiceImpl::~AshChromeServiceImpl() = default;

void AshChromeServiceImpl::BindAttestation(
    mojo::PendingReceiver<crosapi::mojom::Attestation> receiver) {
  attestation_ash_ =
      std::make_unique<crosapi::AttestationAsh>(std::move(receiver));
}

void AshChromeServiceImpl::BindMessageCenter(
    mojo::PendingReceiver<mojom::MessageCenter> receiver) {
  message_center_ash_ = std::make_unique<MessageCenterAsh>(std::move(receiver));
}

void AshChromeServiceImpl::BindSelectFile(
    mojo::PendingReceiver<mojom::SelectFile> receiver) {
  select_file_ash_ = std::make_unique<SelectFileAsh>(std::move(receiver));
}

void AshChromeServiceImpl::BindScreenManager(
    mojo::PendingReceiver<mojom::ScreenManager> receiver) {
  screen_manager_ash_->BindReceiver(std::move(receiver));
}

}  // namespace crosapi
