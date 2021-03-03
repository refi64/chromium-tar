// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FILE_SYSTEM_ACCESS_MOCK_FILE_SYSTEM_ACCESS_PERMISSION_CONTEXT_H_
#define CONTENT_BROWSER_FILE_SYSTEM_ACCESS_MOCK_FILE_SYSTEM_ACCESS_PERMISSION_CONTEXT_H_

#include "base/files/file_path.h"
#include "content/public/browser/file_system_access_permission_context.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace content {
// Mock FileSystemAccessPermissionContext implementation.
class MockFileSystemAccessPermissionContext
    : public FileSystemAccessPermissionContext {
 public:
  MockFileSystemAccessPermissionContext();
  ~MockFileSystemAccessPermissionContext() override;

  MOCK_METHOD4(GetReadPermissionGrant,
               scoped_refptr<FileSystemAccessPermissionGrant>(
                   const url::Origin& origin,
                   const base::FilePath& path,
                   HandleType handle_type,
                   FileSystemAccessPermissionContext::UserAction user_action));

  MOCK_METHOD4(GetWritePermissionGrant,
               scoped_refptr<FileSystemAccessPermissionGrant>(
                   const url::Origin& origin,
                   const base::FilePath& path,
                   HandleType handle_type,
                   FileSystemAccessPermissionContext::UserAction user_action));

  void ConfirmSensitiveDirectoryAccess(
      const url::Origin& origin,
      PathType path_type,
      const base::FilePath& path,
      HandleType handle_type,
      GlobalFrameRoutingId frame_id,
      base::OnceCallback<void(SensitiveDirectoryResult)> callback) override;
  MOCK_METHOD6(
      ConfirmSensitiveDirectoryAccess_,
      void(const url::Origin& origin,
           PathType path_type,
           const base::FilePath& path,
           HandleType handle_type,
           GlobalFrameRoutingId frame_id,
           base::OnceCallback<void(SensitiveDirectoryResult)>& callback));

  void PerformAfterWriteChecks(
      std::unique_ptr<FileSystemAccessWriteItem> item,
      GlobalFrameRoutingId frame_id,
      base::OnceCallback<void(AfterWriteCheckResult)> callback) override;
  MOCK_METHOD3(PerformAfterWriteChecks_,
               void(FileSystemAccessWriteItem* item,
                    GlobalFrameRoutingId frame_id,
                    base::OnceCallback<void(AfterWriteCheckResult)>& callback));

  MOCK_METHOD1(CanObtainReadPermission, bool(const url::Origin& origin));
  MOCK_METHOD1(CanObtainWritePermission, bool(const url::Origin& origin));

  MOCK_METHOD3(SetLastPickedDirectory,
               void(const url::Origin& origin,
                    const base::FilePath& path,
                    const PathType type));
  MOCK_METHOD1(GetLastPickedDirectory, PathInfo(const url::Origin& origin));

  MOCK_METHOD1(GetCommonDirectoryPath,
               base::FilePath(blink::mojom::CommonDirectory directory));
};

}  // namespace content

#endif  // CONTENT_BROWSER_FILE_SYSTEM_ACCESS_MOCK_FILE_SYSTEM_ACCESS_PERMISSION_CONTEXT_H_
