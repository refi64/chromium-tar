// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_APP_LIST_VIEWS_PRIVACY_INFO_VIEW_H_
#define ASH_APP_LIST_VIEWS_PRIVACY_INFO_VIEW_H_

#include "ash/app_list/views/search_result_base_view.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/controls/styled_label_listener.h"
#include "ui/views/view.h"

namespace views {
class Button;
class ImageButton;
class ImageView;
class Label;
class StyledLabel;
}  // namespace views

namespace ash {

// View representing privacy info in Launcher.
class PrivacyInfoView : public SearchResultBaseView,
                        public views::StyledLabelListener {
 public:
  ~PrivacyInfoView() override;

  // views::View:
  gfx::Size CalculatePreferredSize() const override;
  int GetHeightForWidth(int width) const override;
  void OnPaintBackground(gfx::Canvas* canvas) override;

  // ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override;
  void OnGestureEvent(ui::GestureEvent* event) override;
  void OnKeyEvent(ui::KeyEvent* event) override;

  // views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  // views::StyledLabelListener:
  void StyledLabelLinkClicked(views::StyledLabel* label,
                              const gfx::Range& range,
                              int event_flags) override;

  // SearchResultBaseView:
  void SelectInitialResultAction(bool reverse_tab_order) override;
  bool SelectNextResultAction(bool reverse_tab_order) override;

 protected:
  PrivacyInfoView(int info_string_id, int link_string_id);

 private:
  enum class Action { kNone, kDefault, kTextLink, kCloseButton };

  void InitLayout();
  void InitInfoIcon();
  void InitText();
  void InitCloseButton();

  virtual void LinkClicked() = 0;
  virtual void CloseButtonPressed() = 0;

  void UpdateLinkStyle();

  views::ImageView* info_icon_ = nullptr;       // Owned by view hierarchy.
  views::StyledLabel* text_view_ = nullptr;     // Owned by view hierarchy.
  views::ImageButton* close_button_ = nullptr;  // Owned by view hierarchy.

  const int info_string_id_;
  const int link_string_id_;
  gfx::Range link_range_;
  views::Label* link_view_;  // Not owned.

  // Indicates which of the privacy notice's actions is selected for keyboard
  // navigation.
  Action selected_action_;

  DISALLOW_COPY_AND_ASSIGN(PrivacyInfoView);
};

}  // namespace ash

#endif  // ASH_APP_LIST_VIEWS_PRIVACY_INFO_VIEW_H_
