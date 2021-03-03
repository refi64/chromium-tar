// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/palette/stylus_battery_delegate.h"

#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/style/ash_color_provider.h"
#include "ash/system/power/power_status.h"
#include "ash/system/tray/tray_constants.h"
#include "base/strings/string16.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/color_palette.h"

namespace ash {
namespace {
// Battery percentage threshold used to label the battery level as Low.
constexpr int kStylusLowBatteryThreshold = 24;
}  // namespace

StylusBatteryDelegate::StylusBatteryDelegate() {
  if (Shell::Get()->peripheral_battery_listener())
    battery_observation_.Observe(Shell::Get()->peripheral_battery_listener());
}

StylusBatteryDelegate::~StylusBatteryDelegate() = default;

SkColor StylusBatteryDelegate::GetColorForBatteryLevel() const {
  if (battery_level_ <= kStylusLowBatteryThreshold) {
    return AshColorProvider::Get()->GetContentLayerColor(
        AshColorProvider::ContentLayerType::kIconColorAlert);
  }
  return AshColorProvider::Get()->GetContentLayerColor(
      AshColorProvider::ContentLayerType::kIconColorPrimary);
}

gfx::ImageSkia StylusBatteryDelegate::GetBatteryImage() const {
  PowerStatus::BatteryImageInfo info;
  info.charge_percent = battery_level_.value_or(0);

  const SkColor icon_fg_color = GetColorForBatteryLevel();
  const SkColor icon_bg_color = AshColorProvider::Get()->GetBackgroundColor();

  return PowerStatus::GetBatteryImage(info, kUnifiedTrayIconSize, icon_bg_color,
                                      icon_fg_color);
}

bool StylusBatteryDelegate::IsBatteryLevelLow() const {
  return battery_level_ <= kStylusLowBatteryThreshold;
}

void StylusBatteryDelegate::OnAddingBattery(
    const PeripheralBatteryListener::BatteryInfo& battery) {
  battery_level_ = battery.level;
}

void StylusBatteryDelegate::OnRemovingBattery(
    const PeripheralBatteryListener::BatteryInfo& battery) {}

void StylusBatteryDelegate::OnUpdatedBatteryLevel(
    const PeripheralBatteryListener::BatteryInfo& battery) {
  battery_level_ = battery.level;
}

}  // namespace ash
