// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/arc_app_window_info.h"

#include "ash/public/cpp/window_properties.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"

namespace {

// Prefix in intent that specifies a logical window. Among a group of windows
// belonging to the same logical window, only one will be represented in the
// shelf and in the alt-tab menu. S. means string type.
constexpr char kLogicalWindowIntentPrefix[] =
    "S.org.chromium.arc.logical_window_id=";

constexpr size_t kMaxIconPngSize = 64 * 1024;  // 64 kb

std::string GetLogicalWindowIdFromIntent(const std::string& launch_intent) {
  arc::Intent intent;
  if (!arc::ParseIntent(launch_intent, &intent))
    return std::string();
  const std::string prefix(kLogicalWindowIntentPrefix);
  for (const auto& param : intent.extra_params()) {
    if (base::StartsWith(param, prefix, base::CompareCase::SENSITIVE))
      return param.substr(prefix.length());
  }
  return std::string();
}

}  // namespace

ArcAppWindowInfo::ArcAppWindowInfo(const arc::ArcAppShelfId& app_shelf_id,
                                   const std::string& launch_intent,
                                   const std::string& package_name)
    : app_shelf_id_(app_shelf_id),
      launch_intent_(launch_intent),
      package_name_(package_name),
      logical_window_id_(GetLogicalWindowIdFromIntent(launch_intent)) {}

ArcAppWindowInfo::~ArcAppWindowInfo() = default;

void ArcAppWindowInfo::SetDescription(
    const std::string& title,
    const std::vector<uint8_t>& icon_data_png) {
  DCHECK(base::IsStringUTF8(title));
  title_ = title;

  // Chrome has custom Play Store icon. Don't overwrite it.
  if (app_shelf_id_.app_id() == arc::kPlayStoreAppId)
    return;
  if (icon_data_png.size() < kMaxIconPngSize)
    icon_data_png_ = icon_data_png;
  else
    VLOG(1) << "Task icon size is too big " << icon_data_png.size() << ".";
}

void ArcAppWindowInfo::set_hidden_from_shelf(bool hidden) {
  if (hidden_from_shelf_ != hidden) {
    hidden_from_shelf_ = hidden;
    UpdateWindowProperties();
  }
}

void ArcAppWindowInfo::UpdateWindowProperties() {
  aura::Window* const win = window();
  if (!win)
    return;
  win->SetProperty(ash::kHideInDeskMiniViewKey, hidden_from_shelf_);
  win->SetProperty(ash::kHideInOverviewKey, hidden_from_shelf_);
  win->SetProperty(ash::kHideInShelfKey, hidden_from_shelf_);
}

void ArcAppWindowInfo::set_window(aura::Window* window) {
  window_ = window;
  UpdateWindowProperties();
}

aura::Window* ArcAppWindowInfo::ArcAppWindowInfo::window() {
  return window_;
}

const arc::ArcAppShelfId& ArcAppWindowInfo::app_shelf_id() const {
  return app_shelf_id_;
}

const ash::ShelfID ArcAppWindowInfo::shelf_id() const {
  return ash::ShelfID(app_shelf_id_.ToString());
}

const std::string& ArcAppWindowInfo::launch_intent() const {
  return launch_intent_;
}

const std::string& ArcAppWindowInfo::package_name() const {
  return package_name_;
}

const std::string& ArcAppWindowInfo::title() const {
  return title_;
}

const std::vector<uint8_t>& ArcAppWindowInfo::icon_data_png() const {
  return icon_data_png_;
}

const std::string& ArcAppWindowInfo::logical_window_id() const {
  return logical_window_id_;
}