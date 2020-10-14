// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/read_later/read_later_button.h"

#include "base/strings/string16.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/read_later/read_later_bubble_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/pointer/touch_ui_controller.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/button_controller.h"

ReadLaterButton::ReadLaterButton(Browser* browser)
    : ToolbarButton(this), browser_(browser) {
  SetTooltipText(l10n_util::GetStringUTF16(IDS_TOOLTIP_READ_LATER_BUTTON));
  GetViewAccessibility().OverrideHasPopup(ax::mojom::HasPopup::kMenu);
  button_controller()->set_notify_action(
      views::ButtonController::NotifyAction::kOnPress);
}

ReadLaterButton::~ReadLaterButton() = default;

const char* ReadLaterButton::GetClassName() const {
  return "ReadLaterButton";
}

void ReadLaterButton::UpdateIcon() {
  // TODO(corising): Update this to the correct icon once it is added.
  SetImageModel(views::Button::STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(
                    vector_icons::kFolderIcon,
                    GetThemeProvider()->GetColor(
                        ThemeProperties::COLOR_TOOLBAR_BUTTON_ICON),
                    GetIconSize()));
}

void ReadLaterButton::ButtonPressed(views::Button* sender,
                                    const ui::Event& event) {
  if (read_later_bubble_) {
    read_later_bubble_->GetWidget()->Close();

  } else {
    read_later_bubble_ = ReadLaterBubbleView::Show(browser_, this);
  }
}

int ReadLaterButton::GetIconSize() const {
  const bool touch_ui = ui::TouchUiController::Get()->touch_ui();
  return (touch_ui && !browser_->app_controller()) ? kDefaultTouchableIconSize
                                                   : kDefaultIconSize;
}
