// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/messaging_layer/public/report_client.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/containers/queue.h"
#include "base/memory/ptr_util.h"
#include "base/memory/singleton.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "chrome/browser/policy/messaging_layer/encryption/encryption_module.h"
#include "chrome/browser/policy/messaging_layer/public/report_queue.h"
#include "chrome/browser/policy/messaging_layer/public/report_queue_configuration.h"
#include "chrome/browser/policy/messaging_layer/storage/storage_module.h"
#include "chrome/browser/policy/messaging_layer/util/status.h"
#include "chrome/browser/policy/messaging_layer/util/status_macros.h"
#include "chrome/browser/policy/messaging_layer/util/statusor.h"
#include "chrome/browser/policy/messaging_layer/util/task_runner_context.h"
#include "chrome/common/chrome_paths.h"
#include "components/enterprise/browser/controller/browser_dm_token_storage.h"
#include "components/policy/core/common/cloud/device_management_service.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/users/chrome_user_manager.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#else
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#endif

namespace reporting {

namespace {

// policy::CloudPolicyClient is needed by the UploadClient, but is built in two
// different ways for ChromeOS and non-ChromeOS browsers.
#if defined(OS_CHROMEOS)
std::unique_ptr<policy::CloudPolicyClient> BuildCloudPolicyClient() {
  policy::DeviceManagementService* const device_management_service =
      g_browser_process->browser_policy_connector()
          ->device_management_service();

  scoped_refptr<network::SharedURLLoaderFactory>
      signin_profile_url_loader_factory =
          g_browser_process->system_network_context_manager()
              ->GetSharedURLLoaderFactory();

  auto* user_manager_ptr = g_browser_process->platform_part()->user_manager();
  auto* primary_user = user_manager_ptr->GetPrimaryUser();

  auto dm_token_getter = chromeos::GetDeviceDMTokenForUserPolicyGetter(
      primary_user->GetAccountId());

  auto client = std::make_unique<policy::CloudPolicyClient>(
      device_management_service, signin_profile_url_loader_factory,
      dm_token_getter);

  policy::CloudPolicyClient::RegistrationParameters registration(
      enterprise_management::DeviceRegisterRequest::USER,
      enterprise_management::DeviceRegisterRequest::FLAVOR_USER_REGISTRATION);

  // Register the client with the device management service.
  client->Register(registration,
                   /*client_id=*/std::string(),
                   /*oauth_token=*/"oauth_token_unused");
  return client;
}
#else
std::unique_ptr<policy::CloudPolicyClient> BuildCloudPolicyClient() {
  policy::DeviceManagementService* const device_management_service =
      g_browser_process->browser_policy_connector()
          ->device_management_service();

  scoped_refptr<network::SharedURLLoaderFactory>
      signin_profile_url_loader_factory =
          g_browser_process->system_network_context_manager()
              ->GetSharedURLLoaderFactory();

  auto client = std::make_unique<policy::CloudPolicyClient>(
      device_management_service, signin_profile_url_loader_factory,
      policy::CloudPolicyClient::DeviceDMTokenCallback());

  policy::DMToken browser_dm_token =
      policy::BrowserDMTokenStorage::Get()->RetrieveDMToken();
  std::string client_id =
      policy::BrowserDMTokenStorage::Get()->RetrieveClientId();

  client->SetupRegistration(browser_dm_token.value(), client_id,
                            std::vector<std::string>());
  return client;
}
#endif

const base::FilePath::CharType kReportingDirectory[] =
    FILE_PATH_LITERAL("reporting");

}  // namespace

using Uploader = ReportingClient::Uploader;

Uploader::Uploader(UploadCallback upload_callback)
    : upload_callback_(std::move(upload_callback)),
      completed_(false),
      encrypted_records_(std::make_unique<std::vector<EncryptedRecord>>()) {}

Uploader::~Uploader() = default;

StatusOr<std::unique_ptr<Uploader>> Uploader::Create(
    UploadCallback upload_callback) {
  auto uploader = base::WrapUnique(new Uploader(std::move(upload_callback)));
  return uploader;
}

void Uploader::ProcessBlob(Priority priority,
                           StatusOr<base::span<const uint8_t>> data,
                           base::OnceCallback<void(bool)> processed_cb) {
  if (completed_ || !data.ok()) {
    std::move(processed_cb).Run(false);
    return;
  }

  class ProcessBlobContext : public TaskRunnerContext<bool> {
   public:
    ProcessBlobContext(
        base::span<const uint8_t> data,
        std::vector<EncryptedRecord>* records,
        base::OnceCallback<void(bool)> processed_callback,
        scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner)
        : TaskRunnerContext<bool>(std::move(processed_callback),
                                  sequenced_task_runner),
          records_(records),
          data_(data.begin(), data.end()) {}

   private:
    ~ProcessBlobContext() override = default;

    void OnStart() override {
      if (data_.empty()) {
        Complete(true);
        return;
      }
      ProcessBlob();
    }

    void ProcessBlob() {
      EncryptedRecord record;
      if (!record.ParseFromArray(data_.data(), data_.size())) {
        Complete(false);
        return;
      }
      records_->push_back(record);
      Complete(true);
    }

    void Complete(bool success) {
      if (!success) {
        LOG(ERROR) << "Unable to process blob";
      }
      Response(success);
    }

    std::vector<EncryptedRecord>* const records_;
    const std::vector<uint8_t> data_;
  };

  Start<ProcessBlobContext>(data.ValueOrDie(), encrypted_records_.get(),
                            std::move(processed_cb), sequenced_task_runner_);
}

void Uploader::Completed(Priority priority, Status final_status) {
  if (!final_status.ok()) {
    // No work to do - something went wrong with storage and it no longer wants
    // to upload the records. Let the records die with |this|.
    return;
  }

  if (completed_) {
    // RunUpload has already been invoked. Return.
    return;
  }

  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&Uploader::RunUpload, base::Unretained(this)));
}

