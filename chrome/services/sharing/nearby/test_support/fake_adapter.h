// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICES_SHARING_NEARBY_TEST_SUPPORT_FAKE_ADAPTER_H_
#define CHROME_SERVICES_SHARING_NEARBY_TEST_SUPPORT_FAKE_ADAPTER_H_

#include "device/bluetooth/public/cpp/bluetooth_uuid.h"
#include "device/bluetooth/public/mojom/adapter.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote_set.h"

namespace bluetooth {

class FakeAdapter : public mojom::Adapter {
 public:
  FakeAdapter();
  FakeAdapter(const FakeAdapter&) = delete;
  FakeAdapter& operator=(const FakeAdapter&) = delete;
  ~FakeAdapter() override;

  // mojom::Adapter
  void ConnectToDevice(const std::string& address,
                       ConnectToDeviceCallback callback) override;
  void GetDevices(GetDevicesCallback callback) override;
  void GetInfo(GetInfoCallback callback) override;
  void SetClient(::mojo::PendingRemote<mojom::AdapterClient> client,
                 SetClientCallback callback) override;
  void SetDiscoverable(bool discoverable,
                       SetDiscoverableCallback callback) override;
  void SetName(const std::string& name, SetNameCallback callback) override;
  void StartDiscoverySession(StartDiscoverySessionCallback callback) override;
  void ConnectToServiceInsecurely(
      const std::string& address,
      const device::BluetoothUUID& service_uuid,
      ConnectToServiceInsecurelyCallback callback) override;
  void CreateRfcommService(const std::string& service_name,
                           const device::BluetoothUUID& service_uuid,
                           CreateRfcommServiceCallback callback) override;

  void SetShouldDiscoverySucceed(bool should_discovery_succeed);
  void SetDiscoverySessionDestroyedCallback(base::OnceClosure callback);
  bool IsDiscoverySessionActive();
  void NotifyDeviceAdded(mojom::DeviceInfoPtr device_info);
  void NotifyDeviceChanged(mojom::DeviceInfoPtr device_info);
  void NotifyDeviceRemoved(mojom::DeviceInfoPtr device_info);
  void AllowConnectionForAddressAndUuidPair(
      const std::string& address,
      const device::BluetoothUUID& service_uuid);
  void AllowIncomingConnectionForServiceNameAndUuidPair(
      const std::string& service_name,
      const device::BluetoothUUID& service_uuid);

  mojo::Receiver<mojom::Adapter> adapter_{this};
  std::string name_ = "AdapterName";
  bool present_ = true;
  bool powered_ = true;
  bool discoverable_ = false;
  bool discovering_ = false;

 private:
  void OnDiscoverySessionDestroyed();

  mojom::DiscoverySession* discovery_session_ = nullptr;
  bool should_discovery_succeed_ = true;
  base::OnceClosure on_discovery_session_destroyed_callback_;
  std::set<std::pair<std::string, device::BluetoothUUID>>
      allowed_connections_for_address_and_uuid_pair_;
  std::set<std::pair<std::string, device::BluetoothUUID>>
      allowed_connections_for_service_name_and_uuid_pair_;

  mojo::Remote<mojom::AdapterClient> client_;
};

}  // namespace bluetooth

#endif  // CHROME_SERVICES_SHARING_NEARBY_TEST_SUPPORT_FAKE_ADAPTER_H_
