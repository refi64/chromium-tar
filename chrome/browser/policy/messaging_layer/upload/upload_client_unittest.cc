// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/messaging_layer/upload/upload_client.h"
#include "base/json/json_writer.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "chrome/browser/policy/messaging_layer/upload/app_install_report_handler.h"
#include "components/policy/core/common/cloud/dm_token.h"
#include "components/policy/core/common/cloud/mock_cloud_policy_client.h"
#include "components/policy/proto/record.pb.h"
#include "components/policy/proto/record_constants.pb.h"

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/test/test_mock_time_task_runner.h"
#include "components/account_id/account_id.h"
#include "services/network/test/test_network_connection_tracker.h"

namespace reporting {
namespace {

using policy::MockCloudPolicyClient;
using testing::_;
using testing::Invoke;
using testing::InvokeArgument;
using testing::WithArgs;

class TestCallbackWaiter {
 public:
  TestCallbackWaiter()
      : completed_(base::WaitableEvent::ResetPolicy::MANUAL,
                   base::WaitableEvent::InitialState::NOT_SIGNALED) {}

  virtual void Signal() {
    DCHECK(!completed_.IsSignaled());
    completed_.Signal();
  }

  void Wait() { completed_.Wait(); }

 protected:
  base::WaitableEvent completed_;
};

class TestCallbackWaiterWithCounter : public TestCallbackWaiter {
 public:
  explicit TestCallbackWaiterWithCounter(int counter_limit)
      : counter_limit_(counter_limit) {}

  void Signal() override {
    DCHECK(!completed_.IsSignaled());
    DCHECK_GT(counter_limit_, 0);
    if (--counter_limit_ == 0) {
      completed_.Signal();
    }
  }

 private:
  std::atomic<int> counter_limit_;
};

TEST(UploadClientTest, CreateUploadClient) {
  base::test::TaskEnvironment task_envrionment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  const int kExpectedCallTimes = 10;
  const uint64_t kGenerationId = 1234;

  TestCallbackWaiterWithCounter waiter(kExpectedCallTimes);

  auto client = std::make_unique<MockCloudPolicyClient>();
  client->SetDMToken(
      policy::DMToken::CreateValidTokenForTesting("FAKE_DM_TOKEN").value());

  EXPECT_CALL(*client, UploadAppInstallReport_(_, _))
      .WillRepeatedly(WithArgs<1>(
          Invoke([&waiter](AppInstallReportHandler::ClientCallback& callback) {
            std::move(callback).Run(true);
            waiter.Signal();
          })));

  auto upload_client_result =
      UploadClient::Create(std::move(client), base::DoNothing());

  ASSERT_TRUE(upload_client_result.ok());

  base::Value data{base::Value::Type::DICTIONARY};
  data.SetKey("TEST_KEY", base::Value("TEST_VALUE"));

  std::string json_data;
  ASSERT_TRUE(base::JSONWriter::Write(data, &json_data));

  WrappedRecord wrapped_record;
  Record* record = wrapped_record.mutable_record();
  record->set_data(json_data);
  record->set_destination(Destination::APP_INSTALL_EVENT);

  std::string serialized_record;
  wrapped_record.SerializeToString(&serialized_record);

  std::unique_ptr<std::vector<EncryptedRecord>> records =
      std::make_unique<std::vector<EncryptedRecord>>();
  for (int i = 0; i < kExpectedCallTimes; i++) {
    EncryptedRecord encrypted_record;
    encrypted_record.set_encrypted_wrapped_record(serialized_record);

    SequencingInformation* sequencing_information =
        encrypted_record.mutable_sequencing_information();
    sequencing_information->set_sequencing_id(i);
    sequencing_information->set_generation_id(kGenerationId);
    sequencing_information->set_priority(Priority::IMMEDIATE);
    records->push_back(encrypted_record);
  }

  auto upload_client = std::move(upload_client_result.ValueOrDie());
  auto enqueue_result = upload_client->EnqueueUpload(std::move(records));
  EXPECT_TRUE(enqueue_result.ok());

  waiter.Wait();
}

}  // namespace
}  // namespace reporting
