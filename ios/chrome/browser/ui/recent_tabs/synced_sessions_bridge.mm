// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/recent_tabs/synced_sessions_bridge.h"

#include "base/bind.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/sync_sessions/session_sync_service.h"
#include "ios/chrome/browser/browser_state/chrome_browser_state.h"
#include "ios/chrome/browser/signin/identity_manager_factory.h"
#include "ios/chrome/browser/sync/session_sync_service_factory.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace synced_sessions {

#pragma mark - SyncedSessionsObserverBridge

SyncedSessionsObserverBridge::SyncedSessionsObserverBridge(
    id<SyncedSessionsObserver> owner,
    ChromeBrowserState* browserState)
    : owner_(owner),
      identity_manager_(
          IdentityManagerFactory::GetForBrowserState(browserState)),
      identity_manager_observer_(this) {
  identity_manager_observer_.Add(identity_manager_);

  // base::Unretained() is safe below because the subscription itself is a class
  // member field and handles destruction well.
  foreign_session_updated_subscription_ =
      SessionSyncServiceFactory::GetForBrowserState(browserState)
          ->SubscribeToForeignSessionsChanged(base::BindRepeating(
              &SyncedSessionsObserverBridge::OnForeignSessionChanged,
              base::Unretained(this)));
}

SyncedSessionsObserverBridge::~SyncedSessionsObserverBridge() {}

#pragma mark - signin::IdentityManager::Observer

void SyncedSessionsObserverBridge::OnPrimaryAccountChanged(
    const signin::PrimaryAccountChangeEvent& event) {
  switch (event.GetEventTypeFor(signin::ConsentLevel::kSync)) {
    case signin::PrimaryAccountChangeEvent::Type::kSet:
      // Ignored.
      break;
    case signin::PrimaryAccountChangeEvent::Type::kCleared:
      [owner_ reloadSessions];
      break;
    case signin::PrimaryAccountChangeEvent::Type::kNone:
      NOTREACHED() << "ConsentLevel::kNotRequired is not yet supported on iOS. "
                      "This code needs to be updated when it is supported.";
      break;
  }
}

#pragma mark - Signin and syncing status

bool SyncedSessionsObserverBridge::IsSignedIn() {
  return identity_manager_->HasPrimaryAccount();
}

void SyncedSessionsObserverBridge::OnForeignSessionChanged() {
  [owner_ reloadSessions];
}

}  // namespace synced_sessions
