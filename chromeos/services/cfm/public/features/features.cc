// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/cfm/public/features/features.h"

namespace chromeos {
namespace cfm {
namespace features {

// Enables or disables the ability to bind mojo connections through chrome for
// Cfm specific mojom based system services.
const base::Feature kCfmMojoServices{"CfmMojoServices",
                                     base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace features
}  // namespace cfm
}  // namespace chromeos
