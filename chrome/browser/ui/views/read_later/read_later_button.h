// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_READ_LATER_READ_LATER_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_READ_LATER_READ_LATER_BUTTON_H_

#include "chrome/browser/ui/views/toolbar/toolbar_button.h"

class Browser;
class ReadLaterBubbleView;

namespace views {
class ButtonListener;
}

// Button in the bookmarks bar that provides access to the corresponding
// read later menu.
class ReadLaterButton : public ToolbarButton, public views::ButtonListener {
 public:
  explicit ReadLaterButton(Browser* browser);
  ReadLaterButton(const ReadLaterButton&) = delete;
  ReadLaterButton& operator=(const ReadLaterButton&) = delete;
  ~ReadLaterButton() override;

  // ToolbarButton:
  const char* GetClassName() const override;
  void UpdateIcon() override;

  base::WeakPtr<ReadLaterBubbleView> read_later_bubble_for_testing() {
    return read_later_bubble_;
  }

 private:
  // views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  int GetIconSize() const;

  base::WeakPtr<ReadLaterBubbleView> read_later_bubble_;

  Browser* const browser_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_READ_LATER_READ_LATER_BUTTON_H_
