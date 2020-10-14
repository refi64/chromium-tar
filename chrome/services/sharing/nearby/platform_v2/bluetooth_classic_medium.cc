// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/services/sharing/nearby/platform_v2/bluetooth_classic_medium.h"

#include "chrome/services/sharing/nearby/platform_v2/bluetooth_server_socket.h"
#include "chrome/services/sharing/nearby/platform_v2/bluetooth_socket.h"
#include "device/bluetooth/public/cpp/bluetooth_uuid.h"

namespace location {
namespace nearby {
namespace chrome {

BluetoothClassicMedium::BluetoothClassicMedium(
    bluetooth::mojom::Adapter* adapter)
    : adapter_(adapter) {}

BluetoothClassicMedium::~BluetoothClassicMedium() = default;

bool BluetoothClassicMedium::StartDiscovery(
    DiscoveryCallback discovery_callback) {
  if (adapter_client_.is_bound() && discovery_callback_ &&
      discovery_session_.is_bound()) {
    return true;
  }

  // TODO(hansberry): Verify with Nearby team if this is correct behavior.
  discovered_bluetooth_devices_map_.clear();

  bool success =
      adapter_->SetClient(adapter_client_.BindNewPipeAndPassRemote());
  if (!success) {
    adapter_client_.reset();
    return false;
  }

  mojo::PendingRemote<bluetooth::mojom::DiscoverySession> discovery_session;
  success = adapter_->StartDiscoverySession(&discovery_session);

  if (!success || !discovery_session.is_valid()) {
    adapter_client_.reset();
    return false;
  }

  discovery_session_.Bind(std::move(discovery_session));
  discovery_session_.set_disconnect_handler(
      base::BindOnce(&BluetoothClassicMedium::DiscoveringChanged,
                     base::Unretained(this), /*discovering=*/false));

  discovery_callback_ = std::move(discovery_callback);
  return true;
}

bool BluetoothClassicMedium::StopDiscovery() {
  // TODO(hansberry): Verify with Nearby team if this is correct behavior:
  // Do not clear |discovered_bluetooth_devices_map_| because the caller still
  // needs references to BluetoothDevices to remain valid.

  bool stop_discovery_success = true;
  if (discovery_session_) {
    bool message_success = discovery_session_->Stop(&stop_discovery_success);
    stop_discovery_success = stop_discovery_success && message_success;
  }

  adapter_client_.reset();
  discovery_callback_.reset();
  discovery_session_.reset();

  return stop_discovery_success;
}

std::unique_ptr<api::BluetoothSocket> BluetoothClassicMedium::ConnectToService(
    api::BluetoothDevice& remote_device,
    const std::string& service_uuid) {
  // TODO(hansberry): This currently assumes that the device was discovered via
  // Bluetooth Classic (the remote device is in high visibility mode), meaning
  // this address is the expected permanent BT MAC address. Once an
  // implementation is in place to scan for devices over BLE, a new mechanism
  // to query for the remote device's permanent BT MAC address from stored
  // certificates will be needed.
  // We provided this |remote_device|, so we can safely downcast it.
  const std::string& address =
      static_cast<chrome::BluetoothDevice&>(remote_device).GetAddress();

  bluetooth::mojom::ConnectToServiceResultPtr result;
  bool success = adapter_->ConnectToServiceInsecurely(
      address, device::BluetoothUUID(service_uuid), &result);

  if (success && result) {
    return std::make_unique<chrome::BluetoothSocket>(
        remote_device, std::move(result->socket),
        std::move(result->receive_stream), std::move(result->send_stream));
  }

  return nullptr;
}

std::unique_ptr<api::BluetoothServerSocket>
BluetoothClassicMedium::ListenForService(const std::string& service_name,
                                         const std::string& service_uuid) {
  mojo::PendingRemote<bluetooth::mojom::ServerSocket> server_socket;
  bool success = adapter_->CreateRfcommService(
      service_name, device::BluetoothUUID(service_uuid), &server_socket);

  if (success && server_socket) {
    return std::make_unique<chrome::BluetoothServerSocket>(
        std::move(server_socket));
  }

  return nullptr;
}

void BluetoothClassicMedium::PresentChanged(bool present) {
  // TODO(hansberry): It is unclear to me how the API implementation can signal
  // to Core that |present| has become unexpectedly false. Need to ask
  // Nearby team.
  if (!present)
    StopDiscovery();
}

void BluetoothClassicMedium::PoweredChanged(bool powered) {
  // TODO(hansberry): It is unclear to me how the API implementation can signal
  // to Core that |powered| has become unexpectedly false. Need to ask
  // Nearby team.
  if (!powered)
    StopDiscovery();
}

void BluetoothClassicMedium::DiscoverableChanged(bool discoverable) {
  // Do nothing. BluetoothClassicMedium is not responsible for managing
  // discoverable state.
}

void BluetoothClassicMedium::DiscoveringChanged(bool discovering) {
  // TODO(hansberry): It is unclear to me how the API implementation can signal
  // to Core that |discovering| has become unexpectedly false. Need to ask
  // Nearby team.
  if (!discovering)
    StopDiscovery();
}

void BluetoothClassicMedium::DeviceAdded(
    bluetooth::mojom::DeviceInfoPtr device) {
  if (!adapter_client_.is_bound() || !discovery_callback_ ||
      !discovery_session_.is_bound()) {
    return;
  }

  const std::string& address = device->address;
  if (base::Contains(discovered_bluetooth_devices_map_, address)) {
    auto& bluetooth_device = discovered_bluetooth_devices_map_.at(address);
    bluetooth_device.UpdateDeviceInfo(std::move(device));
    discovery_callback_->device_name_changed_cb(bluetooth_device);
  } else {
    discovered_bluetooth_devices_map_.emplace(address, std::move(device));
    discovery_callback_->device_discovered_cb(
        discovered_bluetooth_devices_map_.at(address));
  }
}

void BluetoothClassicMedium::DeviceChanged(
    bluetooth::mojom::DeviceInfoPtr device) {
  DeviceAdded(std::move(device));
}

void BluetoothClassicMedium::DeviceRemoved(
    bluetooth::mojom::DeviceInfoPtr device) {
  if (!adapter_client_.is_bound() || !discovery_callback_ ||
      !discovery_session_.is_bound()) {
    return;
  }

  const std::string& address = device->address;
  if (!base::Contains(discovered_bluetooth_devices_map_, address))
    return;

  discovery_callback_->device_lost_cb(
      discovered_bluetooth_devices_map_.at(address));
  discovered_bluetooth_devices_map_.erase(address);
}

}  // namespace chrome
}  // namespace nearby
}  // namespace location
