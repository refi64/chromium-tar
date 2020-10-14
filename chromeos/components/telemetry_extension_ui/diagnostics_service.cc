// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/telemetry_extension_ui/diagnostics_service.h"

#include <utility>
#include <vector>

#include "base/bind.h"
#include "chromeos/components/telemetry_extension_ui/diagnostics_service_converters.h"
#include "chromeos/services/cros_healthd/public/cpp/service_connection.h"
#include "chromeos/services/cros_healthd/public/mojom/cros_healthd_diagnostics.mojom.h"

namespace chromeos {

DiagnosticsService::DiagnosticsService(
    mojo::PendingReceiver<health::mojom::DiagnosticsService> receiver)
    : receiver_(this, std::move(receiver)) {}

DiagnosticsService::~DiagnosticsService() = default;

cros_healthd::mojom::CrosHealthdDiagnosticsService*
DiagnosticsService::GetService() {
  if (!service_ || !service_.is_connected()) {
    cros_healthd::ServiceConnection::GetInstance()->GetDiagnosticsService(
        service_.BindNewPipeAndPassReceiver());
    service_.set_disconnect_handler(base::BindOnce(
        &DiagnosticsService::OnDisconnect, base::Unretained(this)));
  }
  return service_.get();
}

void DiagnosticsService::OnDisconnect() {
  service_.reset();
}

void DiagnosticsService::GetAvailableRoutines(
    GetAvailableRoutinesCallback callback) {
  GetService()->GetAvailableRoutines(base::BindOnce(
      [](health::mojom::DiagnosticsService::GetAvailableRoutinesCallback
             callback,
         const std::vector<cros_healthd::mojom::DiagnosticRoutineEnum>&
             routines) {
        std::move(callback).Run(
            diagnostics_service_converters::Convert(routines));
      },
      std::move(callback)));
}

void DiagnosticsService::GetRoutineUpdate(
    int32_t id,
    health::mojom::DiagnosticRoutineCommandEnum command,
    bool include_output,
    GetRoutineUpdateCallback callback) {
  GetService()->GetRoutineUpdate(
      id, diagnostics_service_converters::Convert(command), include_output,
      base::BindOnce(
          [](health::mojom::DiagnosticsService::GetRoutineUpdateCallback
                 callback,
             cros_healthd::mojom::RoutineUpdatePtr ptr) {
            std::move(callback).Run(
                diagnostics_service_converters::ConvertPtr(std::move(ptr)));
          },
          std::move(callback)));
}

}  // namespace chromeos
