// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/cfm/public/cpp/service_connection.h"

#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/macros.h"
#include "base/optional.h"
#include "base/run_loop.h"
#include "base/test/bind_test_util.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "chromeos/dbus/cfm/cfm_hotline_client.h"
#include "chromeos/services/cfm/public/cpp/fake_service_connection.h"
#include "chromeos/services/cfm/public/mojom/cfm_service_manager.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;
using ::testing::WithArgs;

namespace chromeos {
namespace cfm {
namespace {

class CfmServiceConnectionTest : public testing::Test {
 public:
  CfmServiceConnectionTest() = default;
  CfmServiceConnectionTest(const CfmServiceConnectionTest&) = delete;
  CfmServiceConnectionTest& operator=(const CfmServiceConnectionTest&) = delete;

  void SetUp() override {
    CfmHotlineClient::InitializeFake();
    ServiceConnection::UseFakeServiceConnectionForTesting(
        &fake_service_connection);
  }

  void TearDown() override { CfmHotlineClient::Shutdown(); }

  void SetCallback(FakeServiceConnectionImpl::FakeBootstrapCallback callback) {
    fake_service_connection.SetCallback(std::move(callback));
  }

 private:
  base::test::TaskEnvironment task_environment_;
  FakeServiceConnectionImpl fake_service_connection;
};

TEST_F(CfmServiceConnectionTest, BindServiceContext) {
  base::RunLoop run_loop;
  base::MockCallback<FakeServiceConnectionImpl::FakeBootstrapCallback> callback;
  ON_CALL(callback, Run(_)).WillByDefault(Invoke([&run_loop](bool success) {
    run_loop.QuitClosure().Run();
  }));
  EXPECT_CALL(callback, Run(true)).Times(1);
  SetCallback(callback.Get());

  mojo::Remote<::chromeos::cfm::mojom::CfmServiceContext> remote;
  ServiceConnection::GetInstance()->BindServiceContext(
      remote.BindNewPipeAndPassReceiver());

  run_loop.Run();

  ASSERT_TRUE(remote.is_bound());
}

}  // namespace
}  // namespace cfm
}  // namespace chromeos
