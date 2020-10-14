// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/external_web_app_manager.h"

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/test/bind_test_util.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/web_applications/components/web_app_helpers.h"
#include "chrome/browser/web_applications/test/test_file_utils.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_launcher.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace web_app {

class ExternalWebAppManagerBrowserTest
    : public extensions::ExtensionBrowserTest {
 public:
  ExternalWebAppManagerBrowserTest() {
    ExternalWebAppManager::SkipStartupScanForTesting();
  }

  GURL GetAppUrl() const {
    return embedded_test_server()->GetURL("/web_apps/basic.html");
  }

  const AppRegistrar& registrar() {
    return WebAppProvider::Get(browser()->profile())->registrar();
  }

  ~ExternalWebAppManagerBrowserTest() override = default;
};

IN_PROC_BROWSER_TEST_F(ExternalWebAppManagerBrowserTest, UninstallAndReplace) {
  ASSERT_TRUE(embedded_test_server()->Start());
  Profile* profile = browser()->profile();

  // Install Chrome app to be replaced.
  const char kChromeAppDirectory[] = "app";
  const char kChromeAppName[] = "App Test";
  const extensions::Extension* app = InstallExtensionWithSourceAndFlags(
      test_data_dir_.AppendASCII(kChromeAppDirectory), 1,
      extensions::Manifest::INTERNAL, extensions::Extension::NO_FLAGS);
  EXPECT_EQ(app->name(), kChromeAppName);

  // Start listening for Chrome app uninstall.
  extensions::TestExtensionRegistryObserver uninstall_observer(
      extensions::ExtensionRegistry::Get(profile));

  // Trigger default web app install.
  base::RunLoop sync_run_loop;
  WebAppProvider::Get(profile)
      ->external_web_app_manager_for_testing()
      .SynchronizeAppsForTesting(
          std::make_unique<FileUtilsWrapper>(),
          {base::ReplaceStringPlaceholders(
              R"({
                "app_url": "$1",
                "launch_container": "window",
                "user_type": ["unmanaged"],
                "uninstall_and_replace": ["$2"]
              })",
              {GetAppUrl().spec(), app->id()}, nullptr)},
          base::BindLambdaForTesting(
              [&](std::map<GURL, InstallResultCode> install_results,
                  std::map<GURL, bool> uninstall_results) {
                EXPECT_EQ(install_results.at(GetAppUrl()),
                          InstallResultCode::kSuccessNewInstall);
                sync_run_loop.Quit();
              }));
  sync_run_loop.Run();

  // Chrome app should get uninstalled.
  scoped_refptr<const extensions::Extension> uninstalled_app =
      uninstall_observer.WaitForExtensionUninstalled();
  EXPECT_EQ(app, uninstalled_app.get());
}

// TODO(crbug.com/1119710): Loading icon.png is flaky on Windows.
#if defined(OS_WIN)
#define MAYBE_OfflineManifest DISABLED_OfflineManifest
#else
#define MAYBE_OfflineManifest OfflineManifest
#endif
IN_PROC_BROWSER_TEST_F(ExternalWebAppManagerBrowserTest,
                       MAYBE_OfflineManifest) {
  ASSERT_TRUE(embedded_test_server()->Start());
  Profile* profile = browser()->profile();

  constexpr char kAppInstallUrl[] = "https://test.org/install.html";
  constexpr char kAppName[] = "Offline app name";
  constexpr char kAppUrl[] = "https://test.org/start.html";
  constexpr char kAppScope[] = "https://test.org/";
  AppId app_id = GenerateAppIdFromURL(GURL(kAppUrl));

  base::FilePath source_root_dir;
  CHECK(base::PathService::Get(base::DIR_SOURCE_ROOT, &source_root_dir));
  base::FilePath test_icon_path = source_root_dir.Append(GetChromeTestDataDir())
                                      .AppendASCII("web_apps/blue-192.png");

  EXPECT_FALSE(registrar().IsInstalled(app_id));

  // Sync default web apps.
  base::RunLoop sync_run_loop;
  WebAppProvider::Get(profile)
      ->external_web_app_manager_for_testing()
      .SynchronizeAppsForTesting(
          TestFileUtils::Create(
              {{base::FilePath(FILE_PATH_LITERAL("test_dir/icon.png")),
                test_icon_path}}),
          {base::ReplaceStringPlaceholders(
              R"({
                "app_url": "$1",
                "launch_container": "window",
                "user_type": ["unmanaged"],
                "offline_manifest": {
                  "name": "$2",
                  "start_url": "$3",
                  "scope": "$4",
                  "display": "minimal-ui",
                  "theme_color_argb_hex": "AABBCCDD",
                  "icon_any_pngs": ["icon.png"]
                }
              })",
              {kAppInstallUrl, kAppName, kAppUrl, kAppScope}, nullptr)},
          base::BindLambdaForTesting(
              [&](std::map<GURL, InstallResultCode> install_results,
                  std::map<GURL, bool> uninstall_results) {
                EXPECT_EQ(install_results.at(GURL(kAppInstallUrl)),
                          InstallResultCode::kSuccessNewInstall);
                sync_run_loop.Quit();
              }));
  sync_run_loop.Run();

  EXPECT_TRUE(registrar().IsInstalled(app_id));
  EXPECT_EQ(registrar().GetAppShortName(app_id), kAppName);
  EXPECT_EQ(registrar().GetAppLaunchURL(app_id).spec(), kAppUrl);
  EXPECT_EQ(registrar().GetAppScope(app_id).spec(), kAppScope);
  // theme_color must be installed opaque.
  EXPECT_EQ(registrar().GetAppThemeColor(app_id),
            SkColorSetARGB(0xFF, 0xBB, 0xCC, 0xDD));
  EXPECT_EQ(ReadAppIconPixel(profile, app_id, /*size=*/192, /*x=*/0, /*y=*/0),
            SK_ColorBLUE);
}

}  // namespace web_app
