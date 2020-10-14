// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/telemetry_extension_ui/test/telemetry_extension_ui_browsertest.h"

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "chrome/browser/chromeos/wilco_dtc_supportd/mojo_utils.h"
#include "chromeos/components/telemetry_extension_ui/url_constants.h"
#include "chromeos/components/web_applications/test/sandboxed_web_ui_test_base.h"
#include "chromeos/constants/chromeos_switches.h"
#include "chromeos/dbus/cros_healthd/cros_healthd_client.h"
#include "chromeos/dbus/cros_healthd/fake_cros_healthd_client.h"

namespace {

// File with utility functions for testing, defines `test_util`.
constexpr base::FilePath::CharType kWebUiTestUtil[] =
    FILE_PATH_LITERAL("chrome/test/data/webui/test_util.js");

// File that `kWebUiTestUtil` is dependent on, defines `cr`.
constexpr base::FilePath::CharType kCr[] =
    FILE_PATH_LITERAL("ui/webui/resources/js/cr.js");

// Folder containing the resources for JS browser tests.
constexpr base::FilePath::CharType kUntrustedAppResources[] = FILE_PATH_LITERAL(
    "chromeos/components/telemetry_extension_ui/test/untrusted_app_resources");

// File containing the query handlers for JS unit tests.
constexpr base::FilePath::CharType kUntrustedTestHandlers[] = FILE_PATH_LITERAL(
    "chromeos/components/telemetry_extension_ui/test/"
    "untrusted_test_handlers.js");

// Test cases that run in the untrusted context.
constexpr base::FilePath::CharType kUntrustedTestCases[] = FILE_PATH_LITERAL(
    "chromeos/components/telemetry_extension_ui/test/untrusted_browsertest.js");

}  // namespace

TelemetryExtensionUiBrowserTest::TelemetryExtensionUiBrowserTest()
    : SandboxedWebUiAppTestBase(
          chromeos::kChromeUITelemetryExtensionURL,
          chromeos::kChromeUIUntrustedTelemetryExtensionURL,
          {base::FilePath(kCr), base::FilePath(kWebUiTestUtil),
           base::FilePath(kUntrustedTestHandlers),
           base::FilePath(kUntrustedTestCases)}) {}

TelemetryExtensionUiBrowserTest::~TelemetryExtensionUiBrowserTest() = default;

void TelemetryExtensionUiBrowserTest::SetUpCommandLine(
    base::CommandLine* command_line) {
  base::FilePath source_root;
  base::PathService::Get(base::DIR_SOURCE_ROOT, &source_root);
  base::FilePath file_path(kUntrustedAppResources);

  command_line->AppendSwitchASCII(
      chromeos::switches::kTelemetryExtensionDirectory,
      source_root.Append(file_path).value());

  SandboxedWebUiAppTestBase::SetUpCommandLine(command_line);
}

void TelemetryExtensionUiBrowserTest::
    ConfigureDiagnosticsForInteractiveUpdate() {
  namespace cros_healthd = ::chromeos::cros_healthd::mojom;

  auto input = cros_healthd::RoutineUpdate::New();
  auto routineUpdateUnion = cros_healthd::RoutineUpdateUnion::New();
  auto interactiveRoutineUpdate = cros_healthd::InteractiveRoutineUpdate::New();

  interactiveRoutineUpdate->user_message =
      cros_healthd::DiagnosticRoutineUserMessageEnum::kUnplugACPower;

  routineUpdateUnion->set_interactive_update(
      std::move(interactiveRoutineUpdate));

  input->progress_percent = 0;
  input->output = chromeos::MojoUtils::CreateReadOnlySharedMemoryMojoHandle(
      "This routine is running!");
  input->routine_update_union = std::move(routineUpdateUnion);

  chromeos::cros_healthd::FakeCrosHealthdClient::Get()
      ->SetGetRoutineUpdateResponseForTesting(input);
}

void TelemetryExtensionUiBrowserTest::
    ConfigureDiagnosticsForNonInteractiveUpdate() {
  namespace cros_healthd = ::chromeos::cros_healthd::mojom;

  auto input = cros_healthd::RoutineUpdate::New();
  auto routineUpdateUnion = cros_healthd::RoutineUpdateUnion::New();
  auto nonInteractiveRoutineUpdate =
      cros_healthd::NonInteractiveRoutineUpdate::New();

  nonInteractiveRoutineUpdate->status =
      cros_healthd::DiagnosticRoutineStatusEnum::kReady;
  nonInteractiveRoutineUpdate->status_message = "Routine ran by Google.";

  routineUpdateUnion->set_noninteractive_update(
      std::move(nonInteractiveRoutineUpdate));

  input->progress_percent = 3147483771;
  input->routine_update_union = std::move(routineUpdateUnion);

  chromeos::cros_healthd::FakeCrosHealthdClient::Get()
      ->SetGetRoutineUpdateResponseForTesting(input);
}

