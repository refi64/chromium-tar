// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file implements common select dialog functionality between GTK and KDE.

#include "ui/gtk/select_file_dialog_impl.h"

#include "base/environment.h"
#include "base/feature_list.h"
#include "base/files/file_util.h"
#include "base/nix/xdg_util.h"
#include "base/notreached.h"
#include "base/threading/thread_restrictions.h"
#include "content/public/common/content_features.h"
#include "ui/gtk/select_file_dialog_impl_portal.h"

namespace {

enum FileDialogChoiceStatus { kUnknown, kGtk, kKde, kPortal };

FileDialogChoiceStatus dialog_status_ = kUnknown;

}  // namespace

namespace gtk {

base::FilePath* SelectFileDialogImpl::last_saved_path_ = nullptr;
base::FilePath* SelectFileDialogImpl::last_opened_path_ = nullptr;

// static
void SelectFileDialogImpl::InitializeFactory() {
  if (dialog_status_ != kUnknown) {
    return;
  }

  // Start out assumimg we are going to use GTK.
  dialog_status_ = kGtk;

  // Check to see if the portal is available.
  if (base::FeatureList::IsEnabled(features::kXdgFileChooserPortal) &&
      SelectFileDialogImplPortal::IsPortalAvailable()) {
    dialog_status_ = kPortal;
  } else {
    // Make sure to kill the portal connection.
    SelectFileDialogImplPortal::DestroyPortalConnection();

    // Check to see if KDE is the desktop environment.
    std::unique_ptr<base::Environment> env(base::Environment::Create());
    base::nix::DesktopEnvironment desktop =
        base::nix::GetDesktopEnvironment(env.get());
    if (desktop == base::nix::DESKTOP_ENVIRONMENT_KDE3 ||
        desktop == base::nix::DESKTOP_ENVIRONMENT_KDE4 ||
        desktop == base::nix::DESKTOP_ENVIRONMENT_KDE5) {
      // Check to see if the user dislikes the KDE file dialog.
      if (!env->HasVar("NO_CHROME_KDE_FILE_DIALOG")) {
        // Check to see if the KDE dialog works.
        if (SelectFileDialogImpl::CheckKDEDialogWorksOnUIThread()) {
          dialog_status_ = kKde;
        }
      }
    }
  }
}

// static
void SelectFileDialogImpl::DestroyFactory() {
  if (dialog_status_ == kPortal) {
    SelectFileDialogImplPortal::DestroyPortalConnection();
  }
}

// static
ui::SelectFileDialog* SelectFileDialogImpl::Create(
    ui::SelectFileDialog::Listener* listener,
    std::unique_ptr<ui::SelectFilePolicy> policy) {
  switch (dialog_status_) {
    case kGtk:
      return SelectFileDialogImpl::NewSelectFileDialogImplGTK(
          listener, std::move(policy));
    case kPortal:
      return SelectFileDialogImpl::NewSelectFileDialogImplPortal(
          listener, std::move(policy));
    case kKde: {
      std::unique_ptr<base::Environment> env(base::Environment::Create());
      base::nix::DesktopEnvironment desktop =
          base::nix::GetDesktopEnvironment(env.get());
      return SelectFileDialogImpl::NewSelectFileDialogImplKDE(
          listener, std::move(policy), desktop);
    }
    case kUnknown:
      CHECK(false) << "InitializeFactory was never called";
      return nullptr;
  }
}

SelectFileDialogImpl::SelectFileDialogImpl(
    Listener* listener,
    std::unique_ptr<ui::SelectFilePolicy> policy)
    : SelectFileDialog(listener, std::move(policy)),
      file_type_index_(0),
      type_(SELECT_NONE) {
  if (!last_saved_path_) {
    last_saved_path_ = new base::FilePath();
    last_opened_path_ = new base::FilePath();
  }
}

SelectFileDialogImpl::~SelectFileDialogImpl() {}

void SelectFileDialogImpl::ListenerDestroyed() {
  listener_ = nullptr;
}

bool SelectFileDialogImpl::CallDirectoryExistsOnUIThread(
    const base::FilePath& path) {
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  return base::DirectoryExists(path);
}

}  // namespace gtk
