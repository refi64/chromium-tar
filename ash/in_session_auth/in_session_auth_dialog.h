// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_IN_SESSION_AUTH_IN_SESSION_AUTH_DIALOG_H_
#define ASH_IN_SESSION_AUTH_IN_SESSION_AUTH_DIALOG_H_

#include <memory>

#include "ash/ash_export.h"

namespace views {
class Widget;
}

namespace ash {

// InSessionAuthDialog gets instantiated on every request to show
// an authentication dialog, and gets destroyed when the request is
// completed.
class InSessionAuthDialog {
 public:
  explicit InSessionAuthDialog(uint32_t auth_methods);
  InSessionAuthDialog(const InSessionAuthDialog&) = delete;
  InSessionAuthDialog& operator=(const InSessionAuthDialog&) = delete;
  ~InSessionAuthDialog();

  views::Widget* widget() { return widget_.get(); }

  bool is_shown() const { return !!widget_; }

 private:
  // The dialog widget. Owned by this class so that we can close the widget
  // when auth completes.
  std::unique_ptr<views::Widget> widget_;
};

}  // namespace ash

#endif  // ASH_IN_SESSION_AUTH_IN_SESSION_AUTH_DIALOG_H_
