// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/capture_mode/capture_mode_feature_pod_controller.h"

#include "ash/capture_mode/capture_mode_controller.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/tray/system_tray_item_uma_type.h"
#include "ash/system/unified/feature_pod_button.h"
#include "ui/base/l10n/l10n_util.h"

namespace ash {

CaptureModeFeaturePodController::CaptureModeFeaturePodController() = default;
CaptureModeFeaturePodController::~CaptureModeFeaturePodController() = default;

FeaturePodButton* CaptureModeFeaturePodController::CreateButton() {
  DCHECK(!button_);
  button_ = new FeaturePodButton(this, /*is_togglable=*/false);
  button_->SetVectorIcon(kCaptureModeIcon);
  button_->SetLabel(
      l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_CAPTURE_MODE_BUTTON_LABEL));
  return button_;
}

void CaptureModeFeaturePodController::OnIconPressed() {
  CaptureModeController::Get()->Start();
}

SystemTrayItemUmaType CaptureModeFeaturePodController::GetUmaType() const {
  return SystemTrayItemUmaType::UMA_SCREEN_CAPTURE;
}

}  // namespace ash
