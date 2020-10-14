// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_COMPONENTS_SCANNING_SCANNING_UI_H_
#define CHROMEOS_COMPONENTS_SCANNING_SCANNING_UI_H_

#include "ui/webui/mojo_web_ui_controller.h"

namespace content {
class WebUI;
}  // namespace content

namespace chromeos {

// The WebUI for chrome://scanning.
class ScanningUI : public ui::MojoWebUIController {
 public:
  explicit ScanningUI(content::WebUI* web_ui);
  ~ScanningUI() override;

  ScanningUI(const ScanningUI&) = delete;
  ScanningUI& operator=(const ScanningUI&) = delete;
};

}  // namespace chromeos

#endif  // CHROMEOS_COMPONENTS_SCANNING_SCANNING_UI_H_
