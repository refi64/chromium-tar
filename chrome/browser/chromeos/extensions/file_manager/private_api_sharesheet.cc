// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/file_manager/private_api_sharesheet.h"

#include "base/bind.h"
#include "chrome/browser/chromeos/file_manager/fileapi_util.h"
#include "chrome/browser/chromeos/fileapi/file_system_backend.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sharesheet/sharesheet_service.h"
#include "chrome/browser/sharesheet/sharesheet_service_factory.h"
#include "chrome/common/extensions/api/file_manager_private.h"
#include "chrome/common/extensions/api/file_manager_private_internal.h"
#include "components/services/app_service/public/cpp/intent_util.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/api/file_handlers/directory_util.h"
#include "extensions/browser/api/file_handlers/mime_util.h"
#include "storage/browser/file_system/file_system_context.h"
#include "storage/browser/file_system/file_system_url.h"

using storage::FileSystemURL;

namespace extensions {

FileManagerPrivateInternalSharesheetHasTargetsFunction::
    FileManagerPrivateInternalSharesheetHasTargetsFunction()
    : chrome_details_(this) {}

FileManagerPrivateInternalSharesheetHasTargetsFunction::
    ~FileManagerPrivateInternalSharesheetHasTargetsFunction() = default;

ExtensionFunction::ResponseAction
FileManagerPrivateInternalSharesheetHasTargetsFunction::Run() {
  using extensions::api::file_manager_private_internal::SharesheetHasTargets::
      Params;
  const std::unique_ptr<Params> params(Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  if (params->urls.empty())
    return RespondNow(Error("No URLs provided"));

  const scoped_refptr<storage::FileSystemContext> file_system_context =
      file_manager::util::GetFileSystemContextForRenderFrameHost(
          chrome_details_.GetProfile(), render_frame_host());

  std::vector<storage::FileSystemURL> file_system_urls;
  // Collect all the URLs, convert them to GURLs, and crack all the urls into
  // file paths.
  for (size_t i = 0; i < params->urls.size(); ++i) {
    const GURL url(params->urls[i]);
    storage::FileSystemURL file_system_url(file_system_context->CrackURL(url));
    if (!chromeos::FileSystemBackend::CanHandleURL(file_system_url))
      continue;
    urls_.push_back(url);
    file_system_urls.push_back(file_system_url);
  }

  mime_type_collector_ =
      std::make_unique<app_file_handler_util::MimeTypeCollector>(
          chrome_details_.GetProfile());
  mime_type_collector_->CollectForURLs(
      file_system_urls,
      base::BindOnce(&FileManagerPrivateInternalSharesheetHasTargetsFunction::
                         OnMimeTypesCollected,
                     this));
  return RespondLater();
}

void FileManagerPrivateInternalSharesheetHasTargetsFunction::
    OnMimeTypesCollected(std::unique_ptr<std::vector<std::string>> mime_types) {
  sharesheet::SharesheetService* sharesheet_service =
      sharesheet::SharesheetServiceFactory::GetForProfile(
          chrome_details_.GetProfile());

  bool result = false;

  if (!sharesheet_service) {
    LOG(ERROR) << "Couldn't get Sharesheet Service for profile";
    Respond(ArgumentList(extensions::api::file_manager_private_internal::
                             SharesheetHasTargets::Results::Create(result)));
  }

  result = sharesheet_service->HasShareTargets(
      apps_util::CreateShareIntentFromFiles(urls_, *mime_types));

  Respond(ArgumentList(extensions::api::file_manager_private_internal::
                           SharesheetHasTargets::Results::Create(result)));
}

FileManagerPrivateInternalInvokeSharesheetFunction::
    FileManagerPrivateInternalInvokeSharesheetFunction()
    : chrome_details_(this) {}

FileManagerPrivateInternalInvokeSharesheetFunction::
    ~FileManagerPrivateInternalInvokeSharesheetFunction() = default;

ExtensionFunction::ResponseAction
FileManagerPrivateInternalInvokeSharesheetFunction::Run() {
  using extensions::api::file_manager_private_internal::InvokeSharesheet::
      Params;
  const std::unique_ptr<Params> params(Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  if (params->urls.empty())
    return RespondNow(Error("No URLs provided"));

  const scoped_refptr<storage::FileSystemContext> file_system_context =
      file_manager::util::GetFileSystemContextForRenderFrameHost(
          chrome_details_.GetProfile(), render_frame_host());

  std::vector<storage::FileSystemURL> file_system_urls;
  // Collect all the URLs, convert them to GURLs, and crack all the urls into
  // file paths.
  for (size_t i = 0; i < params->urls.size(); ++i) {
    const GURL url(params->urls[i]);
    storage::FileSystemURL file_system_url(file_system_context->CrackURL(url));
    if (!chromeos::FileSystemBackend::CanHandleURL(file_system_url))
      continue;
    urls_.push_back(url);
    file_system_urls.push_back(file_system_url);
  }

  mime_type_collector_ =
      std::make_unique<app_file_handler_util::MimeTypeCollector>(
          chrome_details_.GetProfile());
  mime_type_collector_->CollectForURLs(
      file_system_urls,
      base::BindOnce(&FileManagerPrivateInternalInvokeSharesheetFunction::
                         OnMimeTypesCollected,
                     this));

  return RespondLater();
}

void FileManagerPrivateInternalInvokeSharesheetFunction::OnMimeTypesCollected(
    std::unique_ptr<std::vector<std::string>> mime_types) {
  // On button press show sharesheet bubble.
  auto* profile = chrome_details_.GetProfile();
  sharesheet::SharesheetService* sharesheet_service =
      sharesheet::SharesheetServiceFactory::GetForProfile(profile);
  if (!sharesheet_service) {
    Respond(Error("Cannot find sharesheet service"));
    return;
  }
  sharesheet_service->ShowBubble(
      GetSenderWebContents(),
      apps_util::CreateShareIntentFromFiles(urls_, *mime_types));

  Respond(NoArguments());
}

}  // namespace extensions
