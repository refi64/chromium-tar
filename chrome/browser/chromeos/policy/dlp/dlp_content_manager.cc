// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/dlp/dlp_content_manager.h"

#include "ash/public/cpp/privacy_screen_dlp_helper.h"
#include "base/bind.h"
#include "base/stl_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/ui/ash/chrome_screenshot_grabber.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/window.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/skia_util.h"
#include "url/gurl.h"

namespace policy {

namespace {
// Delay to wait to turn off privacy screen enforcement after confidential data
// becomes not visible. This is done to not blink the privacy screen in case of
// a quick switch from one confidential data to another.
const base::TimeDelta kPrivacyScreenOffDelay =
    base::TimeDelta::FromMilliseconds(500);
}  // namespace

static DlpContentManager* g_dlp_content_manager = nullptr;

// static
DlpContentManager* DlpContentManager::Get() {
  if (!g_dlp_content_manager)
    g_dlp_content_manager = new DlpContentManager();
  return g_dlp_content_manager;
}

DlpContentRestrictionSet DlpContentManager::GetConfidentialRestrictions(
    content::WebContents* web_contents) const {
  if (!base::Contains(confidential_web_contents_, web_contents))
    return DlpContentRestrictionSet();
  return confidential_web_contents_.at(web_contents);
}

DlpContentRestrictionSet DlpContentManager::GetOnScreenPresentRestrictions()
    const {
  return on_screen_restrictions_;
}

bool DlpContentManager::IsScreenshotRestricted(
    const ScreenshotArea& area) const {
  // Fullscreen - restricted if any confidential data is visible.
  if (area.type == ScreenshotType::kAllRootWindows) {
    return GetOnScreenPresentRestrictions().HasRestriction(
        DlpContentRestriction::kScreenshot);
  }

  // Window - restricted if the window contains confidential data.
  if (area.type == ScreenshotType::kWindow) {
    DCHECK(area.window);
    for (auto& entry : confidential_web_contents_) {
      aura::Window* web_contents_window = entry.first->GetNativeView();
      if (entry.second.HasRestriction(DlpContentRestriction::kScreenshot) &&
          area.window->Contains(web_contents_window)) {
        return true;
      }
    }
    return false;
  }

  DCHECK_EQ(area.type, ScreenshotType::kPartialWindow);
  DCHECK(area.rect);
  DCHECK(area.window);
  // Partial - restricted if any visible confidential WebContents intersects
  // with the area.
  for (auto& entry : confidential_web_contents_) {
    if (entry.first->GetVisibility() != content::Visibility::VISIBLE ||
        !entry.second.HasRestriction(DlpContentRestriction::kScreenshot)) {
      continue;
    }
    aura::Window* web_contents_window = entry.first->GetNativeView();
    aura::Window* root_window = web_contents_window->GetRootWindow();
    // If no root window, then the WebContent shouldn't be visible.
    if (!root_window)
      continue;
    // Not allowing if the area intersects with confidential WebContents,
    // but the intersection doesn't belong to occluded area.
    gfx::Rect intersection(*area.rect);
    aura::Window::ConvertRectToTarget(area.window, root_window, &intersection);
    intersection.Intersect(web_contents_window->GetBoundsInRootWindow());
    if (!intersection.IsEmpty() &&
        !web_contents_window->occluded_region_in_root().contains(
            gfx::RectToSkIRect(intersection))) {
      return true;
    }
  }

  return false;
}

/* static */
void DlpContentManager::SetDlpContentManagerForTesting(
    DlpContentManager* dlp_content_manager) {
  if (g_dlp_content_manager)
    delete g_dlp_content_manager;
  g_dlp_content_manager = dlp_content_manager;
}

/* static */
void DlpContentManager::ResetDlpContentManagerForTesting() {
  g_dlp_content_manager = nullptr;
}

DlpContentManager::DlpContentManager() = default;

DlpContentManager::~DlpContentManager() = default;

void DlpContentManager::OnConfidentialityChanged(
    content::WebContents* web_contents,
    const DlpContentRestrictionSet& restriction_set) {
  if (restriction_set.IsEmpty()) {
    RemoveFromConfidential(web_contents);
  } else {
    confidential_web_contents_[web_contents] = restriction_set;
    if (web_contents->GetVisibility() == content::Visibility::VISIBLE) {
      MaybeChangeOnScreenRestrictions();
    }
  }
}

void DlpContentManager::OnWebContentsDestroyed(
    content::WebContents* web_contents) {
  RemoveFromConfidential(web_contents);
}

DlpContentRestrictionSet DlpContentManager::GetRestrictionSetForURL(
    const GURL& url) const {
  DlpContentRestrictionSet set;
  // TODO(crbug/1109783): Implement based on the policy.
  return set;
}

void DlpContentManager::OnVisibilityChanged(
    content::WebContents* web_contents) {
  MaybeChangeOnScreenRestrictions();
}

void DlpContentManager::RemoveFromConfidential(
    content::WebContents* web_contents) {
  confidential_web_contents_.erase(web_contents);
  MaybeChangeOnScreenRestrictions();
}

void DlpContentManager::MaybeChangeOnScreenRestrictions() {
  DlpContentRestrictionSet new_restriction_set;
  // TODO(crbug/1111860): Recalculate more effectively.
  for (const auto& entry : confidential_web_contents_) {
    if (entry.first->GetVisibility() == content::Visibility::VISIBLE) {
      new_restriction_set.UnionWith(entry.second);
    }
  }
  if (on_screen_restrictions_ != new_restriction_set) {
    DlpContentRestrictionSet added_restrictions =
        new_restriction_set.DifferenceWith(on_screen_restrictions_);
    DlpContentRestrictionSet removed_restrictions =
        on_screen_restrictions_.DifferenceWith(new_restriction_set);
    on_screen_restrictions_ = new_restriction_set;
    OnScreenRestrictionsChanged(added_restrictions, removed_restrictions);
  }
}

void DlpContentManager::OnScreenRestrictionsChanged(
    const DlpContentRestrictionSet& added_restrictions,
    const DlpContentRestrictionSet& removed_restrictions) const {
  DCHECK(!(added_restrictions.HasRestriction(
               DlpContentRestriction::kPrivacyScreen) &&
           removed_restrictions.HasRestriction(
               DlpContentRestriction::kPrivacyScreen)));
  if (added_restrictions.HasRestriction(
          DlpContentRestriction::kPrivacyScreen)) {
    ash::PrivacyScreenDlpHelper::Get()->SetEnforced(true);
  }

  if (removed_restrictions.HasRestriction(
          DlpContentRestriction::kPrivacyScreen)) {
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&DlpContentManager::MaybeRemovePrivacyScreenEnforcement,
                       base::Unretained(this)),
        kPrivacyScreenOffDelay);
  }
}

void DlpContentManager::MaybeRemovePrivacyScreenEnforcement() const {
  if (!GetOnScreenPresentRestrictions().HasRestriction(
          DlpContentRestriction::kPrivacyScreen)) {
    ash::PrivacyScreenDlpHelper::Get()->SetEnforced(false);
  }
}

// static
base::TimeDelta DlpContentManager::GetPrivacyScreenOffDelayForTesting() {
  return kPrivacyScreenOffDelay;
}

}  // namespace policy
