// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "chrome/browser/nearby_sharing/certificates/fake_nearby_share_certificate_manager.h"
#include "chrome/browser/nearby_sharing/certificates/test_util.h"

FakeNearbyShareCertificateManager::Factory::Factory() = default;

FakeNearbyShareCertificateManager::Factory::~Factory() = default;

std::unique_ptr<NearbyShareCertificateManager>
FakeNearbyShareCertificateManager::Factory::CreateInstance(
    NearbyShareLocalDeviceDataManager* local_device_data_manager,
    PrefService* pref_service,
    leveldb_proto::ProtoDatabaseProvider* proto_database_provider,
    const base::FilePath& profile_path,
    NearbyShareClientFactory* client_factory,
    base::Clock* clock) {
  auto instance = std::make_unique<FakeNearbyShareCertificateManager>();
  instances_.push_back(instance.get());

  return instance;
}

FakeNearbyShareCertificateManager::GetDecryptedPublicCertificateCall::
    GetDecryptedPublicCertificateCall(
        NearbyShareEncryptedMetadataKey encrypted_metadata_key,
        CertDecryptedCallback callback)
    : encrypted_metadata_key(std::move(encrypted_metadata_key)),
      callback(std::move(callback)) {}

FakeNearbyShareCertificateManager::GetDecryptedPublicCertificateCall::
    GetDecryptedPublicCertificateCall(
        GetDecryptedPublicCertificateCall&& other) = default;

FakeNearbyShareCertificateManager::GetDecryptedPublicCertificateCall&
FakeNearbyShareCertificateManager::GetDecryptedPublicCertificateCall::operator=(
    GetDecryptedPublicCertificateCall&& other) = default;

FakeNearbyShareCertificateManager::GetDecryptedPublicCertificateCall::
    ~GetDecryptedPublicCertificateCall() = default;

FakeNearbyShareCertificateManager::FakeNearbyShareCertificateManager() =
    default;

FakeNearbyShareCertificateManager::~FakeNearbyShareCertificateManager() =
    default;

NearbySharePrivateCertificate
FakeNearbyShareCertificateManager::GetValidPrivateCertificate(
    NearbyShareVisibility visibility) {
  ++num_get_valid_private_certificate_calls_;
  return GetNearbyShareTestPrivateCertificate(visibility);
}

std::vector<nearbyshare::proto::PublicCertificate>
FakeNearbyShareCertificateManager::GetPrivateCertificatesAsPublicCertificates(
    NearbyShareVisibility visibility) {
  ++num_get_private_certificates_as_public_certificates_calls_;
  return GetNearbyShareTestPublicCertificateList(visibility);
}

void FakeNearbyShareCertificateManager::GetDecryptedPublicCertificate(
    NearbyShareEncryptedMetadataKey encrypted_metadata_key,
    CertDecryptedCallback callback) {
  get_decrypted_public_certificate_calls_.emplace_back(encrypted_metadata_key,
                                                       std::move(callback));
}

void FakeNearbyShareCertificateManager::DownloadPublicCertificates() {
  ++num_download_public_certificates_calls_;
}

void FakeNearbyShareCertificateManager::OnStart() {}

void FakeNearbyShareCertificateManager::OnStop() {}
