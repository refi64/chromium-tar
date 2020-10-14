// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_CFM_PUBLIC_FEATURES_FEATURES_H_
#define CHROMEOS_SERVICES_CFM_PUBLIC_FEATURES_FEATURES_H_

#include "base/component_export.h"
#include "base/feature_list.h"

namespace chromeos {
namespace cfm {
namespace features {

COMPONENT_EXPORT(CFM_FEATURES)
extern const base::Feature kCfmMojoServices;

}  // namespace features
}  // namespace cfm
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_CFM_PUBLIC_FEATURES_FEATURES_H_