void Uploader::RunUpload() {
  if (completed_) {
    // RunUpload has already been invoked. Return.
    return;
  }
  completed_ = true;

  Status upload_status =
      std::move(upload_callback_).Run(std::move(encrypted_records_));
  if (!upload_status.ok()) {
    LOG(ERROR) << "Unable to upload records: " << upload_status;
  }
}

ReportingClient::Configuration::Configuration() = default;
ReportingClient::Configuration::~Configuration() = default;

ReportingClient::InitializationStateTracker::InitializationStateTracker()
    : sequenced_task_runner_(base::ThreadPool::CreateSequencedTaskRunner({})) {}

ReportingClient::InitializationStateTracker::~InitializationStateTracker() =
    default;

// static
scoped_refptr<ReportingClient::InitializationStateTracker>
ReportingClient::InitializationStateTracker::Create() {
  return base::WrapRefCounted(
      new ReportingClient::InitializationStateTracker());
}

void ReportingClient::InitializationStateTracker::GetInitState(
    GetInitStateCallback get_init_state_cb) {
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &ReportingClient::InitializationStateTracker::OnIsInitializedRequest,
          this, std::move(get_init_state_cb)));
}

void ReportingClient::InitializationStateTracker::RequestLeaderPromotion(
    LeaderPromotionRequestCallback promo_request_cb) {
  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ReportingClient::InitializationStateTracker::
                                    OnLeaderPromotionRequest,
                                this, std::move(promo_request_cb)));
}

void ReportingClient::InitializationStateTracker::OnIsInitializedRequest(
    GetInitStateCallback get_init_state_cb) {
  base::ThreadPool::PostTask(
      FROM_HERE,
      base::BindOnce(
          [](GetInitStateCallback get_init_state_cb, bool is_initialized) {
            std::move(get_init_state_cb).Run(is_initialized);
          },
          std::move(get_init_state_cb), is_initialized_));
}

void ReportingClient::InitializationStateTracker::OnLeaderPromotionRequest(
    LeaderPromotionRequestCallback promo_request_cb) {
  StatusOr<ReleaseLeaderCallback> result;
  if (is_initialized_) {
    result = Status(error::FAILED_PRECONDITION,
                    "ReportClient is already configured");
  } else if (has_promoted_initializing_context_) {
    result = Status(error::RESOURCE_EXHAUSTED,
                    "ReportClient already has a lead initializing context.");
  } else {
    result = base::BindOnce(
        &ReportingClient::InitializationStateTracker::ReleaseLeader, this);
  }

  base::ThreadPool::PostTask(
      FROM_HERE, base::BindOnce(
                     [](LeaderPromotionRequestCallback promo_request_cb,
                        StatusOr<ReleaseLeaderCallback> result) {
                       std::move(promo_request_cb).Run(std::move(result));
                     },
                     std::move(promo_request_cb), std::move(result)));
}

void ReportingClient::InitializationStateTracker::ReleaseLeader(
    bool initialization_successful) {
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &ReportingClient::InitializationStateTracker::OnLeaderRelease, this,
          initialization_successful));
}

void ReportingClient::InitializationStateTracker::OnLeaderRelease(
    bool initialization_successful) {
  if (initialization_successful) {
    is_initialized_ = true;
  }
  has_promoted_initializing_context_ = false;
}

ReportingClient::CreateReportQueueRequest::CreateReportQueueRequest(
    std::unique_ptr<ReportQueueConfiguration> config,
    CreateReportQueueCallback create_cb)
    : config_(std::move(config)), create_cb_(std::move(create_cb)) {}

ReportingClient::CreateReportQueueRequest::~CreateReportQueueRequest() =
    default;

