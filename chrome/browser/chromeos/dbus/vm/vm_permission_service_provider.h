// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DBUS_VM_VM_PERMISSION_SERVICE_PROVIDER_H_
#define CHROME_BROWSER_CHROMEOS_DBUS_VM_VM_PERMISSION_SERVICE_PROVIDER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "base/containers/flat_map.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "chromeos/dbus/services/cros_dbus_service.h"
#include "dbus/exported_object.h"

namespace dbus {
class MethodCall;
}  // namespace dbus

namespace chromeos {

// This class exports D-Bus methods for querying VM permissions and
// registering and unregistering VMs with the permission service.
//
// RegisterVm:
// % dbus-send --system --type=method_call --print-reply
//     --dest=org.chromium.VmPermissionService
//     /org/chromium/VmPermissionService
//     org.chromium.VmPermissionServiceInterface.RegisterVm
//     array:byte:0x0a,0x0a,0x50,0x76,0x6d,0x44,0x65,0x66,0x61,0x75,0x6c,0x74,
//     0x12,0x28,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0x61,0x62,
//     0x63,0x64,0x65,0x66,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,
//     0x61,0x62,0x63,0x64,0x65,0x66,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,
//     0x18,0x01
//
//     (name: "PvmDefault",
//      owner_id: "1234567890abcdef1234567890abcdef12345678"
//      type: PLUGIN_VM)
//
// % (returns message RegisterVmResponse {
//  string token = 1; // Access token for GetPermissions call
//
// UnregisterVm:
// % dbus-send --system --type=method_call --print-reply
//     --dest=org.chromium.VmPermissionService
//     /org/chromium/VmPermissionService
//     org.chromium.VmPermissionServiceInterface.UnegisterVm
//     array:byte:0x0a,0x0a,0x50,0x76,0x6d,0x44,0x65,0x66,0x61,0x75,0x6c,0x74,
//     0x12,0x28,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0x61,0x62,
//     0x63,0x64,0x65,0x66,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,
//     0x61,0x62,0x63,0x64,0x65,0x66,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38
//
//     (name: "PvmDefault",
//      owner_id: "1234567890abcdef1234567890abcdef12345678"
//
// % (returns empty message on success)
//
// SetPermissions:
// % dbus-send --system --type=method_call --print-reply
//     --dest=org.chromium.VmPermissionService
//     /org/chromium/VmPermissionService
//     org.chromium.VmPermissionServiceInterface.SetPermissions
//     array:byte:0x0a,0x0a,0x50,0x76,0x6d,0x44,0x65,0x66,0x61,0x75,0x6c,0x74,
//     0x12,0x28,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0x61,0x62,
//     0x63,0x64,0x65,0x66,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,
//     0x61,0x62,0x63,0x64,0x65,0x66,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,
//     0x1a,0x04,0x08,0x01,0x10,0x01
//
//     (name: "PvmDefault"
//      owner_id: "1234567890abcdef1234567890abcdef12345678"
//      permissions {
//        kind: MICROPHONE
//        allowed: true
//      })
//
// % (returns empty message on success)
//
// GetPermissions:
// % dbus-send --system --type=method_call --print-reply
//     --dest=org.chromium.VmPermissionService
//     /org/chromium/VmPermissionService
//     org.chromium.VmPermissionServiceInterface.GetPermissions
//     array:byte:0x0a,0x24,0x36,0x36,0x62,0x32,0x65,0x32,0x37,0x33,0x2d,0x66,
//     0x36,0x63,0x62,0x2d,0x34,0x63,0x37,0x37,0x2d,0x39,0x34,0x30,0x32,0x2d,
//     0x61,0x61,0x34,0x66,0x36,0x62,0x66,0x39,0x37,0x64,0x35,0x33
//
//     (token: "66b2e273-f6cb-4c77-9402-aa4f6bf97d53")
//
// % (returns message GetPermssionsResponse {
//   repeated Permission permissions = 1;  // Current set of permissions
//                                         // for the VM.
class VmPermissionServiceProvider
    : public CrosDBusService::ServiceProviderInterface {
 public:
  VmPermissionServiceProvider();
  ~VmPermissionServiceProvider() override;

  // CrosDBusService::ServiceProviderInterface overrides:
  void Start(scoped_refptr<dbus::ExportedObject> exported_object) override;

 private:
  struct VmInfo {
    enum VmType { CrostiniVm = 0, PluginVm = 1 };
    enum PermissionType { PermissionCamera = 0, PermissionMicrophone = 1 };

    const std::string owner_id_;
    const std::string name_;
    const VmType type_;

    base::flat_map<PermissionType, bool> permissions_;

    VmInfo(std::string owner_id, std::string name, VmType type);
    ~VmInfo();
  };

  using VmMap = std::unordered_map<std::string, std::unique_ptr<VmInfo>>;

  // Called from ExportedObject when GetLicenseDataResponse() is exported as a
  // D-Bus method or failed to be exported.
  void OnExported(const std::string& interface_name,
                  const std::string& method_name,
                  bool success);

  void RegisterVm(dbus::MethodCall* method_call,
                  dbus::ExportedObject::ResponseSender response_sender);

  void UnregisterVm(dbus::MethodCall* method_call,
                    dbus::ExportedObject::ResponseSender response_sender);

  void SetPermissions(dbus::MethodCall* method_call,
                      dbus::ExportedObject::ResponseSender response_sender);

  void GetPermissions(dbus::MethodCall* method_call,
                      dbus::ExportedObject::ResponseSender response_sender);

  void UpdateVmPermissions(VmInfo* vm);
  void UpdatePluginVmPermissions(VmInfo* vm);

  // Returns an iterator to a vm with given |owner_id| and |name|).
  VmMap::iterator FindVm(const std::string& owner_id, const std::string& name);

  // VMs currently registered with the permission service, keyed by their
  // access token.
  VmMap vms_;

  // Keep this last so that all weak pointers will be invalidated at the
  // beginning of destruction.
  base::WeakPtrFactory<VmPermissionServiceProvider> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(VmPermissionServiceProvider);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_DBUS_VM_VM_PERMISSION_SERVICE_PROVIDER_H_