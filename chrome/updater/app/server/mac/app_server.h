// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_UPDATER_APP_SERVER_MAC_APP_SERVER_H_
#define CHROME_UPDATER_APP_SERVER_MAC_APP_SERVER_H_

#include "base/memory/scoped_refptr.h"

#include <xpc/xpc.h>

#include "base/atomic_ref_count.h"
#include "base/mac/scoped_nsobject.h"
#include "base/memory/ref_counted.h"
#include "chrome/updater/app/app_server.h"
#include "chrome/updater/app/server/mac/service_delegate.h"
#import "chrome/updater/configurator.h"
#import "chrome/updater/mac/setup/info_plist.h"
#import "chrome/updater/mac/xpc_service_names.h"
#include "chrome/updater/prefs.h"
#include "chrome/updater/update_service_in_process.h"

namespace updater {

class AppServerMac : public AppServer {
 public:
  AppServerMac();
  void TaskStarted();
  void TaskCompleted();

 protected:
  // Overrides of App.
  void Uninitialize() override;

 private:
  ~AppServerMac() override;

  // Overrides of AppServer.
  void ActiveDuty() override;
  bool SwapRPCInterfaces() override;
  void UninstallSelf() override;

  void MarkTaskStarted();
  void AcknowledgeTaskCompletion();

  base::scoped_nsobject<CRUUpdateCheckXPCServiceDelegate>
      update_check_delegate_;
  base::scoped_nsobject<NSXPCListener> update_check_listener_;
  base::scoped_nsobject<CRUAdministrationXPCServiceDelegate>
      administration_delegate_;
  base::scoped_nsobject<NSXPCListener> administration_listener_;
  int tasks_running_ = 0;
};

}  // namespace updater

#endif  // CHROME_UPDATER_APP_SERVER_MAC_APP_SERVER_H_