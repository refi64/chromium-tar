// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROME_BROWSER_MAIN_EXTRA_PARTS_LACROS_H_
#define CHROME_BROWSER_CHROME_BROWSER_MAIN_EXTRA_PARTS_LACROS_H_

#include <memory>

#include "chrome/browser/chrome_browser_main_extra_parts.h"

namespace chromeos {
class LacrosChromeServiceImpl;
}

// Startup and shutdown code for lacros-chrome.
class ChromeBrowserMainExtraPartsLacros : public ChromeBrowserMainExtraParts {
 public:
  ChromeBrowserMainExtraPartsLacros();
  ChromeBrowserMainExtraPartsLacros(const ChromeBrowserMainExtraPartsLacros&) =
      delete;
  ChromeBrowserMainExtraPartsLacros& operator=(
      const ChromeBrowserMainExtraPartsLacros&) = delete;
  ~ChromeBrowserMainExtraPartsLacros() override;

 private:
  // ChromeBrowserMainExtraParts:
  void PostCreateThreads() override;

  std::unique_ptr<chromeos::LacrosChromeServiceImpl> lacros_chrome_service_;
};

#endif  // CHROME_BROWSER_CHROME_BROWSER_MAIN_EXTRA_PARTS_LACROS_H_
