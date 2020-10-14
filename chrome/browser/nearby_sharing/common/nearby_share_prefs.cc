// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/nearby_sharing/common/nearby_share_prefs.h"

#include <string>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "chrome/browser/nearby_sharing/common/nearby_share_enums.h"
#include "components/prefs/pref_registry.h"
#include "components/prefs/pref_registry_simple.h"

namespace prefs {

const char kNearbySharingActiveProfilePrefName[] =
    "nearby_sharing.active_profile";
const char kNearbySharingAllowedContactsPrefName[] =
    "nearby_sharing.allowed_contacts";
const char kNearbySharingBackgroundVisibilityName[] =
    "nearby_sharing.background_visibility";
const char kNearbySharingDataUsageName[] = "nearby_sharing.data_usage";
const char kNearbySharingDeviceIdPrefName[] = "nearby_sharing.device_id";
const char kNearbySharingDeviceNamePrefName[] = "nearby_sharing.device_name";
const char kNearbySharingEnabledPrefName[] = "nearby_sharing.enabled";
const char kNearbySharingFullNamePrefName[] = "nearby_sharing.full_name";
const char kNearbySharingIconUrlPrefName[] = "nearby_sharing.icon_url";
const char kNearbySharingOnboardingDismissedTimePrefName[] =
    "nearby_sharing.onboarding_dismissed_time";
const char kNearbySharingSchedulerDownloadDeviceDataPrefName[] =
    "nearby_sharing.scheduler.download_device_data";
const char kNearbySharingSchedulerUploadDeviceNamePrefName[] =
    "nearby_sharing.scheduler.upload_device_name";
const char kNearbySharingPublicCertificateExpirationDictPrefName[] =
    "nearbyshare.public_certificate_expiration_dict";
const char kNearbySharingPrivateCertificateListPrefName[] =
    "nearbyshare.private_certificate_list";
const char kNearbySharingSchedulerDownloadPublicCertificatesPrefName[] =
    "nearby_sharing.scheduler.download_public_certificates";

}  // namespace prefs

void RegisterNearbySharingPrefs(PrefRegistrySimple* registry) {
  // These prefs are not synced across devices on purpose.

  // TODO(vecore): Change the default to false after the settings ui is
  // available.
  registry->RegisterBooleanPref(prefs::kNearbySharingEnabledPrefName,
                                /*default_value=*/true);
  registry->RegisterIntegerPref(
      prefs::kNearbySharingBackgroundVisibilityName,
      /*default_value=*/static_cast<int>(Visibility::kUnknown));
  registry->RegisterIntegerPref(
      prefs::kNearbySharingDataUsageName,
      /*default_value=*/static_cast<int>(DataUsage::kWifiOnly));
  registry->RegisterStringPref(prefs::kNearbySharingDeviceIdPrefName,
                               /*default_value=*/std::string());
  registry->RegisterStringPref(prefs::kNearbySharingDeviceNamePrefName,
                               /*default_value=*/std::string());
  registry->RegisterListPref(prefs::kNearbySharingAllowedContactsPrefName);
  registry->RegisterStringPref(prefs::kNearbySharingFullNamePrefName,
                               /*default_value=*/std::string());
  registry->RegisterStringPref(prefs::kNearbySharingIconUrlPrefName,
                               /*default_value=*/std::string());
  registry->RegisterDictionaryPref(
      prefs::kNearbySharingSchedulerDownloadDeviceDataPrefName);
  registry->RegisterDictionaryPref(
      prefs::kNearbySharingSchedulerUploadDeviceNamePrefName);
  registry->RegisterTimePref(
      prefs::kNearbySharingOnboardingDismissedTimePrefName,
      /*default_value=*/base::Time());
  registry->RegisterDictionaryPref(
      prefs::kNearbySharingPublicCertificateExpirationDictPrefName);
  registry->RegisterListPref(
      prefs::kNearbySharingPrivateCertificateListPrefName);
  registry->RegisterDictionaryPref(
      prefs::kNearbySharingSchedulerDownloadPublicCertificatesPrefName);
}

void RegisterNearbySharingLocalPrefs(PrefRegistrySimple* local_state) {
  local_state->RegisterFilePathPref(prefs::kNearbySharingActiveProfilePrefName,
                                    /*default_value=*/base::FilePath());
}
