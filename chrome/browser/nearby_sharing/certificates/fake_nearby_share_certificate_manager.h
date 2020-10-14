// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NEARBY_SHARING_CERTIFICATES_FAKE_NEARBY_SHARE_CERTIFICATE_MANAGER_H_
#define CHROME_BROWSER_NEARBY_SHARING_CERTIFICATES_FAKE_NEARBY_SHARE_CERTIFICATE_MANAGER_H_

#include <memory>
#include <vector>

#include "base/containers/span.h"
#include "base/time/clock.h"
#include "chrome/browser/nearby_sharing/certificates/nearby_share_certificate_manager.h"
#include "chrome/browser/nearby_sharing/certificates/nearby_share_certificate_manager_impl.h"
#include "chrome/browser/nearby_sharing/certificates/nearby_share_decrypted_public_certificate.h"
#include "chrome/browser/nearby_sharing/certificates/nearby_share_encrypted_metadata_key.h"
#include "chrome/browser/nearby_sharing/certificates/nearby_share_private_certificate.h"
#include "chrome/browser/nearby_sharing/proto/rpc_resources.pb.h"

// A fake implementation of NearbyShareCertificateManager, along with a fake
// factory, to be used in tests.
class FakeNearbyShareCertificateManager : public NearbyShareCertificateManager {
 public:
  // Factory that creates FakeNearbyShareCertificateManager instances. Use in
  // NearbyShareCertificateManagerImpl::Factor::SetFactoryForTesting() in unit
  // tests.
  class Factory : public NearbyShareCertificateManagerImpl::Factory {
   public:
    Factory();
    ~Factory() override;

    // Returns all FakeNearbyShareCertificateManager instances created by
    // CreateInstance().
    std::vector<FakeNearbyShareCertificateManager*>& instances() {
      return instances_;
    }

   private:
    // NearbyShareCertificateManagerImpl::Factory:
    std::unique_ptr<NearbyShareCertificateManager> CreateInstance(
        NearbyShareLocalDeviceDataManager* local_device_data_manager,
        PrefService* pref_service,
        leveldb_proto::ProtoDatabaseProvider* proto_database_provider,
        const base::FilePath& profile_path,
        NearbyShareClientFactory* client_factory,
        base::Clock* clock) override;

    std::vector<FakeNearbyShareCertificateManager*> instances_;
  };

  class GetDecryptedPublicCertificateCall {
   public:
    GetDecryptedPublicCertificateCall(
        NearbyShareEncryptedMetadataKey encrypted_metadata_key,
        CertDecryptedCallback callback);
    GetDecryptedPublicCertificateCall(
        GetDecryptedPublicCertificateCall&& other);
    GetDecryptedPublicCertificateCall& operator=(
        GetDecryptedPublicCertificateCall&& other);
    GetDecryptedPublicCertificateCall(
        const GetDecryptedPublicCertificateCall&) = delete;
    GetDecryptedPublicCertificateCall& operator=(
        const GetDecryptedPublicCertificateCall&) = delete;
    ~GetDecryptedPublicCertificateCall();

    NearbyShareEncryptedMetadataKey encrypted_metadata_key;
    CertDecryptedCallback callback;
  };

  FakeNearbyShareCertificateManager();
  ~FakeNearbyShareCertificateManager() override;

  // NearbyShareCertificateManager:
  NearbySharePrivateCertificate GetValidPrivateCertificate(
      NearbyShareVisibility visibility) override;
  std::vector<nearbyshare::proto::PublicCertificate>
  GetPrivateCertificatesAsPublicCertificates(
      NearbyShareVisibility visibility) override;
  void GetDecryptedPublicCertificate(
      NearbyShareEncryptedMetadataKey encrypted_metadata_key,
      CertDecryptedCallback callback) override;
  void DownloadPublicCertificates() override;

  // Make protected methods from base class public in this fake class.
  using NearbyShareCertificateManager::NotifyPrivateCertificatesChanged;
  using NearbyShareCertificateManager::NotifyPublicCertificatesDownloaded;

  size_t num_get_valid_private_certificate_calls() {
    return num_get_valid_private_certificate_calls_;
  }

  size_t num_get_private_certificates_as_public_certificates_calls() {
    return num_get_private_certificates_as_public_certificates_calls_;
  }

  size_t num_download_public_certificates_calls() {
    return num_download_public_certificates_calls_;
  }

  std::vector<GetDecryptedPublicCertificateCall>&
  get_decrypted_public_certificate_calls() {
    return get_decrypted_public_certificate_calls_;
  }

 private:
  // NearbyShareCertificateManager:
  void OnStart() override;
  void OnStop() override;

  size_t num_get_valid_private_certificate_calls_ = 0;
  size_t num_get_private_certificates_as_public_certificates_calls_ = 0;
  size_t num_download_public_certificates_calls_ = 0;
  std::vector<GetDecryptedPublicCertificateCall>
      get_decrypted_public_certificate_calls_;
};

#endif  // CHROME_BROWSER_NEARBY_SHARING_CERTIFICATES_FAKE_NEARBY_SHARE_CERTIFICATE_MANAGER_H_
