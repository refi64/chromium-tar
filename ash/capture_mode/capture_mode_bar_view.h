// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_CAPTURE_MODE_CAPTURE_MODE_BAR_VIEW_H_
#define ASH_CAPTURE_MODE_CAPTURE_MODE_BAR_VIEW_H_

#include "ash/ash_export.h"
#include "ash/capture_mode/capture_mode_types.h"
#include "ui/views/controls/button/button.h"

namespace views {
class Separator;
}  // namespace views

namespace ash {

class CaptureModeCloseButton;
class CaptureModeSourceView;
class CaptureModeTypeView;

// A view that acts as the content view of the capture mode bar widget.
// It has a set of buttons to toggle between image and video capture, and
// another set of buttons to toggle between fullscreen, region, and window
// capture sources. The structure looks like this:
//
//   +--------------------------------------------------------+
//   |  +----------------+  |                       |         |
//   |  |  +---+  +---+  |  |  +---+  +---+  +---+  |  +---+  |
//   |  |  |   |  |   |  |  |  |   |  |   |  |   |  |  |   |  |
//   |  |  +---+  +---+  |  |  +---+  +---+  +---+  |  +---+  |
//   |  +----------------+  |  ^                 ^  |  ^      |
//   +--^----------------------|-----------------|-----|------+
//   ^  |                      +-----------------+     |
//   |  |                      |                       CaptureModeCloseButton
//   |  |                      CaptureModeSourceView
//   |  CaptureModeTypeView
//   |
//   CaptureModeBarView
//
class ASH_EXPORT CaptureModeBarView : public views::View,
                                      public views::ButtonListener {
 public:
  CaptureModeBarView();
  CaptureModeBarView(const CaptureModeBarView&) = delete;
  CaptureModeBarView& operator=(const CaptureModeBarView&) = delete;
  ~CaptureModeBarView() override;

  CaptureModeTypeView* capture_type_view() const { return capture_type_view_; }
  CaptureModeSourceView* capture_source_view() const {
    return capture_source_view_;
  }
  CaptureModeCloseButton* close_button() const { return close_button_; }

  // Gets the ideal bounds of the bar of widget on the given |root| window.
  static gfx::Rect GetBounds(aura::Window* root);

  // Called when either the capture mode source or type changes.
  void OnCaptureSourceChanged(CaptureModeSource new_source);
  void OnCaptureTypeChanged(CaptureModeType new_type);

  // views::View:
  const char* GetClassName() const override;

  // views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

 private:
  // Owned by the views hierarchy.
  CaptureModeTypeView* capture_type_view_;
  views::Separator* separator_1_;
  CaptureModeSourceView* capture_source_view_;
  views::Separator* separator_2_;
  CaptureModeCloseButton* close_button_;
};

}  // namespace ash

#endif  // ASH_CAPTURE_MODE_CAPTURE_MODE_BAR_VIEW_H_
