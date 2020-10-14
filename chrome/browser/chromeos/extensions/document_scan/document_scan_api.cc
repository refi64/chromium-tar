// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/document_scan/document_scan_api.h"

#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/bind.h"
#include "chrome/browser/chromeos/scanning/lorgnette_scanner_manager.h"
#include "chrome/browser/chromeos/scanning/lorgnette_scanner_manager_factory.h"
#include "content/public/browser/browser_context.h"
#include "third_party/cros_system_api/dbus/lorgnette/dbus-constants.h"

namespace extensions {

namespace api {

namespace {

// Error messages that can be included in a response when scanning fails.
constexpr char kUserGestureRequiredError[] =
    "User gesture required to perform scan";
constexpr char kNoScannersAvailableError[] = "No scanners available";
constexpr char kUnsupportedMimeTypesError[] = "Unsupported MIME types";
constexpr char kScanImageError[] = "Failed to scan image";

// The PNG MIME type.
constexpr char kScannerImageMimeTypePng[] = "image/png";

// The PNG image data URL prefix of a scanned image.
constexpr char kPngImageDataUrlPrefix[] = "data:image/png;base64,";

}  // namespace

DocumentScanScanFunction::DocumentScanScanFunction() = default;

DocumentScanScanFunction::~DocumentScanScanFunction() = default;

ExtensionFunction::ResponseAction DocumentScanScanFunction::Run() {
  params_ = document_scan::Scan::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params_.get());

  if (!user_gesture())
    return RespondNow(Error(kUserGestureRequiredError));

  chromeos::LorgnetteScannerManagerFactory::GetForBrowserContext(
      browser_context())
      ->GetScannerNames(
          base::BindOnce(&DocumentScanScanFunction::OnNamesReceived, this));
  return did_respond() ? AlreadyResponded() : RespondLater();
}

void DocumentScanScanFunction::OnNamesReceived(
    std::vector<std::string> scanner_names) {
  if (scanner_names.empty()) {
    Respond(Error(kNoScannersAvailableError));
    return;
  }

  // PNG is currently the only supported MIME type.
  if (params_->options.mime_types) {
    std::vector<std::string>& mime_types = *params_->options.mime_types;
    if (!base::Contains(mime_types, kScannerImageMimeTypePng)) {
      Respond(Error(kUnsupportedMimeTypesError));
      return;
    }
  }

  // TODO(pstew): Call a delegate method here to select a scanner and options.
  // The first scanner supporting one of the requested MIME types used to be
  // selected. Since all of the scanners only support PNG, this results in
  // selecting the first scanner in the list.
  const std::string& scanner_name = scanner_names[0];
  chromeos::LorgnetteManagerClient::ScanProperties properties;
  properties.mode = lorgnette::kScanPropertyModeColor;
  chromeos::LorgnetteScannerManagerFactory::GetForBrowserContext(
      browser_context())
      ->Scan(
          scanner_name, properties,
          base::BindOnce(&DocumentScanScanFunction::OnResultsReceived, this));
}

void DocumentScanScanFunction::OnResultsReceived(
    base::Optional<std::string> scanned_image) {
  // TODO(pstew): Enlist a delegate to display received scan in the UI and
  // confirm that this scan should be sent to the caller. If this is a
  // multi-page scan, provide a means for adding additional scanned images up to
  // the requested limit.
  if (!scanned_image.has_value()) {
    Respond(Error(kScanImageError));
    return;
  }

  std::string image_base64;
  base::Base64Encode(scanned_image.value(), &image_base64);
  document_scan::ScanResults scan_results;
  scan_results.data_urls.push_back(kPngImageDataUrlPrefix + image_base64);
  scan_results.mime_type = kScannerImageMimeTypePng;
  Respond(ArgumentList(document_scan::Scan::Results::Create(scan_results)));
}

}  // namespace api

}  // namespace extensions
