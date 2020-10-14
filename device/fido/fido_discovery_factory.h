// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_FIDO_FIDO_DISCOVERY_FACTORY_H_
#define DEVICE_FIDO_FIDO_DISCOVERY_FACTORY_H_

#include <memory>
#include <vector>

#include "base/component_export.h"
#include "base/optional.h"
#include "build/build_config.h"
#include "device/fido/cable/cable_discovery_data.h"
#include "device/fido/fido_device_discovery.h"
#include "device/fido/fido_discovery_base.h"
#include "device/fido/fido_request_handler_base.h"
#include "device/fido/fido_transport_protocol.h"
#include "device/fido/hid/fido_hid_discovery.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/device/public/mojom/usb_manager.mojom.h"
#include "services/network/public/mojom/network_context.mojom-forward.h"

#if defined(OS_MAC)
#include "device/fido/mac/authenticator_config.h"
#endif  // defined(OS_MAC)

namespace device {

#if defined(OS_WIN)
class WinWebAuthnApi;
#endif  // defined(OS_WIN)

// FidoDiscoveryFactory offers methods to construct instances of
// FidoDiscoveryBase for a given |transport| protocol.
class COMPONENT_EXPORT(DEVICE_FIDO) FidoDiscoveryFactory {
 public:
  FidoDiscoveryFactory();
  virtual ~FidoDiscoveryFactory();

  // Instantiates a FidoDiscoveryBase for the given transport.
  //
  // FidoTransportProtocol::kUsbHumanInterfaceDevice is not valid on Android.
  virtual std::unique_ptr<FidoDiscoveryBase> Create(
      FidoTransportProtocol transport);

  // Returns whether the current instance is an override injected by the
  // WebAuthn testing API.
  virtual bool IsTestOverride();

  // set_cable_data configures caBLE obtained via a WebAuthn extension.
  void set_cable_data(std::vector<CableDiscoveryData> cable_data,
                      base::Optional<QRGeneratorKey> qr_generator_key);

  void set_usb_device_manager(mojo::Remote<device::mojom::UsbDeviceManager>);

  void set_network_context(network::mojom::NetworkContext*);

  // set_cable_pairing_callback installs a repeating callback that will be
  // called when a QR handshake results in a phone wishing to pair with this
  // browser.
  void set_cable_pairing_callback(
      base::RepeatingCallback<void(std::unique_ptr<CableDiscoveryData>)>);

  void set_hid_ignore_list(base::flat_set<VidPid> hid_ignore_list);

#if defined(OS_MAC)
  // Configures the Touch ID authenticator. Set to base::nullopt to disable it.
  void set_mac_touch_id_info(
      base::Optional<fido::mac::AuthenticatorConfig> mac_touch_id_config) {
    mac_touch_id_config_ = std::move(mac_touch_id_config);
  }
#endif  // defined(OS_MAC)

#if defined(OS_WIN)
  // Instantiates a FidoDiscovery for the native Windows WebAuthn API where
  // available. Returns nullptr otherwise.
  std::unique_ptr<FidoDiscoveryBase> MaybeCreateWinWebAuthnApiDiscovery();

  // Sets the WinWebAuthnApi instance to be used for creating the discovery for
  // the Windows authenticator. If none is set,
  // MaybeCreateWinWebAuthnApiDiscovery() returns nullptr.
  void set_win_webauthn_api(WinWebAuthnApi* api);
  WinWebAuthnApi* win_webauthn_api() const;
#endif  // defined(OS_WIN)

 private:
#if defined(OS_MAC) || defined(OS_CHROMEOS)
  std::unique_ptr<FidoDiscoveryBase> MaybeCreatePlatformDiscovery() const;
#endif

#if defined(OS_MAC)
  base::Optional<fido::mac::AuthenticatorConfig> mac_touch_id_config_;
#endif  // defined(OS_MAC)
  base::Optional<mojo::Remote<device::mojom::UsbDeviceManager>>
      usb_device_manager_;
  network::mojom::NetworkContext* network_context_ = nullptr;
  base::Optional<std::vector<CableDiscoveryData>> cable_data_;
  base::Optional<QRGeneratorKey> qr_generator_key_;
  base::Optional<
      base::RepeatingCallback<void(std::unique_ptr<CableDiscoveryData>)>>
      cable_pairing_callback_;
#if defined(OS_WIN)
  WinWebAuthnApi* win_webauthn_api_ = nullptr;
#endif  // defined(OS_WIN)
  base::flat_set<VidPid> hid_ignore_list_;
};

}  // namespace device

#endif  // DEVICE_FIDO_FIDO_DISCOVERY_FACTORY_H_