ReportingClient::CreateReportQueueRequest::CreateReportQueueRequest(
    ReportingClient::CreateReportQueueRequest&& other)
    : config_(other.config()), create_cb_(other.create_cb()) {}

std::unique_ptr<ReportQueueConfiguration>
ReportingClient::CreateReportQueueRequest::config() {
  return std::move(config_);
}

ReportingClient::CreateReportQueueCallback
ReportingClient::CreateReportQueueRequest::create_cb() {
  return std::move(create_cb_);
}

ReportingClient::InitializingContext::InitializingContext(
    Storage::StartUploadCb start_upload_cb,
    UpdateConfigurationCallback update_config_cb,
    InitCompleteCallback init_complete_cb,
    scoped_refptr<ReportingClient::InitializationStateTracker>
        init_state_tracker,
    scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner)
    : TaskRunnerContext<Status>(std::move(init_complete_cb),
                                sequenced_task_runner),
      start_upload_cb_(std::move(start_upload_cb)),
      update_config_cb_(std::move(update_config_cb)),
      init_state_tracker_(init_state_tracker) {}

ReportingClient::InitializingContext::~InitializingContext() = default;

void ReportingClient::InitializingContext::OnStart() {
  init_state_tracker_->RequestLeaderPromotion(base::BindOnce(
      &ReportingClient::InitializingContext::OnLeaderPromotionResult,
      base::Unretained(this)));
}

void ReportingClient::InitializingContext::OnLeaderPromotionResult(
    StatusOr<ReportingClient::InitializationStateTracker::ReleaseLeaderCallback>
        promo_result) {
  if (promo_result.status().error_code() == error::FAILED_PRECONDITION) {
    // Between building this InitializationContext and attempting to promote to
    // leader, the ReportingClient was configured. Ok response.
    Complete(Status::StatusOK());
    return;
  }

  if (!promo_result.ok()) {
    Complete(promo_result.status());
    return;
  }

  release_leader_cb_ = std::move(promo_result.ValueOrDie());
  Schedule(&ReportingClient::InitializingContext::ConfigureStorageModule,
           base::Unretained(this));
}

void ReportingClient::InitializingContext::ConfigureStorageModule() {
  base::FilePath user_data_dir;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    Complete(
        Status(error::FAILED_PRECONDITION, "Could not retrieve base path"));
    return;
  }

  base::FilePath reporting_path = user_data_dir.Append(kReportingDirectory);
  StorageModule::Create(
      Storage::Options().set_directory(reporting_path),
      std::move(start_upload_cb_),
      base::BindOnce(
          &ReportingClient::InitializingContext::OnStorageModuleConfigured,
          base::Unretained(this)));
}

void ReportingClient::InitializingContext::OnStorageModuleConfigured(
    StatusOr<scoped_refptr<StorageModule>> storage_result) {
  if (!storage_result.ok()) {
    Complete(Status(error::FAILED_PRECONDITION,
                    base::StrCat({"Unable to build StorageModule: ",
                                  storage_result.status().message()})));
    return;
  }

  client_config_.storage_ = storage_result.ValueOrDie();
  Schedule(&ReportingClient::InitializingContext::ConfigureEncryptionModule,
           base::Unretained(this));
}

// TODO(chromium:1078512) Currently we use a stub encryption module. In the
// future it needs to be replaced with a real one.
void ReportingClient::InitializingContext::ConfigureEncryptionModule() {
  OnEncryptionModuleConfigured(base::MakeRefCounted<EncryptionModule>());
}

void ReportingClient::InitializingContext::OnEncryptionModuleConfigured(
    StatusOr<scoped_refptr<EncryptionModule>> encryption_result) {
  if (!encryption_result.ok()) {
    Complete(Status(error::FAILED_PRECONDITION,
                    base::StrCat({"Unable to build EncryptionModule: ",
                                  encryption_result.status().message()})));
    return;
  }

  client_config_.encryption_ = encryption_result.ValueOrDie();
  Schedule(&ReportingClient::InitializingContext::UpdateConfiguration,
           base::Unretained(this));
}

void ReportingClient::InitializingContext::UpdateConfiguration() {
  std::move(update_config_cb_)
      .Run(std::move(client_config_),
           base::BindOnce(&ReportingClient::InitializingContext::Complete,
                          base::Unretained(this)));
}

void ReportingClient::InitializingContext::Complete(Status status) {
  Schedule(&ReportingClient::InitializingContext::Response,
           base::Unretained(this), status);
}

ReportingClient::ReportingClient()
    : create_request_queue_(SharedQueue<CreateReportQueueRequest>::Create()),
      init_state_tracker_(
          ReportingClient::InitializationStateTracker::Create()) {}

ReportingClient::~ReportingClient() = default;

