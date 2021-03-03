// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/test/bind.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/banners/test_app_banner_manager_desktop.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_view.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/ui/web_applications/web_app_dialog_utils.h"
#include "chrome/browser/ui/web_applications/web_app_menu_model.h"
#include "chrome/browser/web_applications/components/app_registry_controller.h"
#include "chrome/browser/web_applications/components/install_finalizer.h"
#include "chrome/browser/web_applications/components/os_integration_manager.h"
#include "chrome/browser/web_applications/components/policy/web_app_policy_constants.h"
#include "chrome/browser/web_applications/components/policy/web_app_policy_manager.h"
#include "chrome/browser/web_applications/components/web_app_constants.h"
#include "chrome/browser/web_applications/components/web_app_id.h"
#include "chrome/browser/web_applications/components/web_app_provider_base.h"
#include "chrome/browser/web_applications/test/web_app_install_observer.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_navigation_observer.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/public/cpp/network_switches.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/blink/public/mojom/manifest/display_mode.mojom-shared.h"
#include "third_party/re2/src/re2/re2.h"

namespace {

const std::string kTestCaseFilename =
    "web_app_integration_browsertest_cases.csv";
const std::string kExpectationsFilename = "TestExpectations";
const std::string kPlatformName =
#if BUILDFLAG(IS_CHROMEOS_ASH)
    "ChromeOS";
#elif defined(OS_LINUX)
    "Linux";
#elif defined(OS_MAC)
    "Mac";
#elif defined(OS_WIN)
    "Win";
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

std::string StripAllWhitespace(std::string line) {
  std::string output;
  output.reserve(line.size());
  for (const char& c : line) {
    if (!isspace(c)) {
      output += c;
    }
  }
  return output;
}

// Returns the path of the requested file in the test data directory.
base::FilePath GetTestFilePath(const std::string& file_name) {
  base::FilePath file_path;
  base::PathService::Get(base::DIR_SOURCE_ROOT, &file_path);
  file_path = file_path.Append(FILE_PATH_LITERAL("chrome"));
  file_path = file_path.Append(FILE_PATH_LITERAL("test"));
  file_path = file_path.Append(FILE_PATH_LITERAL("data"));
  file_path = file_path.Append(FILE_PATH_LITERAL("web_apps"));
  return file_path.AppendASCII(file_name);
}

std::vector<std::string> ReadTestInputFile(const std::string& file_name) {
  base::FilePath file = GetTestFilePath(file_name);
  std::string contents;
  std::vector<std::string> test_cases;
  if (!base::ReadFileToString(file, &contents)) {
    return test_cases;
  }

  std::vector<std::string> file_lines = base::SplitString(
      contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto& line : file_lines) {
    if (line[0] == '#') {
      continue;
    }

    if (line.find('|') == std::string::npos) {
      test_cases.push_back(StripAllWhitespace(line));
      continue;
    }

    std::vector<std::string> platforms_and_test = base::SplitString(
        line, "|", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (platforms_and_test[0].find(kPlatformName) != std::string::npos) {
      test_cases.push_back(StripAllWhitespace(platforms_and_test[1]));
    }
  }

  return test_cases;
}

std::vector<std::string> GetPlatformIgnoredTests(const std::string& file_name) {
  base::FilePath file = GetTestFilePath(file_name);
  std::string contents;
  std::vector<std::string> platform_expectations;
  if (!base::ReadFileToString(file, &contents)) {
    return platform_expectations;
  }

  std::vector<std::string> file_lines = base::SplitString(
      contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto& line : file_lines) {
    if (line[0] == '#') {
      continue;
    }

    std::string platform;
    std::string expectation;
    std::string test_case;
    RE2::FullMatch(
        line, "crbug.com/\\d* \\[ (\\w*) \\] \\[ (\\w*) \\] ([\\w*,\\s*]*)",
        &platform, &expectation, &test_case);
    if (platform == kPlatformName) {
      if (expectation == "Skip") {
        platform_expectations.push_back(StripAllWhitespace(test_case));
      } else {
        NOTREACHED() << "Unsupported expectation " << expectation;
      }
    }
  }
  return platform_expectations;
}

std::vector<std::string> BuildAllPlatformTestCaseSet() {
  std::vector<std::string> test_cases_all =
      ReadTestInputFile(kTestCaseFilename);
  std::sort(test_cases_all.begin(), test_cases_all.end());

  std::vector<std::string> ignored_cases =
      GetPlatformIgnoredTests(kExpectationsFilename);
  std::sort(ignored_cases.begin(), ignored_cases.end());

  std::vector<std::string> final_tests(test_cases_all.size());
  auto iter = std::set_difference(test_cases_all.begin(), test_cases_all.end(),
                                  ignored_cases.begin(), ignored_cases.end(),
                                  final_tests.begin());
  final_tests.resize(iter - final_tests.begin());
  return final_tests;
}

}  // anonymous namespace

namespace web_app {

struct NavigateToSiteResult {
  content::WebContents* web_contents;
  webapps::TestAppBannerManagerDesktop* app_banner_manager;
  bool installable;
};

class WebAppIntegrationBrowserTest
    : public InProcessBrowserTest,
      public testing::WithParamInterface<std::string> {
 public:
  WebAppIntegrationBrowserTest()
      : https_server_(net::EmbeddedTestServer::TYPE_HTTPS) {}
  ~WebAppIntegrationBrowserTest() override = default;

  WebAppIntegrationBrowserTest(const WebAppIntegrationBrowserTest&) = delete;
  WebAppIntegrationBrowserTest& operator=(const WebAppIntegrationBrowserTest&) =
      delete;

  // InProcessBrowserTest
  void SetUp() override {
    https_server_.AddDefaultHandlers(GetChromeTestDataDir());
    ASSERT_TRUE(https_server_.Start());

    webapps::TestAppBannerManagerDesktop::SetUp();

    InProcessBrowserTest::SetUp();
  }

  // BrowserTestBase
  void SetUpOnMainThread() override {
    os_hooks_suppress_ =
        OsIntegrationManager::ScopedSuppressOsHooksForTesting();
    pwa_install_view_ =
        BrowserView::GetBrowserViewForBrowser(browser())
            ->toolbar_button_provider()
            ->GetPageActionIconView(PageActionIconType::kPwaInstall);
    ASSERT_TRUE(pwa_install_view_);
    EXPECT_FALSE(pwa_install_view_->GetVisible());
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitchASCII(
        network::switches::kUnsafelyTreatInsecureOriginAsSecure,
        GetInstallableAppURL().GetOrigin().spec());
  }

  // Test Framework
  void ParseParams() {
    std::string action_strings = GetParam();
    testing_actions_ = base::SplitString(
        action_strings, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  }

  // Non-assert actions implemented before assert actions. Implemented in
  // alphabetical order.
  void ExecuteAction(const std::string& action_string) {
    if (action_string == "add_policy_app_internal_tabbed") {
      AddPolicyAppInternal(base::Value(kDefaultLaunchContainerTabValue));
    } else if (action_string == "add_policy_app_internal_windowed") {
      AddPolicyAppInternal(base::Value(kDefaultLaunchContainerWindowValue));
    } else if (action_string == "close_pwa") {
      ClosePWA();
    } else if (action_string == "install_create_shortcut_tabbed") {
      InstallCreateShortcutTabbed();
    } else if (action_string == "install_omnibox_or_menu") {
      InstallOmniboxOrMenu();
    } else if (action_string == "launch_internal") {
      LaunchInternal();
    } else if (action_string == "list_apps_internal") {
      ListAppsInternal();
    } else if (action_string == "navigate_browser_in_scope") {
      NavigateToSite(browser(), GetInScopeURL());
    } else if (base::StartsWith(action_string, "navigate_installable")) {
      NavigateToSite(browser(), GetInstallableAppURL());
    } else if (action_string == "navigate_not_installable") {
      NavigateToSite(browser(), GetNonInstallableAppURL());
    } else if (action_string == "remove_policy_app") {
      RemovePolicyApp();
    } else if (base::StartsWith(action_string, "set_open_in_tab_internal")) {
      SetOpenInTabInternal();
    } else if (action_string == "set_open_in_window_internal") {
      SetOpenInWindowInternal();
    } else if (action_string == "uninstall_from_menu") {
      UninstallFromMenu();
    } else if (action_string == "uninstall_internal") {
      UninstallInternal();
    } else if (action_string == "assert_app_in_list_not_windowed") {
      AssertAppInListNotWindowed();
    } else if (action_string == "assert_app_not_in_list") {
      AssertAppNotInList();
    } else if (action_string == "assert_display_mode_standalone_internal") {
      AssertDisplayModeStandaloneInternal();
    } else if (action_string == "assert_display_mode_browser_internal") {
      AssertDisplayModeBrowserInternal();
    } else if (action_string == "assert_installable") {
      AssertInstallable();
    } else if (action_string == "assert_install_icon_shown") {
      AssertInstallIconShown();
    } else if (action_string == "assert_install_icon_not_shown") {
      AssertInstallIconNotShown();
    } else if (action_string == "assert_launch_icon_shown") {
      AssertLaunchIconShown();
    } else if (action_string == "assert_launch_icon_not_shown") {
      AssertLaunchIconNotShown();
    } else if (action_string == "assert_no_crash") {
    } else if (action_string == "assert_tab_created") {
      AssertTabCreated();
    } else if (action_string == "assert_window_created") {
      AssertWindowCreated();
    } else {
      FAIL() << "Unimplemented action: " << action_string;
    }
  }

  // Automated Testing Actions
  void AddPolicyAppInternal(base::Value default_launch_container) {
    GURL url = GetInstallableAppURL();
    auto* web_app_registrar =
        WebAppProvider::Get(profile())->registrar().AsWebAppRegistrar();
    base::RunLoop run_loop;
    WebAppInstallObserver observer(profile());
    observer.SetWebAppInstalledDelegate(
        base::BindLambdaForTesting([&](const AppId& app_id) {
          bool is_installed = web_app_registrar->IsInstalled(app_id);
          GURL installed_url = web_app_registrar->GetAppStartUrl(app_id);
          if (is_installed && installed_url.is_valid() &&
              installed_url.spec() == url.spec()) {
            app_id_ = app_id;
            run_loop.Quit();
          }
        }));
    {
      base::Value item(base::Value::Type::DICTIONARY);
      item.SetKey(kUrlKey, base::Value(url.spec()));
      item.SetKey(kDefaultLaunchContainerKey,
                  std::move(default_launch_container));
      ListPrefUpdate update(profile()->GetPrefs(),
                            prefs::kWebAppInstallForceList);
      update->Append(item.Clone());
    }
    run_loop.Run();
  }

  void ClosePWA() {
    DCHECK(app_browser_);
    app_browser_->window()->Close();
    ui_test_utils::WaitForBrowserToClose(app_browser_);
  }

  void InstallCreateShortcutTabbed() {
    chrome::SetAutoAcceptWebAppDialogForTesting(/*auto_accept=*/true,
                                                /*auto_open_in_window=*/false);
    WebAppInstallObserver observer(profile());
    CHECK(chrome::ExecuteCommand(browser(), IDC_CREATE_SHORTCUT));
    app_id_ = observer.AwaitNextInstall();
    chrome::SetAutoAcceptWebAppDialogForTesting(false, false);
  }

  web_app::AppId InstallOmniboxOrMenu() {
    chrome::SetAutoAcceptPWAInstallConfirmationForTesting(true);

    web_app::AppId app_id;
    base::RunLoop run_loop;
    web_app::SetInstalledCallbackForTesting(base::BindLambdaForTesting(
        [&app_id, &run_loop](const web_app::AppId& installed_app_id,
                             web_app::InstallResultCode code) {
          app_id = installed_app_id;
          run_loop.Quit();
        }));

    pwa_install_view()->ExecuteForTesting();

    run_loop.Run();

    chrome::SetAutoAcceptPWAInstallConfirmationForTesting(false);
    app_id_ = app_id;
    auto* browser_list = BrowserList::GetInstance();
    app_browser_ = browser_list->GetLastActive();
    DCHECK(AppBrowserController::IsWebApp(app_browser_));

    return app_id;
  }

  void LaunchInternal() {
    auto* web_app_provider = GetProvider();
    AppRegistrar& app_registrar = web_app_provider->registrar();
    DisplayMode display_mode =
        app_registrar.GetAppEffectiveDisplayMode(app_id_);
    if (display_mode == blink::mojom::DisplayMode::kStandalone) {
      app_browser_ = LaunchWebAppBrowserAndWait(
          ProfileManager::GetActiveUserProfile(), app_id_);
    } else {
      ui_test_utils::UrlLoadObserver url_observer(
          WebAppProviderBase::GetProviderBase(profile())
              ->registrar()
              .GetAppLaunchUrl(app_id_),
          content::NotificationService::AllSources());
      LaunchBrowserForWebAppInTab(profile(), app_id_);
      url_observer.Wait();
    }
  }

  void ListAppsInternal() {
    auto* web_app_registrar =
        WebAppProvider::Get(profile())->registrar().AsWebAppRegistrar();
    app_ids_ = web_app_registrar->GetAppIds();
  }

  NavigateToSiteResult NavigateToSite(Browser* browser, const GURL& url) {
    content::WebContents* web_contents = GetCurrentTab(browser);
    auto* app_banner_manager =
        webapps::TestAppBannerManagerDesktop::FromWebContents(web_contents);
    DCHECK(!app_banner_manager->WaitForInstallableCheck());

    ui_test_utils::NavigateToURL(browser, url);
    bool installable = app_banner_manager->WaitForInstallableCheck();

    last_navigation_result_ =
        NavigateToSiteResult{web_contents, app_banner_manager, installable};
    return last_navigation_result_;
  }

  void RemovePolicyApp() {
    GURL url = GetInstallableAppURL();
    base::RunLoop run_loop;
    WebAppInstallObserver observer(profile());
    observer.SetWebAppUninstalledDelegate(
        base::BindLambdaForTesting([&](const AppId& app_id) {
          if (app_id_ == app_id) {
            run_loop.Quit();
          }
        }));
    {
      ListPrefUpdate update(profile()->GetPrefs(),
                            prefs::kWebAppInstallForceList);
      update->EraseListValueIf([&](const base::Value& item) {
        const base::Value* url_value = item.FindKey(kUrlKey);
        return url_value && url_value->GetString() == url.spec();
      });
    }
    run_loop.Run();
  }

  void SetOpenInTabInternal() {
    auto& app_registry_controller =
        WebAppProvider::Get(profile())->registry_controller();
    app_registry_controller.SetAppUserDisplayMode(
        app_id_, blink::mojom::DisplayMode::kBrowser, true);
  }

  void SetOpenInWindowInternal() {
    auto& app_registry_controller =
        WebAppProvider::Get(profile())->registry_controller();
    app_registry_controller.SetAppUserDisplayMode(
        app_id_, blink::mojom::DisplayMode::kStandalone, true);
  }

  // TODO(https://crbug.com/1159651): Support this action on CrOS.
  void UninstallFromMenu() {
    DCHECK(app_browser_);
    base::RunLoop run_loop;
    WebAppInstallObserver observer(profile());
    observer.SetWebAppUninstalledDelegate(
        base::BindLambdaForTesting([&](const AppId& app_id) {
          if (app_id == app_id_) {
            run_loop.Quit();
          }
        }));

    extensions::ScopedTestDialogAutoConfirm auto_confirm(
        extensions::ScopedTestDialogAutoConfirm::ACCEPT);
    auto app_menu_model =
        std::make_unique<WebAppMenuModel>(/*provider=*/nullptr, app_browser_);
    app_menu_model->Init();
    ui::MenuModel* model = app_menu_model.get();
    int index = -1;
    const bool found = app_menu_model->GetModelAndIndexForCommandId(
        WebAppMenuModel::kUninstallAppCommandId, &model, &index);
    EXPECT_TRUE(found);
    EXPECT_TRUE(model->IsEnabledAt(index));

    app_menu_model->ExecuteCommand(WebAppMenuModel::kUninstallAppCommandId,
                                   /*event_flags=*/0);
    // The |app_menu_model| must be destroyed here, as the |run_loop| waits
    // until the app is fully uninstalled, which includes closing and deleting
    // the app_browser_.
    app_menu_model.reset();
    app_browser_ = nullptr;
    run_loop.Run();
  }

  void UninstallInternal() {
    WebAppProviderBase* const provider =
        WebAppProviderBase::GetProviderBase(profile());
    base::RunLoop run_loop;

    DCHECK(provider->install_finalizer().CanUserUninstallExternalApp(app_id_));
    provider->install_finalizer().UninstallExternalAppByUser(
        app_id_, base::BindLambdaForTesting([&](bool uninstalled) {
          EXPECT_TRUE(uninstalled);
          run_loop.Quit();
        }));

    run_loop.Run();
  }

  // Assert Actions
  void AssertAppInListNotWindowed() {
    EXPECT_TRUE(base::Contains(app_ids_, app_id_));
    WebAppProviderBase* const provider =
        WebAppProviderBase::GetProviderBase(browser()->profile());
    AppRegistrar& app_registrar = provider->registrar();
    DisplayMode display_mode = app_registrar.GetAppUserDisplayMode(app_id_);
    EXPECT_FALSE(display_mode == blink::mojom::DisplayMode::kStandalone);
  }
  void AssertAppNotInList() { EXPECT_FALSE(base::Contains(app_ids_, app_id_)); }

  void AssertDisplayModeStandaloneInternal() {
    EXPECT_EQ(GetProvider()->registrar().GetAppEffectiveDisplayMode(app_id_),
              DisplayMode::kStandalone);
  }
  void AssertDisplayModeBrowserInternal() {
    EXPECT_EQ(GetProvider()->registrar().GetAppEffectiveDisplayMode(app_id_),
              DisplayMode::kBrowser);
  }

  void AssertInstallable() { EXPECT_TRUE(last_navigation_result_.installable); }

  void AssertInstallIconShown() {
    EXPECT_EQ(GetAppMenuCommandState(IDC_INSTALL_PWA, browser()), kEnabled);
    EXPECT_TRUE(pwa_install_view()->GetVisible());
  }

  void AssertInstallIconNotShown() {
    EXPECT_EQ(GetAppMenuCommandState(IDC_INSTALL_PWA, browser()), kNotPresent);
    EXPECT_FALSE(pwa_install_view()->GetVisible());
  }

  void AssertLaunchIconShown() {
    EXPECT_EQ(GetAppMenuCommandState(IDC_OPEN_IN_PWA_WINDOW, browser()),
              kEnabled);
  }

  void AssertLaunchIconNotShown() {
    EXPECT_EQ(GetAppMenuCommandState(IDC_OPEN_IN_PWA_WINDOW, browser()),
              kNotPresent);
  }

  void AssertTabCreated() {
    auto* tab_strip_model = browser()->tab_strip_model();
    EXPECT_TRUE(tab_strip_model->count() == 2);
    auto* web_contents = tab_strip_model->GetActiveWebContents();
    const GURL& url = web_contents->GetLastCommittedURL();
    EXPECT_EQ(GetInstallableAppURL(), url);
  }

  void AssertWindowCreated() { EXPECT_TRUE(app_browser_); }

  GURL GetInstallableAppURL() {
    return https_server_.GetURL("/banners/manifest_test_page.html");
  }

  GURL GetNonInstallableAppURL() {
    return https_server_.GetURL("/banners/no_manifest_test_page.html");
  }

  GURL GetInScopeURL() {
    return https_server_.GetURL("/banners/manifest_test_page.html");
  }

  GURL GetOutOfScopeURL() {
    return https_server_.GetURL("/out_of_scope/index.html");
  }

  content::WebContents* GetCurrentTab(Browser* browser) {
    return browser->tab_strip_model()->GetActiveWebContents();
  }

  Profile* profile() { return browser()->profile(); }
  Browser* app_browser() { return app_browser_; }
  WebAppProvider* GetProvider() { return WebAppProvider::Get(profile()); }
  std::vector<std::string>& testing_actions() { return testing_actions_; }
  PageActionIconView* pwa_install_view() { return pwa_install_view_; }

 private:
  Browser* app_browser_ = nullptr;
  std::vector<AppId> app_ids_;
  std::vector<std::string> testing_actions_;
  NavigateToSiteResult last_navigation_result_;
  AppId app_id_;
  net::EmbeddedTestServer https_server_;
  PageActionIconView* pwa_install_view_ = nullptr;
  ScopedOsHooksSuppress os_hooks_suppress_;
};

// Tests that installing a PWA will cause the install icon to be hidden, and
// the launch icon to be shown.
IN_PROC_BROWSER_TEST_F(WebAppIntegrationBrowserTest,
                       InstallAndVerifyUIUpdates) {
  bool installable =
      NavigateToSite(browser(), GetInstallableAppURL()).installable;
  ASSERT_TRUE(installable);

  EXPECT_EQ(GetAppMenuCommandState(IDC_CREATE_SHORTCUT, browser()), kEnabled);
  EXPECT_EQ(GetAppMenuCommandState(IDC_INSTALL_PWA, browser()), kEnabled);
  EXPECT_TRUE(pwa_install_view()->GetVisible());
  EXPECT_EQ(GetAppMenuCommandState(IDC_OPEN_IN_PWA_WINDOW, browser()),
            kNotPresent);

  InstallOmniboxOrMenu();

  chrome::NewTab(browser());
  NavigateToSite(browser(), GetInstallableAppURL());
  EXPECT_EQ(GetAppMenuCommandState(IDC_INSTALL_PWA, browser()), kNotPresent);
  EXPECT_FALSE(pwa_install_view()->GetVisible());
  EXPECT_EQ(GetAppMenuCommandState(IDC_OPEN_IN_PWA_WINDOW, browser()),
            kEnabled);
}

IN_PROC_BROWSER_TEST_F(WebAppIntegrationBrowserTest, LaunchInternal) {
  auto* browser_list = BrowserList::GetInstance();
  EXPECT_EQ(1U, browser_list->size());
  EXPECT_FALSE(AppBrowserController::IsWebApp(browser_list->GetLastActive()));
  NavigateToSite(browser(), GetInstallableAppURL());
  InstallOmniboxOrMenu();
  EXPECT_EQ(2U, browser_list->size());
  EXPECT_TRUE(AppBrowserController::IsWebApp(browser_list->GetLastActive()));
  ClosePWA();
  EXPECT_EQ(1U, browser_list->size());
  EXPECT_FALSE(AppBrowserController::IsWebApp(browser_list->GetLastActive()));
  LaunchInternal();
  EXPECT_EQ(2U, browser_list->size());
  EXPECT_TRUE(AppBrowserController::IsWebApp(browser_list->GetLastActive()));
}

IN_PROC_BROWSER_TEST_P(WebAppIntegrationBrowserTest, Default) {
  ParseParams();

  for (auto& action : testing_actions()) {
    ExecuteAction(action);
  }
}

INSTANTIATE_TEST_SUITE_P(All,
                         WebAppIntegrationBrowserTest,
                         testing::ValuesIn(BuildAllPlatformTestCaseSet()));

}  // namespace web_app