void TelemetryExtensionUiBrowserTest::SetUpOnMainThread() {
  {
    namespace cros_diagnostics = ::chromeos::cros_healthd::mojom;

    std::vector<cros_diagnostics::DiagnosticRoutineEnum> input{
        cros_diagnostics::DiagnosticRoutineEnum::kBatteryCapacity,
        cros_diagnostics::DiagnosticRoutineEnum::kBatteryHealth,
        cros_diagnostics::DiagnosticRoutineEnum::kUrandom,
        cros_diagnostics::DiagnosticRoutineEnum::kSmartctlCheck,
        cros_diagnostics::DiagnosticRoutineEnum::kAcPower,
        cros_diagnostics::DiagnosticRoutineEnum::kCpuCache,
        cros_diagnostics::DiagnosticRoutineEnum::kCpuStress,
        cros_diagnostics::DiagnosticRoutineEnum::kFloatingPointAccuracy,
        cros_diagnostics::DiagnosticRoutineEnum::kNvmeWearLevel,
        cros_diagnostics::DiagnosticRoutineEnum::kNvmeSelfTest,
        cros_diagnostics::DiagnosticRoutineEnum::kDiskRead,
        cros_diagnostics::DiagnosticRoutineEnum::kPrimeSearch,
        cros_diagnostics::DiagnosticRoutineEnum::kBatteryDischarge,
    };

    chromeos::cros_healthd::FakeCrosHealthdClient::Get()
        ->SetAvailableRoutinesForTesting(input);
  }

  auto telemetry_info = chromeos::cros_healthd::mojom::TelemetryInfo::New();
  {
    auto battery_info = chromeos::cros_healthd::mojom::BatteryInfo::New();
    battery_info->cycle_count = 100000000000000;
    battery_info->voltage_now = 1234567890.123456;
    battery_info->vendor = "Google";
    battery_info->serial_number = "abcdef";
    battery_info->charge_full_design = 3000000000000000;
    battery_info->charge_full = 9000000000000000;
    battery_info->voltage_min_design = 1000000000.1001;
    battery_info->model_name = "Google Battery";
    battery_info->charge_now = 7777777777.777;
    battery_info->current_now = 0.9999999999999;
    battery_info->technology = "Li-ion";
    battery_info->status = "Charging";
    battery_info->manufacture_date = "2020-07-30";
    battery_info->temperature =
        chromeos::cros_healthd::mojom::UInt64Value::New(7777777777777777);

    telemetry_info->battery_result =
        chromeos::cros_healthd::mojom::BatteryResult::NewBatteryInfo(
            std::move(battery_info));
  }
  {
    auto block_device_info =
        chromeos::cros_healthd::mojom::NonRemovableBlockDeviceInfo::New();
    block_device_info->path = "/dev/device1";
    block_device_info->size = 5555555555555555;
    block_device_info->type = "NVMe";
    block_device_info->manufacturer_id = 200;
    block_device_info->name = "goog";
    block_device_info->serial = 4287654321;
    block_device_info->bytes_read_since_last_boot = 9000000000000000;
    block_device_info->bytes_written_since_last_boot = 8000000000000000;
    block_device_info->read_time_seconds_since_last_boot = 7000000000000000;
    block_device_info->write_time_seconds_since_last_boot = 6666666666666666;
    block_device_info->io_time_seconds_since_last_boot = 1111111111111;
    block_device_info->discard_time_seconds_since_last_boot =
        chromeos::cros_healthd::mojom::UInt64Value::New(77777777777777);

    // Need to put some placeholder values, otherwise Mojo will crash, because
    // mandatory union fields cannot be nullptr.
    block_device_info->vendor_id =
        chromeos::cros_healthd::mojom::BlockDeviceVendor::NewOther(0);
    block_device_info->product_id =
        chromeos::cros_healthd::mojom::BlockDeviceProduct::NewOther(0);
    block_device_info->revision =
        chromeos::cros_healthd::mojom::BlockDeviceRevision::NewOther(0);
    block_device_info->firmware_version =
        chromeos::cros_healthd::mojom::BlockDeviceFirmware::NewOther(0);

    std::vector<chromeos::cros_healthd::mojom::NonRemovableBlockDeviceInfoPtr>
        infos;
    infos.push_back(std::move(block_device_info));

    telemetry_info->block_device_result = chromeos::cros_healthd::mojom::
        NonRemovableBlockDeviceResult::NewBlockDeviceInfo(std::move(infos));
  }

  DCHECK(chromeos::cros_healthd::FakeCrosHealthdClient::Get());

  chromeos::cros_healthd::FakeCrosHealthdClient::Get()
      ->SetProbeTelemetryInfoResponseForTesting(telemetry_info);

  SandboxedWebUiAppTestBase::SetUpOnMainThread();
}
