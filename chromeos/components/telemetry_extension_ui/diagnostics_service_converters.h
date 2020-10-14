// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_COMPONENTS_TELEMETRY_EXTENSION_UI_DIAGNOSTICS_SERVICE_CONVERTERS_H_
#define CHROMEOS_COMPONENTS_TELEMETRY_EXTENSION_UI_DIAGNOSTICS_SERVICE_CONVERTERS_H_

#if defined(OFFICIAL_BUILD)
#error Diagnostics service should only be included in unofficial builds.
#endif

#include <string>
#include <utility>
#include <vector>

#include "chromeos/components/telemetry_extension_ui/mojom/diagnostics_service.mojom-forward.h"
#include "chromeos/services/cros_healthd/public/mojom/cros_healthd_diagnostics.mojom-forward.h"
#include "mojo/public/cpp/system/handle.h"

namespace chromeos {
namespace diagnostics_service_converters {

// This file contains helper functions used by DiagnosticsService to convert its
// types to/from cros_healthd DiagnosticsService types.

namespace unchecked {

health::mojom::RoutineUpdatePtr UncheckedConvertPtr(
    cros_healthd::mojom::RoutineUpdatePtr input);

health::mojom::RoutineUpdateUnionPtr UncheckedConvertPtr(
    cros_healthd::mojom::RoutineUpdateUnionPtr input);

health::mojom::InteractiveRoutineUpdatePtr UncheckedConvertPtr(
    cros_healthd::mojom::InteractiveRoutineUpdatePtr input);

health::mojom::NonInteractiveRoutineUpdatePtr UncheckedConvertPtr(
    cros_healthd::mojom::NonInteractiveRoutineUpdatePtr input);

}  // namespace unchecked

std::vector<health::mojom::DiagnosticRoutineEnum> Convert(
    const std::vector<cros_healthd::mojom::DiagnosticRoutineEnum>& input);

health::mojom::DiagnosticRoutineUserMessageEnum Convert(
    cros_healthd::mojom::DiagnosticRoutineUserMessageEnum input);

health::mojom::DiagnosticRoutineStatusEnum Convert(
    cros_healthd::mojom::DiagnosticRoutineStatusEnum input);

cros_healthd::mojom::DiagnosticRoutineCommandEnum Convert(
    health::mojom::DiagnosticRoutineCommandEnum input);

std::string Convert(mojo::ScopedHandle handle);

template <class InputT>
auto ConvertPtr(InputT input) {
  return (!input.is_null()) ? unchecked::UncheckedConvertPtr(std::move(input))
                            : nullptr;
}

}  // namespace diagnostics_service_converters
}  // namespace chromeos

#endif  // CHROMEOS_COMPONENTS_TELEMETRY_EXTENSION_UI_DIAGNOSTICS_SERVICE_CONVERTERS_H_