ReportingClient* ReportingClient::GetInstance() {
  return base::Singleton<ReportingClient>::get();
}

void ReportingClient::CreateReportQueue(
    std::unique_ptr<ReportQueueConfiguration> config,
    CreateReportQueueCallback create_cb) {
  auto* instance = GetInstance();
  instance->create_request_queue_->Push(
      CreateReportQueueRequest(std::move(config), std::move(create_cb)),
      base::BindOnce(&ReportingClient::OnPushComplete,
                     base::Unretained(instance)));
}

void ReportingClient::Reset_test() {
  base::Singleton<ReportingClient>::OnExit(nullptr);
}

void ReportingClient::OnPushComplete() {
  init_state_tracker_->GetInitState(
      base::BindOnce(&ReportingClient::OnInitState, base::Unretained(this)));
}

void ReportingClient::OnInitState(bool reporting_client_configured) {
  if (!reporting_client_configured) {
    // Schedule an InitializingContext to take care of initialization.
    Start<ReportingClient::InitializingContext>(
        base::BindRepeating(&ReportingClient::BuildUploader),
        base::BindOnce(&ReportingClient::OnConfigResult,
                       base::Unretained(this)),
        base::BindOnce(&ReportingClient::OnInitializationComplete,
                       base::Unretained(this)),
        init_state_tracker_, base::ThreadPool::CreateSequencedTaskRunner({}));
    return;
  }

  // Client was configured, build the queue!
  create_request_queue_->Pop(base::BindOnce(&ReportingClient::BuildRequestQueue,
                                            base::Unretained(this)));
}

void ReportingClient::OnConfigResult(
    const ReportingClient::Configuration& config,
    base::OnceCallback<void(Status)> continue_init_cb) {
  config_ = std::move(config);
  std::move(continue_init_cb).Run(Status::StatusOK());
}

void ReportingClient::OnInitializationComplete(Status init_status) {
  if (init_status.error_code() == error::RESOURCE_EXHAUSTED) {
    // This happens when a new request comes in while the ReportingClient is
    // undergoing initialization. The leader will either clear or build the
    // queue when it completes.
    return;
  }

  // Configuration failed. Clear out all the requests that came in while we were
  // attempting to configure.
  if (!init_status.ok()) {
    create_request_queue_->Swap(
        base::queue<CreateReportQueueRequest>(),
        base::BindOnce(&ReportingClient::ClearRequestQueue,
                       base::Unretained(this)));
    return;
  }
  create_request_queue_->Pop(base::BindOnce(&ReportingClient::BuildRequestQueue,
                                            base::Unretained(this)));
}

void ReportingClient::ClearRequestQueue(
    base::queue<CreateReportQueueRequest> failed_requests) {
  while (!failed_requests.empty()) {
    // Post to general thread.
    base::ThreadPool::PostTask(
        FROM_HERE, base::BindOnce(
                       [](CreateReportQueueRequest queue_request) {
                         std::move(queue_request.create_cb())
                             .Run(Status(error::UNAVAILABLE,
                                         "Unable to build a ReportQueue"));
                       },
                       std::move(failed_requests.front())));
    failed_requests.pop();
  }
}

void ReportingClient::BuildRequestQueue(
    StatusOr<CreateReportQueueRequest> pop_result) {
  // Queue is clear - nothing more to do.
  if (!pop_result.ok()) {
    return;
  }

  // We don't want to block either the ReportingClient sequenced_task_runner_ or
  // the create_request_queue_.sequenced_task_runner_, so we post the task to a
  // general thread.
  base::ThreadPool::PostTask(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<StorageModule> storage_module,
             scoped_refptr<EncryptionModule> encryption_module,
             CreateReportQueueRequest report_queue_request) {
            std::move(report_queue_request.create_cb())
                .Run(ReportQueue::Create(report_queue_request.config(),
                                         storage_module, encryption_module));
          },
          config_.storage_, config_.encryption_,
          std::move(pop_result.ValueOrDie())));

  // Build the next item asynchronously
  create_request_queue_->Pop(base::BindOnce(&ReportingClient::BuildRequestQueue,
                                            base::Unretained(this)));
}

// static
StatusOr<std::unique_ptr<Storage::UploaderInterface>>
ReportingClient::BuildUploader(Priority priority) {
  ReportingClient* const instance = GetInstance();
  if (instance->upload_client_ == nullptr) {
    ASSIGN_OR_RETURN(
        instance->upload_client_,
        UploadClient::Create(BuildCloudPolicyClient(),
                             base::BindRepeating(&StorageModule::ReportSuccess,
                                                 instance->storage_)));
  }
  return Uploader::Create(
      base::BindOnce(&UploadClient::EnqueueUpload,
                     base::Unretained(instance->upload_client_.get())));
}

}  // namespace reporting
