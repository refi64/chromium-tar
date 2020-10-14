// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FEDERATED_LEARNING_FLOC_ID_PROVIDER_IMPL_H_
#define CHROME_BROWSER_FEDERATED_LEARNING_FLOC_ID_PROVIDER_IMPL_H_

#include "base/gtest_prod_util.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/timer/timer.h"
#include "chrome/browser/federated_learning/floc_id_provider.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_service_observer.h"
#include "components/sync/driver/sync_service_observer.h"

namespace content_settings {
class CookieSettings;
}

namespace syncer {
class UserEventService;
}

namespace federated_learning {

class FlocRemotePermissionService;

// A service that regularly computes the floc id and logs it in a user event. A
// computed floc can be in either a valid or invalid state, based on whether all
// the prerequisites are met:
// 1) Sync & sync-history are enabled.
// 2) 3rd party cookies are NOT blocked.
// 3) Supplemental Web and App Activity is enabled.
// 4) Supplemental Ad Personalization is enabled.
// 5) The account type is NOT a child account.
//
// When all the prerequisites are met, the floc will be computed by sim-hashing
// navigation URL domains in the last 7 days; otherwise, an invalid floc will be
// given.
//
// The floc will be first computed after sync & sync-history are enabled. After
// each computation, another computation will be scheduled 24 hours later. In
// the event of history deletion, the floc will be recomputed immediately and
// reset the timer of any currently scheduled computation to be 24 hours later.
class FlocIdProviderImpl : public FlocIdProvider,
                           public history::HistoryServiceObserver,
                           public syncer::SyncServiceObserver {
 public:
  enum class ComputeFlocTrigger {
    kBrowserStart,
    kScheduledUpdate,
    kHistoryDelete,
  };

  using CanComputeFlocCallback = base::OnceCallback<void(bool)>;
  using ComputeFlocCompletedCallback = base::OnceCallback<void(FlocId)>;
  using GetRecentlyVisitedURLsCallback =
      history::HistoryService::QueryHistoryCallback;

  FlocIdProviderImpl(
      syncer::SyncService* sync_service,
      scoped_refptr<content_settings::CookieSettings> cookie_settings,
      FlocRemotePermissionService* floc_remote_permission_service,
      history::HistoryService* history_service,
      syncer::UserEventService* user_event_service);
  ~FlocIdProviderImpl() override;
  FlocIdProviderImpl(const FlocIdProviderImpl&) = delete;
  FlocIdProviderImpl& operator=(const FlocIdProviderImpl&) = delete;

 protected:
  // protected virtual for testing.
  virtual void NotifyFlocUpdated(ComputeFlocTrigger trigger);
  virtual bool IsSyncHistoryEnabled();
  virtual bool AreThirdPartyCookiesAllowed();
  virtual void IsSwaaNacAccountEnabled(CanComputeFlocCallback callback);

 private:
  friend class FlocIdProviderUnitTest;
  friend class FlocIdProviderBrowserTest;

  // KeyedService:
  void Shutdown() override;

  // history::HistoryServiceObserver
  //
  // On history deletion, recompute the floc if the current floc is speculated
  // to be derived from the deleted history.
  void OnURLsDeleted(history::HistoryService* history_service,
                     const history::DeletionInfo& deletion_info) override;

  // syncer::SyncServiceObserver:
  void OnStateChanged(syncer::SyncService* sync_service) override;

  void ComputeFloc(ComputeFlocTrigger trigger);
  void OnComputeFlocCompleted(ComputeFlocTrigger trigger, FlocId floc_id);

  void CheckCanComputeFloc(CanComputeFlocCallback callback);
  void OnCheckCanComputeFlocCompleted(ComputeFlocCompletedCallback callback,
                                      bool can_compute_floc);

  void OnCheckSwaaNacAccountEnabledCompleted(CanComputeFlocCallback callback,
                                             bool enabled);

  void GetRecentlyVisitedURLs(GetRecentlyVisitedURLsCallback callback);
  void OnGetRecentlyVisitedURLsCompleted(ComputeFlocCompletedCallback callback,
                                         history::QueryResults results);

  FlocId floc_id_;
  bool floc_computation_in_progress_ = false;
  bool first_floc_computation_triggered_ = false;

  // For the swaa/nac/account_type permission, we will use a cached status to
  // avoid querying too often.
  bool cached_swaa_nac_account_enabled_ = false;
  base::TimeTicks last_swaa_nac_account_enabled_query_time_;

  syncer::SyncService* sync_service_;
  scoped_refptr<content_settings::CookieSettings> cookie_settings_;
  FlocRemotePermissionService* floc_remote_permission_service_;
  history::HistoryService* history_service_;
  syncer::UserEventService* user_event_service_;

  // Used for the async tasks querying the HistoryService.
  base::CancelableTaskTracker history_task_tracker_;

  // The timer used to schedule a floc computation.
  base::OneShotTimer compute_floc_timer_;

  base::WeakPtrFactory<FlocIdProviderImpl> weak_ptr_factory_{this};
};

}  // namespace federated_learning

#endif  // CHROME_BROWSER_FEDERATED_LEARNING_FLOC_ID_PROVIDER_IMPL_H_
