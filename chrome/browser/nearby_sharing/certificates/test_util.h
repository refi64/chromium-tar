// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NEARBY_SHARING_CERTIFICATES_TEST_UTIL_H_
#define CHROME_BROWSER_NEARBY_SHARING_CERTIFICATES_TEST_UTIL_H_

#include <memory>
#include <vector>

#include "base/time/time.h"
#include "chrome/browser/nearby_sharing/certificates/nearby_share_encrypted_metadata_key.h"
#include "chrome/browser/nearby_sharing/certificates/nearby_share_private_certificate.h"
#include "chrome/browser/nearby_sharing/proto/encrypted_metadata.pb.h"
#include "chrome/browser/nearby_sharing/proto/rpc_resources.pb.h"
#include "crypto/ec_private_key.h"
#include "crypto/symmetric_key.h"

std::unique_ptr<crypto::ECPrivateKey> GetNearbyShareTestP256KeyPair();
const std::vector<uint8_t>& GetNearbyShareTestP256PublicKey();

std::unique_ptr<crypto::SymmetricKey> GetNearbyShareTestSecretKey();
const std::vector<uint8_t>& GetNearbyShareTestCertificateId();

const std::vector<uint8_t>& GetNearbyShareTestMetadataEncryptionKey();
const std::vector<uint8_t>& GetNearbyShareTestMetadataEncryptionKeyTag();
const std::vector<uint8_t>& GetNearbyShareTestSalt();
const NearbyShareEncryptedMetadataKey& GetNearbyShareTestEncryptedMetadataKey();

base::Time GetNearbyShareTestNotBefore();
base::TimeDelta GetNearbyShareTestValidityOffset();

const nearbyshare::proto::EncryptedMetadata& GetNearbyShareTestMetadata();
const std::vector<uint8_t>& GetNearbyShareTestEncryptedMetadata();

const std::vector<uint8_t>& GetNearbyShareTestPayloadToSign();
const std::vector<uint8_t>& GetNearbyShareTestSampleSignature();

NearbySharePrivateCertificate GetNearbyShareTestPrivateCertificate();
const nearbyshare::proto::PublicCertificate&
GetNearbyShareTestPublicCertificate();

#endif  // CHROME_BROWSER_NEARBY_SHARING_CERTIFICATES_TEST_UTIL_H_