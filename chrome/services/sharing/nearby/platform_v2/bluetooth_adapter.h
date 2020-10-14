// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICES_SHARING_NEARBY_PLATFORM_V2_BLUETOOTH_ADAPTER_H_
#define CHROME_SERVICES_SHARING_NEARBY_PLATFORM_V2_BLUETOOTH_ADAPTER_H_

#include <string>

#include "device/bluetooth/public/mojom/adapter.mojom.h"
#include "third_party/nearby/src/cpp/platform_v2/api/bluetooth_adapter.h"

namespace location {
namespace nearby {
namespace chrome {

// Concrete BluetoothAdapter implementation.
// api::BluetoothAdapter is a synchronous interface, so this implementation
// consumes the synchronous signatures of bluetooth::mojom::Adapter methods.
class BluetoothAdapter : public api::BluetoothAdapter {
 public:
  explicit BluetoothAdapter(bluetooth::mojom::Adapter* adapter);
  ~BluetoothAdapter() override;

  BluetoothAdapter(const BluetoothAdapter&) = delete;
  BluetoothAdapter& operator=(const BluetoothAdapter&) = delete;

  // api::BluetoothAdapter:
  bool SetStatus(Status status) override;
  bool IsEnabled() const override;
  ScanMode GetScanMode() const override;
  bool SetScanMode(ScanMode scan_mode) override;
  std::string GetName() const override;
  bool SetName(absl::string_view name) override;

 private:
  // This reference is owned by the top-level Nearby Connections interface and
  // will always outlive this object.
  bluetooth::mojom::Adapter* adapter_;
};

}  // namespace chrome
}  // namespace nearby
}  // namespace location

#endif  // CHROME_SERVICES_SHARING_NEARBY_PLATFORM_V2_BLUETOOTH_ADAPTER_H_
