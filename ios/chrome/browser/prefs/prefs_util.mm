// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/prefs/prefs_util.h"

#include "components/prefs/pref_service.h"
#include "ios/chrome/browser/policy/policy_features.h"
#include "ios/chrome/browser/pref_names.h"
#include "ios/chrome/browser/prefs/browser_prefs.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

bool IsIncognitoModeDisabled(PrefService* pref_service) {
  return IsEnterprisePolicyEnabled() &&
         pref_service->IsManagedPreference(prefs::kIncognitoModeAvailability) &&
         pref_service->GetInteger(prefs::kIncognitoModeAvailability) ==
             static_cast<int>(IncognitoModePrefs::kDisabled);
}

bool IsIncognitoModeForced(PrefService* pref_service) {
  return IsEnterprisePolicyEnabled() &&
         pref_service->IsManagedPreference(prefs::kIncognitoModeAvailability) &&
         pref_service->GetInteger(prefs::kIncognitoModeAvailability) ==
             static_cast<int>(IncognitoModePrefs::kForced);
}
