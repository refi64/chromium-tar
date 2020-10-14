// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/scanning/scanning_ui.h"

#include <string>

#include "base/containers/span.h"
#include "base/memory/ptr_util.h"
#include "chromeos/components/scanning/url_constants.h"
#include "chromeos/grit/chromeos_scanning_app_resources.h"
#include "chromeos/grit/chromeos_scanning_app_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/resources/grit/webui_resources.h"

namespace chromeos {

namespace {

constexpr char kGeneratedPath[] =
    "@out_folder@/gen/chromeos/components/scanning/resources/";

// TODO(jschettler): Replace with webui::SetUpWebUIDataSource() once it no
// longer requires a dependency on //chrome/browser.
void SetUpWebUIDataSource(content::WebUIDataSource* source,
                          base::span<const GritResourceMap> resources,
                          const std::string& generated_path,
                          int default_resource) {
  for (const auto& resource : resources) {
    std::string path = resource.name;
    if (path.rfind(generated_path, 0) == 0)
      path = path.substr(generated_path.size());

    source->AddResourcePath(path, resource.value);
  }

  source->SetDefaultResource(default_resource);
  source->AddResourcePath("test_loader.html", IDR_WEBUI_HTML_TEST_LOADER);
  source->AddResourcePath("test_loader.js", IDR_WEBUI_JS_TEST_LOADER);
}

}  // namespace

ScanningUI::ScanningUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  auto html_source = base::WrapUnique(
      content::WebUIDataSource::Create(kChromeUIScanningAppHost));
  html_source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      "script-src chrome://resources chrome://test 'self';");
  html_source->DisableTrustedTypesCSP();

  const auto resources = base::make_span(kChromeosScanningAppResources,
                                         kChromeosScanningAppResourcesSize);
  SetUpWebUIDataSource(html_source.get(), resources, kGeneratedPath,
                       IDR_SCANNING_APP_INDEX_HTML);

  content::WebUIDataSource::Add(web_ui->GetWebContents()->GetBrowserContext(),
                                html_source.release());
}

ScanningUI::~ScanningUI() = default;

}  // namespace chromeos
