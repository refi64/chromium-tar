// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/os_integration_manager.h"

#include <utility>

#include "base/bind.h"
#include "base/feature_list.h"
#include "base/optional.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/components/app_shortcut_manager.h"
#include "chrome/browser/web_applications/components/file_handler_manager.h"
#include "chrome/browser/web_applications/components/web_app_ui_manager.h"
#include "chrome/common/chrome_features.h"
#include "content/public/browser/browser_thread.h"

#if defined(OS_MAC)
#include "chrome/browser/web_applications/components/app_shim_registry_mac.h"
#endif

namespace web_app {

// This is adapted from base/barrier_closure.cc. os_hooks_results is maintained
// to track install results from different OS hooks callers
class OsHooksBarrierInfo {
 public:
  explicit OsHooksBarrierInfo(InstallOsHooksCallback done_callback)
      : done_callback_(std::move(done_callback)) {}

  void Run(OsHookType::Type os_hook, bool completed) {
    DCHECK(!os_hooks_called_[os_hook]);

    os_hooks_called_[os_hook] = true;
    os_hooks_results_[os_hook] = completed;

    if (os_hooks_called_.all()) {
      std::move(done_callback_).Run(os_hooks_results_);
    }
  }

 private:
  OsHooksResults os_hooks_results_{false};
  OsHooksResults os_hooks_called_{false};
  InstallOsHooksCallback done_callback_;
};

OsIntegrationManager::OsIntegrationManager(Profile* profile)
    : profile_(profile) {}

OsIntegrationManager::~OsIntegrationManager() = default;

void OsIntegrationManager::SetSubsystems(
    AppRegistrar* registrar,
    AppShortcutManager* shortcut_manager,
    FileHandlerManager* file_handler_manager,
    WebAppUiManager* ui_manager) {
  registrar_ = registrar;
  shortcut_manager_ = shortcut_manager;
  file_handler_manager_ = file_handler_manager;
  ui_manager_ = ui_manager;
}

void OsIntegrationManager::Start() {
  DCHECK(registrar_);

#if defined(OS_MAC)
  // Ensure that all installed apps are included in the AppShimRegistry when the
  // profile is loaded. This is redundant, because apps are registered when they
  // are installed. It is necessary, however, because app registration was added
  // long after app installation launched. This should be removed after shipping
  // for a few versions (whereupon it may be assumed that most applications have
  // been registered).
  std::vector<AppId> app_ids = registrar_->GetAppIds();
  for (const auto& app_id : app_ids) {
    AppShimRegistry::Get()->OnAppInstalledForProfile(app_id,
                                                     profile_->GetPath());
  }
#endif
}

void OsIntegrationManager::InstallOsHooks(
    const AppId& app_id,
    InstallOsHooksCallback callback,
    std::unique_ptr<WebApplicationInfo> web_app_info,
    InstallOsHooksOptions options) {
  DCHECK(shortcut_manager_);

  if (suppress_os_hooks_for_testing_) {
    OsHooksResults os_hooks_results{true};
    std::move(callback).Run(os_hooks_results);
    return;
  }

#if defined(OS_MAC)
  AppShimRegistry::Get()->OnAppInstalledForProfile(app_id, profile_->GetPath());
#endif

  // Note: This barrier protects against multiple calls on the same type, but
  // it doesn't protect against the case where we fail to call Run / create a
  // callback for every type. Developers should double check that Run is
  // called for every OsHookType::Type. If there is any missing type, the
  // InstallOsHooksCallback will not get run.
  base::RepeatingCallback<void(OsHookType::Type os_hook, bool completed)>
      barrier = base::BindRepeating(
          &OsHooksBarrierInfo::Run,
          base::Owned(new OsHooksBarrierInfo(std::move(callback))));

  // TODO(ortuno): Make adding a shortcut to the applications menu independent
  // from adding a shortcut to desktop.
  if (options.add_to_applications_menu &&
      shortcut_manager_->CanCreateShortcuts()) {
    shortcut_manager_->CreateShortcuts(
        app_id, options.add_to_desktop,
        base::BindOnce(&OsIntegrationManager::OnShortcutsCreated,
                       weak_ptr_factory_.GetWeakPtr(), app_id,
                       std::move(web_app_info), std::move(options), barrier));
  } else {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&OsIntegrationManager::OnShortcutsCreated,
                       weak_ptr_factory_.GetWeakPtr(), app_id,
                       std::move(web_app_info), std::move(options), barrier,
                       /*shortcuts_created=*/false));
  }
}

void OsIntegrationManager::UninstallOsHooks(const AppId& app_id,
                                            UninstallOsHooksCallback callback) {
  DCHECK(shortcut_manager_);

  if (suppress_os_hooks_for_testing_)
    return;

  base::RepeatingCallback<void(OsHookType::Type os_hook, bool completed)>
      barrier = base::BindRepeating(
          &OsHooksBarrierInfo::Run,
          base::Owned(new OsHooksBarrierInfo(std::move(callback))));

  if (ShouldRegisterShortcutsMenuWithOs()) {
    barrier.Run(OsHookType::kShortcutsMenu,
                UnregisterShortcutsMenuWithOs(app_id, profile_->GetPath()));
  } else {
    barrier.Run(OsHookType::kShortcutsMenu, /*completed=*/true);
  }

  std::unique_ptr<ShortcutInfo> shortcut_info =
      shortcut_manager_->BuildShortcutInfo(app_id);
  base::FilePath shortcut_data_dir =
      internals::GetShortcutDataDir(*shortcut_info);

  if (base::FeatureList::IsEnabled(features::kDesktopPWAsRunOnOsLogin)) {
    ScheduleUnregisterRunOnOsLogin(
        shortcut_info->profile_path, shortcut_info->title,
        base::BindOnce(barrier, OsHookType::kRunOnOsLogin));
  }

  internals::ScheduleDeletePlatformShortcuts(
      shortcut_data_dir, std::move(shortcut_info),
      base::BindOnce(barrier, OsHookType::kShortcuts));

  // TODO(https://crbug.com/1108109) we should return the result of file handler
  // unregistration and record errors during unregistration.
  file_handler_manager_->DisableAndUnregisterOsFileHandlers(app_id);
  barrier.Run(OsHookType::kFileHandlers, /*completed=*/true);

  DeleteSharedAppShims(app_id);
}

