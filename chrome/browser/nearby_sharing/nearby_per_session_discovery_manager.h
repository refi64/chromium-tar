// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NEARBY_SHARING_NEARBY_PER_SESSION_DISCOVERY_MANAGER_H_
#define CHROME_BROWSER_NEARBY_SHARING_NEARBY_PER_SESSION_DISCOVERY_MANAGER_H_

#include "base/containers/flat_map.h"
#include "base/memory/weak_ptr.h"
#include "base/unguessable_token.h"
#include "chrome/browser/nearby_sharing/nearby_sharing_service.h"
#include "chrome/browser/nearby_sharing/share_target_discovered_callback.h"
#include "chrome/browser/nearby_sharing/transfer_update_callback.h"
#include "chrome/browser/ui/webui/nearby_share/nearby_share.mojom.h"
#include "mojo/public/cpp/bindings/remote.h"

// Handles a single nearby device discovery session. Holds all discovered share
// targets for the user to choose from and provides callbacks for when they are
// discovered or lost. All methods are expected to be called on the UI thread
// and there is one instance per WebUI surface.
class NearbyPerSessionDiscoveryManager
    : public TransferUpdateCallback,
      public ShareTargetDiscoveredCallback,
      public nearby_share::mojom::DiscoveryManager {
 public:
  explicit NearbyPerSessionDiscoveryManager(
      NearbySharingService* nearby_sharing_service);
  ~NearbyPerSessionDiscoveryManager() override;

  // TransferUpdateCallback:
  void OnTransferUpdate(const ShareTarget& share_target,
                        const TransferMetadata& transfer_metadata) override;

  // ShareTargetDiscoveredCallback:
  void OnShareTargetDiscovered(ShareTarget share_target) override;
  void OnShareTargetLost(ShareTarget share_target) override;

  // nearby_share::mojom::DiscoveryManager:
  void StartDiscovery(
      mojo::PendingRemote<nearby_share::mojom::ShareTargetListener> listener,
      StartDiscoveryCallback callback) override;
  void SelectShareTarget(const base::UnguessableToken& share_target_id,
                         SelectShareTargetCallback callback) override;

 private:
  // Called as a result of NearbySharingService::Send() to indicate if the
  // transfer has been initiated successfully. OnTransferUpdate() will be called
  // multiple times as the transfer progresses.
  void OnSend(NearbySharingService::StatusCodes status);

  // Unregisters this class from the NearbySharingService.
  void UnregisterSendSurface();

  NearbySharingService* nearby_sharing_service_;
  mojo::Remote<nearby_share::mojom::ShareTargetListener> share_target_listener_;
  SelectShareTargetCallback select_share_target_callback_;

  // Map of ShareTarget id to discovered ShareTargets.
  base::flat_map<base::UnguessableToken, ShareTarget> discovered_share_targets_;

  base::WeakPtrFactory<NearbyPerSessionDiscoveryManager> weak_ptr_factory_{
      this};
};

#endif  // CHROME_BROWSER_NEARBY_SHARING_NEARBY_PER_SESSION_DISCOVERY_MANAGER_H_
