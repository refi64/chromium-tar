// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICES_SHARING_NEARBY_PLATFORM_V2_WEBRTC_H_
#define CHROME_SERVICES_SHARING_NEARBY_PLATFORM_V2_WEBRTC_H_

#include "third_party/nearby/src/cpp/platform_v2/api/webrtc.h"

#include <memory>

#include "chrome/services/sharing/public/mojom/webrtc.mojom.h"
#include "chrome/services/sharing/public/mojom/webrtc_signaling_messenger.mojom.h"
#include "services/network/public/mojom/mdns_responder.mojom.h"
#include "services/network/public/mojom/p2p.mojom.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"
#include "third_party/webrtc/api/peer_connection_interface.h"

namespace sharing {
class IpcPacketSocketFactory;
}  // namespace sharing

namespace location {
namespace nearby {
namespace chrome {

class WebRtcMedium : public api::WebRtcMedium {
 public:
  using PeerConnectionCallback = api::WebRtcMedium::PeerConnectionCallback;

  WebRtcMedium(
      network::mojom::P2PSocketManager* socket_manager,
      network::mojom::MdnsResponder* mdns_responder,
      sharing::mojom::IceConfigFetcher* ice_config_fetcher,
      sharing::mojom::WebRtcSignalingMessenger* webrtc_signaling_messenger);
  ~WebRtcMedium() override;

  // api::WebRtcMedium:
  void CreatePeerConnection(webrtc::PeerConnectionObserver* observer,
                            PeerConnectionCallback callback) override;
  std::unique_ptr<api::WebRtcSignalingMessenger> GetSignalingMessenger(
      absl::string_view self_id) override;

 private:
  void OnIceServersFetched(
      webrtc::PeerConnectionObserver* observer,
      PeerConnectionCallback callback,
      std::vector<sharing::mojom::IceServerPtr> ice_servers);

  network::mojom::P2PSocketManager* p2p_socket_manager_;
  network::mojom::MdnsResponder* mdns_responder_;
  sharing::mojom::IceConfigFetcher* ice_config_fetcher_;
  sharing::mojom::WebRtcSignalingMessenger* webrtc_signaling_messenger_;

  std::unique_ptr<sharing::IpcPacketSocketFactory> socket_factory_;

  base::WeakPtrFactory<WebRtcMedium> weak_ptr_factory_{this};
};

}  // namespace chrome
}  // namespace nearby
}  // namespace location

#endif  // CHROME_SERVICES_SHARING_NEARBY_PLATFORM_V2_WEBRTC_H_
