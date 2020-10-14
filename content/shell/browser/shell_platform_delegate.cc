// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/shell_platform_delegate.h"

#include "content/public/browser/javascript_dialog_manager.h"

namespace content {

void ShellPlatformDelegate::DidCreateOrAttachWebContents(
    Shell* shell,
    WebContents* web_contents) {}

std::unique_ptr<JavaScriptDialogManager>
ShellPlatformDelegate::CreateJavaScriptDialogManager(Shell* shell) {
  return nullptr;
}

std::unique_ptr<BluetoothChooser> ShellPlatformDelegate::RunBluetoothChooser(
    Shell* shell,
    RenderFrameHost* frame,
    const BluetoothChooser::EventHandler& event_handler) {
  return nullptr;
}

bool ShellPlatformDelegate::ShouldAllowRunningInsecureContent(Shell* shell) {
  return false;
}

}  // namespace content
