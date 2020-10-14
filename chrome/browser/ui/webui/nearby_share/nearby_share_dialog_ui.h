// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_NEARBY_SHARE_NEARBY_SHARE_DIALOG_UI_H_
#define CHROME_BROWSER_UI_WEBUI_NEARBY_SHARE_NEARBY_SHARE_DIALOG_UI_H_

#include "chrome/browser/ui/webui/nearby_share/nearby_share.mojom.h"
#include "chrome/browser/ui/webui/nearby_share/public/mojom/nearby_share_settings.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class NearbySharingService;

namespace nearby_share {

// The WebUI controller for chrome://nearby.
class NearbyShareDialogUI : public ui::MojoWebUIController {
 public:
  explicit NearbyShareDialogUI(content::WebUI* web_ui);
  NearbyShareDialogUI(const NearbyShareDialogUI&) = delete;
  NearbyShareDialogUI& operator=(const NearbyShareDialogUI&) = delete;
  ~NearbyShareDialogUI() override;

  // Instantiates the implementor of the mojom::DiscoveryManager mojo
  // interface passing the pending receiver that will be internally bound.
  void BindInterface(mojo::PendingReceiver<mojom::DiscoveryManager> manager);
  // ui::MojoWebUIController
  void BindInterface(
      mojo::PendingReceiver<mojom::NearbyShareSettings> receiver);

 private:
  NearbySharingService* nearby_service_;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace nearby_share

#endif  // CHROME_BROWSER_UI_WEBUI_NEARBY_SHARE_NEARBY_SHARE_DIALOG_UI_H_
