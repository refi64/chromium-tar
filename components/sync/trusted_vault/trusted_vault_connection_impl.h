// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_TRUSTED_VAULT_TRUSTED_VAULT_CONNECTION_IMPL_H_
#define COMPONENTS_SYNC_TRUSTED_VAULT_TRUSTED_VAULT_CONNECTION_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "components/sync/trusted_vault/trusted_vault_access_token_fetcher.h"
#include "components/sync/trusted_vault/trusted_vault_connection.h"

namespace network {
class PendingSharedURLLoaderFactory;
}  // namespace network

namespace syncer {

// This class is created on UI thread and used/destroyed on trusted vault
// backend thread.
class TrustedVaultConnectionImpl : public TrustedVaultConnection {
 public:
  TrustedVaultConnectionImpl(
      std::unique_ptr<network::PendingSharedURLLoaderFactory>
          url_loader_factory,
      std::unique_ptr<TrustedVaultAccessTokenFetcher> access_token_fetcher);

  TrustedVaultConnectionImpl(const TrustedVaultConnectionImpl& other) = delete;
  TrustedVaultConnectionImpl& operator=(
      const TrustedVaultConnectionImpl& other) = delete;
  ~TrustedVaultConnectionImpl() override = default;

  void RegisterDevice(const CoreAccountInfo& account_info,
                      const std::vector<uint8_t>& last_trusted_vault_key,
                      int last_trusted_vault_key_version,
                      const SecureBoxPublicKey& device_key_pair,
                      RegisterDeviceCallback callback) override;

  void DownloadKeys(const CoreAccountInfo& account_info,
                    const std::vector<uint8_t>& last_trusted_vault_key,
                    int last_trusted_vault_key_version,
                    std::unique_ptr<SecureBoxKeyPair> device_key_pair,
                    DownloadKeysCallback callback) override;
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_TRUSTED_VAULT_TRUSTED_VAULT_CONNECTION_IMPL_H_
