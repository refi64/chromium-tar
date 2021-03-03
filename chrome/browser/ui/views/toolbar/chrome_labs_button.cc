// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/toolbar/chrome_labs_button.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/views/toolbar/chrome_labs_bubble_view.h"
#include "chrome/browser/ui/views/toolbar/chrome_labs_bubble_view_model.h"
#include "ui/views/controls/button/button_controller.h"

ChromeLabsButton::ChromeLabsButton()
    : ToolbarButton(base::BindRepeating(&ChromeLabsButton::ButtonPressed,
                                        base::Unretained(this))) {
  button_controller()->set_notify_action(
      views::ButtonController::NotifyAction::kOnPress);
}

ChromeLabsButton::~ChromeLabsButton() = default;

void ChromeLabsButton::UpdateIcon() {
  UpdateIconsWithStandardColors(kChromeLabsIcon);
}

const char* ChromeLabsButton::GetClassName() const {
  return "ChromeLabsButton";
}

void ChromeLabsButton::SetLabInfoForTesting(
    const std::vector<LabInfo>& test_lab_info) {
  test_lab_info_ = test_lab_info;
}

void ChromeLabsButton::ButtonPressed() {
  if (ChromeLabsBubbleView::IsShowing()) {
    ChromeLabsBubbleView::Hide();
    return;
  }
  std::unique_ptr<ChromeLabsBubbleViewModel> model =
      std::make_unique<ChromeLabsBubbleViewModel>(test_lab_info_);
  ChromeLabsBubbleView::Show(this, std::move(model));
}