void OsIntegrationManager::SuppressOsHooksForTesting() {
  suppress_os_hooks_for_testing_ = true;
}

void OsIntegrationManager::UpdateOsHooks(
    const AppId& app_id,
    base::StringPiece old_name,
    const WebApplicationInfo& web_app_info) {
  DCHECK(shortcut_manager_);

  // TODO(crbug.com/1079439): Update file handlers.
  shortcut_manager_->UpdateShortcuts(app_id, old_name);
  if (base::FeatureList::IsEnabled(
          features::kDesktopPWAsAppIconShortcutsMenu) &&
      !web_app_info.shortcuts_menu_item_infos.empty()) {
    shortcut_manager_->RegisterShortcutsMenuWithOs(
        app_id, web_app_info.shortcuts_menu_item_infos,
        web_app_info.shortcuts_menu_icons_bitmaps);
  } else {
    // Unregister shortcuts menu when feature is disabled or
    // shortcuts_menu_item_infos is empty.
    shortcut_manager_->UnregisterShortcutsMenuWithOs(app_id);
  }
}

void OsIntegrationManager::OnShortcutsCreated(
    const AppId& app_id,
    std::unique_ptr<WebApplicationInfo> web_app_info,
    InstallOsHooksOptions options,
    base::RepeatingCallback<void(OsHookType::Type os_hook, bool created)>
        barrier_callback,
    bool shortcuts_created) {
  DCHECK(file_handler_manager_);
  DCHECK(ui_manager_);

  barrier_callback.Run(OsHookType::kShortcuts, /*completed=*/true);

  // TODO(crbug.com/1087219): callback should be run after all hooks are
  // deployed, need to refactor filehandler to allow this.
  file_handler_manager_->EnableAndRegisterOsFileHandlers(app_id);
  barrier_callback.Run(OsHookType::kFileHandlers, /*completed=*/true);

  if (options.add_to_quick_launch_bar &&
      ui_manager_->CanAddAppToQuickLaunchBar()) {
    ui_manager_->AddAppToQuickLaunchBar(app_id);
  }
  if (shortcuts_created && base::FeatureList::IsEnabled(
                               features::kDesktopPWAsAppIconShortcutsMenu)) {
    if (web_app_info) {
      if (web_app_info->shortcuts_menu_item_infos.empty()) {
        barrier_callback.Run(OsHookType::kShortcutsMenu, /*completed=*/false);
      } else {
        shortcut_manager_->RegisterShortcutsMenuWithOs(
            app_id, web_app_info->shortcuts_menu_item_infos,
            web_app_info->shortcuts_menu_icons_bitmaps);
        // TODO(https://crbug.com/1098471): fix RegisterShortcutsMenuWithOs to
        // take callback.
        barrier_callback.Run(OsHookType::kShortcutsMenu, /*completed=*/true);
      }
    } else {
      shortcut_manager_->ReadAllShortcutsMenuIconsAndRegisterShortcutsMenu(
          app_id, base::BindOnce(barrier_callback, OsHookType::kShortcutsMenu));
    }
  } else {
    barrier_callback.Run(OsHookType::kShortcutsMenu, /*completed=*/false);
  }

  if (base::FeatureList::IsEnabled(features::kDesktopPWAsRunOnOsLogin) &&
      options.run_on_os_login) {
    // TODO(crbug.com/897302): Implement Run on OS Login mode selection.
    // Currently it is set to be the default: RunOnOsLoginMode::kWindowed
    RegisterRunOnOsLogin(
        app_id, base::BindOnce(barrier_callback, OsHookType::kRunOnOsLogin));
  } else {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(barrier_callback, OsHookType::kRunOnOsLogin, false));
  }
}

void OsIntegrationManager::DeleteSharedAppShims(const AppId& app_id) {
#if defined(OS_MAC)
  bool delete_multi_profile_shortcuts =
      AppShimRegistry::Get()->OnAppUninstalledForProfile(app_id,
                                                         profile_->GetPath());
  if (delete_multi_profile_shortcuts) {
    web_app::internals::GetShortcutIOTaskRunner()->PostTask(
        FROM_HERE,
        base::BindOnce(&web_app::internals::DeleteMultiProfileShortcutsForApp,
                       app_id));
  }
#endif
}

void OsIntegrationManager::RegisterRunOnOsLogin(
    const AppId& app_id,
    RegisterRunOnOsLoginCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  shortcut_manager_->GetShortcutInfoForApp(
      app_id,
      base::BindOnce(
          &OsIntegrationManager::OnShortcutInfoRetrievedRegisterRunOnOsLogin,
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void OsIntegrationManager::OnShortcutInfoRetrievedRegisterRunOnOsLogin(
    RegisterRunOnOsLoginCallback callback,
    std::unique_ptr<ShortcutInfo> info) {
  ScheduleRegisterRunOnOsLogin(std::move(info), std::move(callback));
}

}  // namespace web_app
