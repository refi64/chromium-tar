// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/forced_extensions/force_installed_metrics.h"

#include <set>

#include "base/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "chrome/browser/extensions/extension_management.h"
#include "chrome/browser/extensions/extension_management_constants.h"
#include "chrome/browser/extensions/forced_extensions/install_stage_tracker.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/install/crx_install_error.h"
#include "extensions/browser/updater/extension_downloader.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "components/arc/arc_prefs.h"
#endif  // defined(OS_CHROMEOS)

namespace extensions {

using ExtensionStatus = ForceInstalledTracker::ExtensionStatus;
using FailureReason = InstallStageTracker::FailureReason;

namespace {
// Timeout to report UMA if not all force-installed extension were loaded.
constexpr base::TimeDelta kInstallationTimeout =
    base::TimeDelta::FromMinutes(5);

#if defined(OS_CHROMEOS)
// Helper method to convert user_manager::UserType to
// InstallStageTracker::UserType for histogram purposes.
ForceInstalledMetrics::UserType ConvertUserType(
    InstallStageTracker::UserInfo user_info) {
  switch (user_info.user_type) {
    case user_manager::USER_TYPE_REGULAR: {
      if (user_info.is_new_user)
        return ForceInstalledMetrics::UserType::USER_TYPE_REGULAR_NEW;
      return ForceInstalledMetrics::UserType::USER_TYPE_REGULAR_EXISTING;
    }
    case user_manager::USER_TYPE_GUEST:
      return ForceInstalledMetrics::UserType::USER_TYPE_GUEST;
    case user_manager::USER_TYPE_PUBLIC_ACCOUNT:
      return ForceInstalledMetrics::UserType::USER_TYPE_PUBLIC_ACCOUNT;
    case user_manager::USER_TYPE_SUPERVISED:
      return ForceInstalledMetrics::UserType::USER_TYPE_SUPERVISED;
    case user_manager::USER_TYPE_KIOSK_APP:
      return ForceInstalledMetrics::UserType::USER_TYPE_KIOSK_APP;
    case user_manager::USER_TYPE_CHILD:
      return ForceInstalledMetrics::UserType::USER_TYPE_CHILD;
    case user_manager::USER_TYPE_ARC_KIOSK_APP:
      return ForceInstalledMetrics::UserType::USER_TYPE_ARC_KIOSK_APP;
    case user_manager::USER_TYPE_ACTIVE_DIRECTORY:
      return ForceInstalledMetrics::UserType::USER_TYPE_ACTIVE_DIRECTORY;
    case user_manager::USER_TYPE_WEB_KIOSK_APP:
      return ForceInstalledMetrics::UserType::USER_TYPE_WEB_KIOSK_APP;
    default:
      NOTREACHED();
  }
  return ForceInstalledMetrics::UserType::kMaxValue;
}
#endif  // defined(OS_CHROMEOS)

}  // namespace

ForceInstalledMetrics::ForceInstalledMetrics(
    ExtensionRegistry* registry,
    Profile* profile,
    ForceInstalledTracker* tracker,
    std::unique_ptr<base::OneShotTimer> timer)
    : registry_(registry),
      profile_(profile),
      tracker_(tracker),
      start_time_(base::Time::Now()),
      timer_(std::move(timer)) {
  timer_->Start(
      FROM_HERE, kInstallationTimeout,
      base::BindOnce(&ForceInstalledMetrics::OnForceInstalledExtensionsLoaded,
                     base::Unretained(this)));
  if (tracker_->IsDoneLoading())
    OnForceInstalledExtensionsLoaded();
  else
    tracker_observer_.Add(tracker_);
}

ForceInstalledMetrics::~ForceInstalledMetrics() = default;

bool ForceInstalledMetrics::IsStatusGood(ExtensionStatus status) {
  switch (status) {
    case ExtensionStatus::PENDING:
      return false;
    case ExtensionStatus::LOADED:
      return true;
    case ExtensionStatus::READY:
      return true;
    case ExtensionStatus::FAILED:
      return false;
    default:
      NOTREACHED();
  }
  return false;
}

bool ForceInstalledMetrics::IsMisconfiguration(
    const InstallStageTracker::InstallationData& installation_data,
    const ExtensionId& id) {
  if (installation_data.install_error_detail) {
    ExtensionManagement* management =
        ExtensionManagementFactory::GetForBrowserContext(profile_);
    CrxInstallErrorDetail detail =
        installation_data.install_error_detail.value();
    if (detail == CrxInstallErrorDetail::KIOSK_MODE_ONLY)
      return true;

    if (installation_data.extension_type &&
        detail == CrxInstallErrorDetail::DISALLOWED_BY_POLICY &&
        !management->IsAllowedManifestType(
            installation_data.extension_type.value(), id)) {
      return true;
    }
  }
#if defined(OS_CHROMEOS)
  // REPLACED_BY_ARC_APP error is a misconfiguration if it ARC++ is enabled for
  // the device.
  if (profile_->GetPrefs()->IsManagedPreference(arc::prefs::kArcEnabled) &&
      profile_->GetPrefs()->GetBoolean(arc::prefs::kArcEnabled) &&
      installation_data.failure_reason == FailureReason::REPLACED_BY_ARC_APP) {
    return true;
  }
#endif  // defined(OS_CHROMEOS)

  if (installation_data.failure_reason ==
      FailureReason::NOT_PERFORMING_NEW_INSTALL) {
    return true;
  }
  if (installation_data.failure_reason == FailureReason::CRX_FETCH_URL_EMPTY) {
    DCHECK(installation_data.no_updates_info);
    if (installation_data.no_updates_info.value() ==
        InstallStageTracker::NoUpdatesInfo::kEmpty) {
      return true;
    }
  }

  return false;
}

void ForceInstalledMetrics::ReportDisableReason(
    const ExtensionId& extension_id) {
  int disable_reasons =
      ExtensionPrefs::Get(profile_)->GetDisableReasons(extension_id);
  // Choose any disable reason among the disable reasons for this extension.
  disable_reasons = disable_reasons & ~(disable_reasons - 1);
  base::UmaHistogramSparse("Extensions.ForceInstalledNotLoadedDisableReason",
                           disable_reasons);
}

void ForceInstalledMetrics::ReportMetrics() {
  base::UmaHistogramCounts100("Extensions.ForceInstalledTotalCandidateCount",
                              tracker_->extensions().size());
  std::set<ExtensionId> missing_forced_extensions;
  InstallStageTracker* install_stage_tracker =
      InstallStageTracker::Get(profile_);
  for (const auto& extension : tracker_->extensions()) {
    if (!IsStatusGood(extension.second.status)) {
      missing_forced_extensions.insert(extension.first);
    } else {
      InstallStageTracker::InstallationData installation =
          install_stage_tracker->Get(extension.first);
      if (installation.download_manifest_finish_time &&
          installation.download_manifest_started_time) {
        base::UmaHistogramLongTimes(
            "Extensions.ForceInstalledTime.DownloadingStartTo."
            "ManifestDownloadComplete",
            installation.download_manifest_finish_time.value() -
                installation.download_manifest_started_time.value());
      }
      // Report the download time for CRX only when
      // installation.download_CRX_started_time is set because in other case CRX
      // is fetched from cache and the download was not started.
      if (installation.download_CRX_finish_time &&
          installation.download_CRX_started_time) {
        base::UmaHistogramLongTimes(
            "Extensions.ForceInstalledTime.ManifestDownloadCompleteTo."
            "CRXDownloadComplete",
            installation.download_CRX_finish_time.value() -
                installation.download_CRX_started_time.value());
      }
      if (installation.copying_started_time) {
        DCHECK(installation.verification_started_time);
        base::UmaHistogramLongTimes(
            "Extensions.ForceInstalledTime.VerificationStartTo.CopyingStart",
            installation.copying_started_time.value() -
                installation.verification_started_time.value());
      }
      if (installation.unpacking_started_time &&
          installation.copying_started_time) {
        base::UmaHistogramLongTimes(
            "Extensions.ForceInstalledTime.CopyingStartTo.UnpackingStart",
            installation.unpacking_started_time.value() -
                installation.copying_started_time.value());
      }
      if (installation.checking_expectations_started_time &&
          installation.unpacking_started_time) {
        base::UmaHistogramLongTimes(
            "Extensions.ForceInstalledTime.UnpackingStartTo."
            "CheckingExpectationsStart",
            installation.checking_expectations_started_time.value() -
                installation.unpacking_started_time.value());
      }
      if (installation.finalizing_started_time &&
          installation.checking_expectations_started_time) {
        base::UmaHistogramLongTimes(
            "Extensions.ForceInstalledTime.CheckingExpectationsStartTo."
            "FinalizingStart",
            installation.finalizing_started_time.value() -
                installation.checking_expectations_started_time.value());
      }
      if (installation.installation_complete_time &&
          installation.finalizing_started_time) {
        base::UmaHistogramLongTimes(
            "Extensions.ForceInstalledTime.FinalizingStartTo."
            "CRXInstallComplete",
            installation.installation_complete_time.value() -
                installation.finalizing_started_time.value());
      }
    }
  }
  if (missing_forced_extensions.empty()) {
    base::UmaHistogramLongTimes("Extensions.ForceInstalledLoadTime",
                                base::Time::Now() - start_time_);
    // TODO(burunduk): Remove VLOGs after resolving crbug/917700 and
    // crbug/904600.
    VLOG(2) << "All forced extensions seems to be installed";
    return;
  }
  size_t enabled_missing_count = missing_forced_extensions.size();
  size_t blocklisted_count = 0;
  auto installed_extensions = registry_->GenerateInstalledExtensionsSet();
  auto blocklisted_extensions = registry_->GenerateInstalledExtensionsSet(
      ExtensionRegistry::IncludeFlag::BLOCKLISTED);
  for (const auto& entry : *installed_extensions) {
    if (missing_forced_extensions.count(entry->id())) {
      missing_forced_extensions.erase(entry->id());
      ReportDisableReason(entry->id());
      if (blocklisted_extensions->Contains(entry->id()))
        blocklisted_count++;
    }
  }
  size_t misconfigured_extensions = 0;
  size_t installed_missing_count = missing_forced_extensions.size();

  base::UmaHistogramCounts100("Extensions.ForceInstalledTimedOutCount",
                              enabled_missing_count);
  base::UmaHistogramCounts100(
      "Extensions.ForceInstalledTimedOutAndNotInstalledCount",
      installed_missing_count);
  base::UmaHistogramCounts100("Extensions.ForceInstalledAndBlackListed",
                              blocklisted_count);
  VLOG(2) << "Failed to install " << installed_missing_count
          << " forced extensions.";
  for (const auto& extension_id : missing_forced_extensions) {
    InstallStageTracker::InstallationData installation =
        install_stage_tracker->Get(extension_id);
    base::UmaHistogramEnumeration(
        "Extensions.ForceInstalledFailureCacheStatus",
        installation.downloading_cache_status.value_or(
            ExtensionDownloaderDelegate::CacheStatus::CACHE_UNKNOWN));
    if (!installation.failure_reason && installation.install_stage) {
      installation.failure_reason = FailureReason::IN_PROGRESS;
      InstallStageTracker::Stage install_stage =
          installation.install_stage.value();
      base::UmaHistogramEnumeration("Extensions.ForceInstalledStage",
                                    install_stage);
      if (install_stage == InstallStageTracker::Stage::DOWNLOADING) {
        DCHECK(installation.downloading_stage);
        ExtensionDownloaderDelegate::Stage downloading_stage =
            installation.downloading_stage.value();
        base::UmaHistogramEnumeration(
            "Extensions.ForceInstalledDownloadingStage", downloading_stage);
      }
    }
    if (IsMisconfiguration(installation, extension_id))
      misconfigured_extensions++;
    FailureReason failure_reason =
        installation.failure_reason.value_or(FailureReason::UNKNOWN);
    base::UmaHistogramEnumeration("Extensions.ForceInstalledFailureReason3",
                                  failure_reason);
    if (tracker_->extensions().at(extension_id).is_from_store) {
      base::UmaHistogramEnumeration(
          "Extensions.WebStore_ForceInstalledFailureReason3", failure_reason);
    } else {
      base::UmaHistogramEnumeration(
          "Extensions.OffStore_ForceInstalledFailureReason3", failure_reason);
    }

    // In case of CRX_FETCH_FAILURE, report the network error code, HTTP
    // error code and number of fetch tries made.
    if (failure_reason == FailureReason::CRX_FETCH_FAILED) {
      base::UmaHistogramSparse("Extensions.ForceInstalledNetworkErrorCode",
                               installation.network_error_code.value());

      if (installation.response_code) {
        base::UmaHistogramSparse("Extensions.ForceInstalledHttpErrorCode",
                                 installation.response_code.value());
      }
      base::UmaHistogramExactLinear("Extensions.ForceInstalledFetchTries",
                                    installation.fetch_tries.value(),
                                    ExtensionDownloader::kMaxRetries);
    }

    // In case of MANIFEST_FETCH_FAILURE, report the network error code,
    // HTTP error code and number of fetch tries made.
    if (failure_reason == FailureReason::MANIFEST_FETCH_FAILED) {
      base::UmaHistogramSparse(
          "Extensions.ForceInstalledManifestFetchFailedNetworkErrorCode",
          installation.network_error_code.value());

      if (installation.response_code) {
        base::UmaHistogramSparse(
            "Extensions.ForceInstalledManifestFetchFailedHttpErrorCode",
            installation.response_code.value());
      }
      base::UmaHistogramExactLinear(
          "Extensions.ForceInstalledManifestFetchFailedFetchTries",
          installation.fetch_tries.value(), ExtensionDownloader::kMaxRetries);
    }
#if defined(OS_CHROMEOS)
    // Report type of user in case Force Installed Extensions fail to
    // install only if there is a user corresponding to given profile. There can
    // be extensions on the login screen. There is no user on the login screen
    // and thus we would not report in that case.
    if (chromeos::ProfileHelper::Get()->GetUserByProfile(profile_)) {
      InstallStageTracker::UserInfo user_info =
          InstallStageTracker::GetUserInfo(profile_);
      base::UmaHistogramEnumeration(
          "Extensions.ForceInstalledFailureSessionType",
          ConvertUserType(user_info));
    }
#endif  // defined(OS_CHROMEOS)
    VLOG(2) << "Forced extension " << extension_id
            << " failed to install with data="
            << InstallStageTracker::GetFormattedInstallationData(installation);
    if (installation.install_error_detail) {
      CrxInstallErrorDetail detail = installation.install_error_detail.value();
      base::UmaHistogramEnumeration(
          "Extensions.ForceInstalledFailureCrxInstallError", detail);
    }
    if (installation.unpacker_failure_reason) {
      base::UmaHistogramEnumeration(
          "Extensions.ForceInstalledFailureSandboxUnpackFailureReason",
          installation.unpacker_failure_reason.value(),
          SandboxedUnpackerFailureReason::NUM_FAILURE_REASONS);
    }
    if (failure_reason == FailureReason::CRX_FETCH_URL_EMPTY) {
      if (installation.update_check_status) {
        base::UmaHistogramEnumeration(
            "Extensions.ForceInstalledFailureUpdateCheckStatus",
            installation.update_check_status.value());
      }

      DCHECK(installation.no_updates_info);
      base::UmaHistogramEnumeration(
          "Extensions."
          "ForceInstalledFailureNoUpdatesInfo",
          installation.no_updates_info.value());
    }
    if (installation.manifest_invalid_error) {
      DCHECK_EQ(failure_reason, FailureReason::MANIFEST_INVALID);
      base::UmaHistogramEnumeration(
          "Extensions.ForceInstalledFailureManifestInvalidErrorDetail2",
          installation.manifest_invalid_error.value());
      if (installation.app_status_error) {
        base::UmaHistogramEnumeration(
            "Extensions.ForceInstalledFailureManifestInvalidAppStatusError",
            installation.app_status_error.value());
      }
    }
  }
  bool non_misconfigured_failure_occurred =
      misconfigured_extensions != missing_forced_extensions.size();
  base::UmaHistogramBoolean(
      "Extensions."
      "ForceInstalledSessionsWithNonMisconfigurationFailureOccured",
      non_misconfigured_failure_occurred);
}

void ForceInstalledMetrics::OnForceInstalledExtensionsLoaded() {
  if (reported_)
    return;
  // Report only if there was non-empty list of force-installed extensions.
  if (!tracker_->extensions().empty())
    ReportMetrics();
  reported_ = true;
  timer_->Stop();
}

void ForceInstalledMetrics::OnExtensionDownloadCacheStatusRetrieved(
    const ExtensionId& id,
    ExtensionDownloaderDelegate::CacheStatus cache_status) {
  UMA_HISTOGRAM_ENUMERATION("Extensions.ForceInstalledCacheStatus",
                            cache_status);
}

}  //  namespace extensions
