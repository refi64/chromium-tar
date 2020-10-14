// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/sync_invalidations_service_factory.h"

#include "chrome/browser/gcm/gcm_profile_service_factory.h"
#include "chrome/browser/gcm/instance_id/instance_id_profile_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/gcm_driver/gcm_profile_service.h"
#include "components/gcm_driver/instance_id/instance_id_profile_service.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/sync/invalidations/switches.h"
#include "components/sync/invalidations/sync_invalidations_service_impl.h"

// TODO(crbug.com/1082115): change to real sync sender id: 8181035976.
constexpr char kDefaultSenderId[] = "361488507004";
constexpr char kDefaultApplicationId[] = "com.google.chrome.sync.invalidations";

syncer::SyncInvalidationsService*
SyncInvalidationsServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<syncer::SyncInvalidationsService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

SyncInvalidationsServiceFactory*
SyncInvalidationsServiceFactory::GetInstance() {
  static base::NoDestructor<SyncInvalidationsServiceFactory> instance;
  return instance.get();
}

SyncInvalidationsServiceFactory::SyncInvalidationsServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "SyncInvalidationsService",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(gcm::GCMProfileServiceFactory::GetInstance());
  DependsOn(instance_id::InstanceIDProfileServiceFactory::GetInstance());
}

SyncInvalidationsServiceFactory::~SyncInvalidationsServiceFactory() = default;

KeyedService* SyncInvalidationsServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  if (!base::FeatureList::IsEnabled(switches::kSubscribeForSyncInvalidations)) {
    return nullptr;
  }

  Profile* profile = Profile::FromBrowserContext(context);

  gcm::GCMDriver* gcm_driver =
      gcm::GCMProfileServiceFactory::GetForProfile(profile)->driver();
  instance_id::InstanceIDDriver* instance_id_driver =
      instance_id::InstanceIDProfileServiceFactory::GetForProfile(profile)
          ->driver();
  return new syncer::SyncInvalidationsServiceImpl(
      gcm_driver, instance_id_driver, kDefaultSenderId, kDefaultApplicationId);
}
