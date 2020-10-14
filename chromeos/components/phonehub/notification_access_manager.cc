// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/phonehub/notification_access_manager.h"

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "chromeos/components/multidevice/logging/logging.h"

namespace chromeos {
namespace phonehub {

NotificationAccessManager::NotificationAccessManager() = default;

NotificationAccessManager::~NotificationAccessManager() = default;

std::unique_ptr<NotificationAccessSetupOperation>
NotificationAccessManager::AttemptNotificationSetup(
    NotificationAccessSetupOperation::Delegate* delegate) {
  if (HasAccessBeenGranted())
    return nullptr;

  int operation_id = next_operation_id_;
  ++next_operation_id_;

  auto operation = base::WrapUnique(new NotificationAccessSetupOperation(
      delegate,
      base::BindOnce(&NotificationAccessManager::OnSetupOperationDeleted,
                     weak_ptr_factory_.GetWeakPtr(), operation_id)));
  id_to_operation_map_.emplace(operation_id, operation.get());

  OnSetupAttemptStarted();
  return operation;
}

void NotificationAccessManager::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void NotificationAccessManager::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void NotificationAccessManager::NotifyNotificationAccessChanged() {
  for (auto& observer : observer_list_)
    observer.OnNotificationAccessChanged();
}

void NotificationAccessManager::SetNotificationSetupOperationStatus(
    NotificationAccessSetupOperation::Status new_status) {
  DCHECK(IsSetupOperationInProgress());

  PA_LOG(INFO) << "Notification access setup flow - new status: " << new_status;

  for (auto& it : id_to_operation_map_)
    it.second->NotifyStatusChanged(new_status);

  if (NotificationAccessSetupOperation::IsFinalStatus(new_status))
    id_to_operation_map_.clear();
}

bool NotificationAccessManager::IsSetupOperationInProgress() const {
  return !id_to_operation_map_.empty();
}

void NotificationAccessManager::OnSetupOperationDeleted(int operation_id) {
  auto it = id_to_operation_map_.find(operation_id);
  if (it == id_to_operation_map_.end())
    return;

  id_to_operation_map_.erase(it);
  if (id_to_operation_map_.empty())
    OnSetupAttemptEnded();
}

}  // namespace phonehub
}  // namespace chromeos
