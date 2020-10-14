// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/nearby_sharing/nearby_sharing_service_impl.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/containers/span.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/system/sys_info.h"
#include "base/test/bind_test_util.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/browser_features.h"
#include "chrome/browser/download/chrome_download_manager_delegate.h"
#include "chrome/browser/download/download_core_service_factory.h"
#include "chrome/browser/download/download_core_service_impl.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/nearby_sharing/certificates/fake_nearby_share_certificate_manager.h"
#include "chrome/browser/nearby_sharing/certificates/nearby_share_certificate_manager_impl.h"
#include "chrome/browser/nearby_sharing/certificates/test_util.h"
#include "chrome/browser/nearby_sharing/common/nearby_share_prefs.h"
#include "chrome/browser/nearby_sharing/constants.h"
#include "chrome/browser/nearby_sharing/contacts/fake_nearby_share_contact_manager.h"
#include "chrome/browser/nearby_sharing/contacts/nearby_share_contact_manager_impl.h"
#include "chrome/browser/nearby_sharing/fake_nearby_connection.h"
#include "chrome/browser/nearby_sharing/fake_nearby_connections_manager.h"
#include "chrome/browser/nearby_sharing/fast_initiation_manager.h"
#include "chrome/browser/nearby_sharing/local_device_data/fake_nearby_share_local_device_data_manager.h"
#include "chrome/browser/nearby_sharing/local_device_data/nearby_share_local_device_data_manager_impl.h"
#include "chrome/browser/nearby_sharing/mock_nearby_process_manager.h"
#include "chrome/browser/nearby_sharing/mock_nearby_sharing_decoder.h"
#include "chrome/browser/nearby_sharing/nearby_connections_manager.h"
#include "chrome/browser/nearby_sharing/proto/rpc_resources.pb.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/notifications/notification_display_service_tester.h"
#include "chrome/services/sharing/public/cpp/advertisement.h"
#include "chrome/services/sharing/public/proto/wire_format.pb.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/test/browser_task_environment.h"
#include "device/bluetooth/bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"
#include "device/bluetooth/test/mock_bluetooth_adapter.h"
#include "net/base/mock_network_change_notifier.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/idle/scoped_set_idle_state.h"

using ::testing::_;
using testing::NiceMock;
using testing::Return;

using NetConnectionType = net::NetworkChangeNotifier::ConnectionType;

using SendSurfaceState = NearbySharingService::SendSurfaceState;

class FakeFastInitiationManager : public FastInitiationManager {
 public:
  explicit FakeFastInitiationManager(
      scoped_refptr<device::BluetoothAdapter> adapter,
      bool should_succeed_on_start,
      base::OnceCallback<void()> on_stop_advertising_callback,
      base::OnceCallback<void()> on_destroy_callback)
      : FastInitiationManager(adapter),
        should_succeed_on_start_(should_succeed_on_start),
        on_stop_advertising_callback_(std::move(on_stop_advertising_callback)),
        on_destroy_callback_(std::move(on_destroy_callback)) {}

  ~FakeFastInitiationManager() override {
    std::move(on_destroy_callback_).Run();
  }

  void StartAdvertising(FastInitType type,
                        base::OnceCallback<void()> callback,
                        base::OnceCallback<void()> error_callback) override {
    ++start_advertising_call_count_;
    if (should_succeed_on_start_)
      std::move(callback).Run();
    else
      std::move(error_callback).Run();
  }

  void StopAdvertising(base::OnceCallback<void()> callback) override {
    std::move(on_stop_advertising_callback_).Run();
    std::move(callback).Run();
  }

  size_t start_advertising_call_count() {
    return start_advertising_call_count_;
  }

 private:
  bool should_succeed_on_start_;
  size_t start_advertising_call_count_ = 0u;
  base::OnceCallback<void()> on_stop_advertising_callback_;
  base::OnceCallback<void()> on_destroy_callback_;
};

class FakeFastInitiationManagerFactory : public FastInitiationManager::Factory {
 public:
  explicit FakeFastInitiationManagerFactory(bool should_succeed_on_start)
      : should_succeed_on_start_(should_succeed_on_start) {}

  std::unique_ptr<FastInitiationManager> CreateInstance(
      scoped_refptr<device::BluetoothAdapter> adapter) override {
    auto fake_fast_initiation_manager = std::make_unique<
        FakeFastInitiationManager>(
        adapter, should_succeed_on_start_,
        base::BindOnce(&FakeFastInitiationManagerFactory::OnStopAdvertising,
                       weak_ptr_factory_.GetWeakPtr()),
        base::BindOnce(
            &FakeFastInitiationManagerFactory::OnFastInitiationManagerDestroyed,
            weak_ptr_factory_.GetWeakPtr()));
    last_fake_fast_initiation_manager_ = fake_fast_initiation_manager.get();
    return std::move(fake_fast_initiation_manager);
  }

  void OnStopAdvertising() { stop_advertising_called_ = true; }

  void OnFastInitiationManagerDestroyed() {
    fast_initiation_manager_destroyed_ = true;
    last_fake_fast_initiation_manager_ = nullptr;
  }

  size_t StartAdvertisingCount() {
    return last_fake_fast_initiation_manager_
               ? last_fake_fast_initiation_manager_
                     ->start_advertising_call_count()
               : 0;
  }

  bool StopAdvertisingCalledAndManagerDestroyed() {
    return stop_advertising_called_ && fast_initiation_manager_destroyed_;
  }

 private:
  FakeFastInitiationManager* last_fake_fast_initiation_manager_ = nullptr;
  bool should_succeed_on_start_ = false;
  bool stop_advertising_called_ = false;
  bool fast_initiation_manager_destroyed_ = false;
  base::WeakPtrFactory<FakeFastInitiationManagerFactory> weak_ptr_factory_{
      this};
};

class MockTransferUpdateCallback : public TransferUpdateCallback {
 public:
  ~MockTransferUpdateCallback() override = default;

  MOCK_METHOD(void,
              OnTransferUpdate,
              (const ShareTarget& shareTarget,
               const TransferMetadata& transferMetadata),
              (override));
};

class MockShareTargetDiscoveredCallback : public ShareTargetDiscoveredCallback {
 public:
  ~MockShareTargetDiscoveredCallback() override = default;

  MOCK_METHOD(void,
              OnShareTargetDiscovered,
              (ShareTarget shareTarget),
              (override));
  MOCK_METHOD(void, OnShareTargetLost, (ShareTarget shareTarget), (override));
};

namespace {

constexpr base::TimeDelta kDelta = base::TimeDelta::FromMilliseconds(100);

const char kServiceId[] = "NearbySharing";
const char kDeviceName[] = "test_device_name";
const char kEndpointId[] = "test_endpoint_id";

const std::vector<uint8_t> kValidV1EndpointInfo = {
    0, 0, 0, 0,  0,   0,   0,   0,   0,  0,   0,  0,  0,   0,
    0, 0, 0, 10, 100, 101, 118, 105, 99, 101, 78, 97, 109, 101};

const std::vector<uint8_t> kToken = {0, 1, 2};
const char kFourDigitToken[] = "1953";

const std::vector<uint8_t> kPrivateCertificateHashAuthToken = {
    0x8b, 0xcb, 0xa2, 0xf8, 0xe4, 0x06};
const std::vector<uint8_t> kIncomingConnectionSignedData = {
    0x30, 0x45, 0x02, 0x20, 0x4f, 0x83, 0x72, 0xbd, 0x02, 0x70, 0xd9, 0xda,
    0x62, 0x83, 0x5d, 0xb2, 0xdc, 0x6e, 0x3f, 0xa6, 0xa8, 0xa1, 0x4f, 0x5f,
    0xd3, 0xe3, 0xd9, 0x1a, 0x5d, 0x2d, 0x61, 0xd2, 0x6c, 0xdd, 0x8d, 0xa5,
    0x02, 0x21, 0x00, 0xd4, 0xe1, 0x1d, 0x14, 0xcb, 0x58, 0xf7, 0x02, 0xd5,
    0xab, 0x48, 0xe2, 0x2f, 0xcb, 0xc0, 0x53, 0x41, 0x06, 0x50, 0x65, 0x95,
    0x19, 0xa9, 0x22, 0x92, 0x00, 0x42, 0x01, 0x26, 0x25, 0xcb, 0x8c};

sharing::mojom::FramePtr GetValidIntroductionFrame() {
  std::vector<sharing::mojom::TextMetadataPtr> mojo_text_metadatas;
  // TODO(himanshujaju) - Parameterise number of text and file metadatas.
  for (int i = 1; i <= 3; i++) {
    mojo_text_metadatas.push_back(sharing::mojom::TextMetadata::New(
        "title " + base::NumberToString(i),
        static_cast<sharing::mojom::TextMetadata::Type>(i), i, i, i));
  }

  sharing::mojom::V1FramePtr mojo_v1frame = sharing::mojom::V1Frame::New();
  mojo_v1frame->set_introduction(sharing::mojom::IntroductionFrame::New(
      std::vector<sharing::mojom::FileMetadataPtr>(),
      std::move(mojo_text_metadatas), base::nullopt,
      std::vector<sharing::mojom::WifiCredentialsMetadataPtr>()));

  sharing::mojom::FramePtr mojo_frame = sharing::mojom::Frame::New();
  mojo_frame->set_v1(std::move(mojo_v1frame));
  return mojo_frame;
}

sharing::mojom::FramePtr GetEmptyIntroductionFrame() {
  sharing::mojom::V1FramePtr mojo_v1frame = sharing::mojom::V1Frame::New();
  mojo_v1frame->set_introduction(sharing::mojom::IntroductionFrame::New());

  sharing::mojom::FramePtr mojo_frame = sharing::mojom::Frame::New();
  mojo_frame->set_v1(std::move(mojo_v1frame));
  return mojo_frame;
}

class NearbySharingServiceImplTest : public testing::Test {
 public:
  NearbySharingServiceImplTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    scoped_feature_list_.InitAndEnableFeature(features::kNearbySharing);
    RegisterNearbySharingPrefs(prefs_.registry());
  }

  ~NearbySharingServiceImplTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(profile_manager_.SetUp());
    network_notifier_ = net::test::MockNetworkChangeNotifier::Create();

    NearbyShareLocalDeviceDataManagerImpl::Factory::SetFactoryForTesting(
        &local_device_data_manager_factory_);
    NearbyShareContactManagerImpl::Factory::SetFactoryForTesting(
        &contact_manager_factory_);
    NearbyShareCertificateManagerImpl::Factory::SetFactoryForTesting(
        &certificate_manager_factory_);

    mock_bluetooth_adapter_ =
        base::MakeRefCounted<NiceMock<device::MockBluetoothAdapter>>();
    ON_CALL(*mock_bluetooth_adapter_, IsPresent())
        .WillByDefault(
            Invoke(this, &NearbySharingServiceImplTest::IsBluetoothPresent));
    ON_CALL(*mock_bluetooth_adapter_, IsPowered())
        .WillByDefault(
            Invoke(this, &NearbySharingServiceImplTest::IsBluetoothPowered));
    ON_CALL(*mock_bluetooth_adapter_, AddObserver(_))
        .WillByDefault(
            Invoke(this, &NearbySharingServiceImplTest::AddAdapterObserver));
    device::BluetoothAdapterFactory::SetAdapterForTesting(
        mock_bluetooth_adapter_);

    service_ = CreateService("name");
    SetFakeFastInitiationManagerFactory(/*should_succeed_on_start=*/true);

    EXPECT_CALL(mock_nearby_process_manager(),
                GetOrStartNearbySharingDecoder(testing::_))
        .WillRepeatedly(testing::Return(&mock_decoder_));
  }

  void TearDown() override {
    if (profile_) {
      DownloadCoreServiceFactory::GetForBrowserContext(profile_)
          ->SetDownloadManagerDelegateForTesting(nullptr);
      profile_ = nullptr;
    }

    profile_manager_.DeleteAllTestingProfiles();
  }

  std::unique_ptr<NearbySharingServiceImpl> CreateService(
      const std::string& profile_name) {
    profile_ = profile_manager_.CreateTestingProfile(profile_name);
    fake_nearby_connections_manager_ = new FakeNearbyConnectionsManager();
    notification_tester_ =
        std::make_unique<NotificationDisplayServiceTester>(profile_);
    NotificationDisplayService* notification_display_service =
        NotificationDisplayServiceFactory::GetForProfile(profile_);
    auto service = std::make_unique<NearbySharingServiceImpl>(
        &prefs_, notification_display_service, profile_,
        base::WrapUnique(fake_nearby_connections_manager_),
        &mock_nearby_process_manager_);
    ON_CALL(mock_nearby_process_manager_, IsActiveProfile(profile_))
        .WillByDefault(Return(true));

    DownloadCoreServiceFactory::GetForBrowserContext(profile_)
        ->SetDownloadManagerDelegateForTesting(
            std::make_unique<ChromeDownloadManagerDelegate>(profile_));

    // Allow the posted task to fetch the BluetoothAdapter to finish.
    base::RunLoop().RunUntilIdle();

    return service;
  }

  void SetVisibility(nearby_share::mojom::Visibility visibility) {
    NearbyShareSettings settings(&prefs_);
    settings.SetVisibility(visibility);
  }

  void SetFakeFastInitiationManagerFactory(bool should_succeed_on_start) {
    fast_initiation_manager_factory_ =
        std::make_unique<FakeFastInitiationManagerFactory>(
            should_succeed_on_start);
    FastInitiationManager::Factory::SetFactoryForTesting(
        fast_initiation_manager_factory_.get());
  }

  bool IsBluetoothPresent() { return is_bluetooth_present_; }
  bool IsBluetoothPowered() { return is_bluetooth_powered_; }

  void AddAdapterObserver(device::BluetoothAdapter::Observer* observer) {
    DCHECK(!adapter_observer_);
    adapter_observer_ = observer;
  }

  void SetConnectionType(net::NetworkChangeNotifier::ConnectionType type) {
    network_notifier_->SetConnectionType(type);
    network_notifier_->NotifyObserversOfNetworkChangeForTests(
        network_notifier_->GetConnectionType());
  }

  NiceMock<MockNearbyProcessManager>& mock_nearby_process_manager() {
    return mock_nearby_process_manager_;
  }

  void SetUpForegroundReceiveSurface(
      NiceMock<MockTransferUpdateCallback>& callback) {
    NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
        &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
    EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
    EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
  }

  void ProcessLatestPublicCertificateDecryption(size_t expected_num_calls,
                                                bool success) {
    std::vector<
        FakeNearbyShareCertificateManager::GetDecryptedPublicCertificateCall>&
        calls = certificate_manager()->get_decrypted_public_certificate_calls();

    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(expected_num_calls, calls.size());
    EXPECT_EQ(GetNearbyShareTestEncryptedMetadataKey().salt(),
              calls.back().encrypted_metadata_key.salt());
    EXPECT_EQ(GetNearbyShareTestEncryptedMetadataKey().encrypted_key(),
              calls.back().encrypted_metadata_key.encrypted_key());

    if (success) {
      std::move(calls.back().callback)
          .Run(NearbyShareDecryptedPublicCertificate::DecryptPublicCertificate(
              GetNearbyShareTestPublicCertificate(
                  NearbyShareVisibility::kAllContacts),
              GetNearbyShareTestEncryptedMetadataKey()));
    } else {
      std::move(calls.back().callback).Run(base::nullopt);
    }
  }

  void SetUpKeyVerification(
      sharing::mojom::PairedKeyResultFrame::Status status) {
    SetVisibility(nearby_share::mojom::Visibility::kAllContacts);

    std::string encryption_frame = "test_encryption_frame";
    std::vector<uint8_t> encryption_bytes(encryption_frame.begin(),
                                          encryption_frame.end());
    EXPECT_CALL(mock_decoder_,
                DecodeFrame(testing::Eq(encryption_bytes), testing::_))
        .WillOnce(testing::Invoke(
            [](const std::vector<uint8_t>& data,
               MockNearbySharingDecoder::DecodeFrameCallback callback) {
              sharing::mojom::V1FramePtr mojo_v1frame =
                  sharing::mojom::V1Frame::New();
              mojo_v1frame->set_paired_key_encryption(
                  sharing::mojom::PairedKeyEncryptionFrame::New(
                      kIncomingConnectionSignedData,
                      kPrivateCertificateHashAuthToken));
              sharing::mojom::FramePtr mojo_frame =
                  sharing::mojom::Frame::New();
              mojo_frame->set_v1(std::move(mojo_v1frame));
              std::move(callback).Run(std::move(mojo_frame));
            }));
    connection_.AppendReadableData(encryption_bytes);

    std::string encryption_result = "test_encryption_result";
    std::vector<uint8_t> result_bytes(encryption_result.begin(),
                                      encryption_result.end());
    EXPECT_CALL(mock_decoder_,
                DecodeFrame(testing::Eq(result_bytes), testing::_))
        .WillOnce(testing::Invoke(
            [=](const std::vector<uint8_t>& data,
                MockNearbySharingDecoder::DecodeFrameCallback callback) {
              sharing::mojom::V1FramePtr mojo_v1frame =
                  sharing::mojom::V1Frame::New();
              mojo_v1frame->set_paired_key_result(
                  sharing::mojom::PairedKeyResultFrame::New(status));

              sharing::mojom::FramePtr mojo_frame =
                  sharing::mojom::Frame::New();
              mojo_frame->set_v1(std::move(mojo_v1frame));
              std::move(callback).Run(std::move(mojo_frame));
            }));
    connection_.AppendReadableData(result_bytes);
  }

  void SetUpAdvertisementDecoder(const std::vector<uint8_t>& endpoint_info,
                                 bool return_empty_advertisement) {
    EXPECT_CALL(mock_decoder_,
                DecodeAdvertisement(testing::Eq(endpoint_info), testing::_))
        .WillOnce(testing::Invoke(
            [=](const std::vector<uint8_t>& data,
                MockNearbySharingDecoder::DecodeAdvertisementCallback
                    callback) {
              if (return_empty_advertisement) {
                std::move(callback).Run(nullptr);
                return;
              }

              sharing::mojom::AdvertisementPtr advertisement =
                  sharing::mojom::Advertisement::New(
                      GetNearbyShareTestEncryptedMetadataKey().salt(),
                      GetNearbyShareTestEncryptedMetadataKey().encrypted_key(),
                      kDeviceName);
              std::move(callback).Run(std::move(advertisement));
            }));
  }

  void SetUpIntroductionFrameDecoder(bool return_empty_introduction_frame) {
    std::string intro = "introduction_frame";
    std::vector<uint8_t> bytes(intro.begin(), intro.end());
    EXPECT_CALL(mock_decoder_, DecodeFrame(testing::Eq(bytes), testing::_))
        .WillOnce(testing::Invoke(
            [=](const std::vector<uint8_t>& data,
                MockNearbySharingDecoder::DecodeFrameCallback callback) {
              std::move(callback).Run(return_empty_introduction_frame
                                          ? GetEmptyIntroductionFrame()
                                          : GetValidIntroductionFrame());
            }));
    connection_.AppendReadableData(bytes);
  }

  ShareTarget SetUpIncomingConnection(
      NiceMock<MockTransferUpdateCallback>& callback) {
    fake_nearby_connections_manager_->SetRawAuthenticationToken(kEndpointId,
                                                                kToken);
    SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                              /*return_empty_advertisement=*/false);
    SetUpIntroductionFrameDecoder(/*return_empty_introduction_frame=*/false);

    ShareTarget share_target;
    ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
    SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
    base::RunLoop run_loop;
    EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
        .WillOnce(testing::Invoke([&](const ShareTarget& incoming_share_target,
                                      TransferMetadata metadata) {
          EXPECT_EQ(TransferMetadata::Status::kAwaitingLocalConfirmation,
                    metadata.status());
          EXPECT_FALSE(metadata.is_final_status());
          share_target = incoming_share_target;
          run_loop.Quit();
        }));

    SetUpKeyVerification(sharing::mojom::PairedKeyResultFrame_Status::kSuccess);
    SetUpForegroundReceiveSurface(callback);
    service_->OnIncomingConnection(kEndpointId, kValidV1EndpointInfo,
                                   &connection_);
    ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                             /*success=*/true);
    run_loop.Run();

    EXPECT_TRUE(
        fake_nearby_connections_manager_->DidUpgradeBandwidth(kEndpointId));

    return share_target;
  }

  sharing::nearby::Frame GetWrittenFrame() {
    std::vector<uint8_t> data = connection_.GetWrittenData();
    sharing::nearby::Frame frame;
    frame.ParseFromArray(data.data(), data.size());
    return frame;
  }

  void ExpectPairedKeyEncryptionFrame() {
    sharing::nearby::Frame frame = GetWrittenFrame();
    ASSERT_TRUE(frame.has_v1());
    ASSERT_TRUE(frame.v1().has_paired_key_encryption());
  }

  void ExpectPairedKeyResultFrame() {
    sharing::nearby::Frame frame = GetWrittenFrame();
    ASSERT_TRUE(frame.has_v1());
    ASSERT_TRUE(frame.v1().has_paired_key_result());
  }

  void ExpectConnectionResponseFrame(
      sharing::nearby::ConnectionResponseFrame::Status status) {
    sharing::nearby::Frame frame = GetWrittenFrame();
    ASSERT_TRUE(frame.has_v1());
    ASSERT_TRUE(frame.v1().has_connection_response());
    EXPECT_EQ(status, frame.v1().connection_response().status());
  }

 protected:
  FakeNearbyShareLocalDeviceDataManager* local_device_data_manager() {
    EXPECT_EQ(1u, local_device_data_manager_factory_.instances().size());
    return local_device_data_manager_factory_.instances().back();
  }

  FakeNearbyShareContactManager* contact_manager() {
    EXPECT_EQ(1u, contact_manager_factory_.instances().size());
    return contact_manager_factory_.instances().back();
  }

  FakeNearbyShareCertificateManager* certificate_manager() {
    EXPECT_EQ(1u, certificate_manager_factory_.instances().size());
    return certificate_manager_factory_.instances().back();
  }

  base::test::ScopedFeatureList scoped_feature_list_;
  // We need to ensure that |network_notifier_| is created and destroyed after
  // |task_environment_| to avoid UAF issues when using
  // ChromeDownloadManagerDelegate.
  std::unique_ptr<net::test::MockNetworkChangeNotifier> network_notifier_;
  content::BrowserTaskEnvironment task_environment_;
  ui::ScopedSetIdleState idle_state_{ui::IDLE_STATE_IDLE};
  TestingProfileManager profile_manager_{TestingBrowserProcess::GetGlobal()};
  Profile* profile_ = nullptr;
  sync_preferences::TestingPrefServiceSyncable prefs_;
  FakeNearbyConnectionsManager* fake_nearby_connections_manager_ = nullptr;
  FakeNearbyShareLocalDeviceDataManager::Factory
      local_device_data_manager_factory_;
  FakeNearbyShareContactManager::Factory contact_manager_factory_;
  FakeNearbyShareCertificateManager::Factory certificate_manager_factory_;
  std::unique_ptr<NotificationDisplayServiceTester> notification_tester_;
  std::unique_ptr<NearbySharingServiceImpl> service_;
  std::unique_ptr<FakeFastInitiationManagerFactory>
      fast_initiation_manager_factory_;
  bool is_bluetooth_present_ = true;
  bool is_bluetooth_powered_ = true;
  device::BluetoothAdapter::Observer* adapter_observer_ = nullptr;
  scoped_refptr<NiceMock<device::MockBluetoothAdapter>> mock_bluetooth_adapter_;
  NiceMock<MockNearbyProcessManager> mock_nearby_process_manager_;
  NiceMock<MockNearbySharingDecoder> mock_decoder_;
  FakeNearbyConnection connection_;
};

struct ValidSendSurfaceTestData {
  ui::IdleState idle_state;
  bool bluetooth_enabled;
  net::NetworkChangeNotifier::ConnectionType connection_type;
} kValidSendSurfaceTestData[] = {
    // No network connection, only bluetooth available
    {ui::IDLE_STATE_IDLE, true, net::NetworkChangeNotifier::CONNECTION_NONE},
    // Wifi available
    {ui::IDLE_STATE_IDLE, true, net::NetworkChangeNotifier::CONNECTION_WIFI},
    // Ethernet available
    {ui::IDLE_STATE_IDLE, true,
     net::NetworkChangeNotifier::CONNECTION_ETHERNET},
    // 3G available
    {ui::IDLE_STATE_IDLE, true, net::NetworkChangeNotifier::CONNECTION_3G},
    // Wifi available and no bluetooth
    {ui::IDLE_STATE_IDLE, false, net::NetworkChangeNotifier::CONNECTION_WIFI},
    // Ethernet available and no bluetooth
    {ui::IDLE_STATE_IDLE, false,
     net::NetworkChangeNotifier::CONNECTION_ETHERNET}};

class NearbySharingServiceImplValidSendTest
    : public NearbySharingServiceImplTest,
      public testing::WithParamInterface<ValidSendSurfaceTestData> {};

struct InvalidSendSurfaceTestData {
  ui::IdleState idle_state;
  bool bluetooth_enabled;
  net::NetworkChangeNotifier::ConnectionType connection_type;
} kInvalidSendSurfaceTestData[] = {
    // Screen locked
    {ui::IDLE_STATE_LOCKED, true, net::NetworkChangeNotifier::CONNECTION_WIFI},
    // No network connection and no bluetooth
    {ui::IDLE_STATE_IDLE, false, net::NetworkChangeNotifier::CONNECTION_NONE},
    // 3G available and no bluetooth
    {ui::IDLE_STATE_IDLE, false, net::NetworkChangeNotifier::CONNECTION_3G},
};

class NearbySharingServiceImplInvalidSendTest
    : public NearbySharingServiceImplTest,
      public testing::WithParamInterface<InvalidSendSurfaceTestData> {};

}  // namespace

TEST_F(NearbySharingServiceImplTest, AddsNearbyProcessObserver) {
  EXPECT_TRUE(
      mock_nearby_process_manager().observers_.HasObserver(service_.get()));
}

TEST_F(NearbySharingServiceImplTest, RemovesNearbyProcessObserver) {
  service_.reset();
  EXPECT_FALSE(mock_nearby_process_manager().observers_.might_have_observers());
}

TEST_F(NearbySharingServiceImplTest, DisableNearbyShutdownConnections) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  prefs_.SetBoolean(prefs::kNearbySharingEnabledPrefName, false);
  service_->FlushMojoForTesting();
  EXPECT_TRUE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest, StartFastInitiationAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_EQ(1u, fast_initiation_manager_factory_->StartAdvertisingCount());

  // Call RegisterSendSurface a second time and make sure StartAdvertising is
  // not called again.
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kError,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_EQ(1u, fast_initiation_manager_factory_->StartAdvertisingCount());
}

TEST_F(NearbySharingServiceImplTest, StartFastInitiationAdvertisingError) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  SetFakeFastInitiationManagerFactory(/*should_succeed_on_start=*/false);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
}

TEST_F(NearbySharingServiceImplTest,
       BackgroundStartFastInitiationAdvertisingError) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kBackground));
  EXPECT_EQ(0u, fast_initiation_manager_factory_->StartAdvertisingCount());
}

TEST_F(NearbySharingServiceImplTest,
       StartFastInitiationAdvertising_BluetoothNotPresent) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  is_bluetooth_present_ = false;
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
}

TEST_F(NearbySharingServiceImplTest,
       StartFastInitiationAdvertising_BluetoothNotPowered) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  is_bluetooth_powered_ = false;
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
}

TEST_F(NearbySharingServiceImplTest, StopFastInitiationAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_EQ(1u, fast_initiation_manager_factory_->StartAdvertisingCount());
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->UnregisterSendSurface(&transfer_callback, &discovery_callback));
  EXPECT_TRUE(fast_initiation_manager_factory_
                  ->StopAdvertisingCalledAndManagerDestroyed());
}

TEST_F(NearbySharingServiceImplTest,
       StopFastInitiationAdvertising_BluetoothBecomesNotPresent) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  adapter_observer_->AdapterPresentChanged(mock_bluetooth_adapter_.get(),
                                           false);
  EXPECT_TRUE(fast_initiation_manager_factory_
                  ->StopAdvertisingCalledAndManagerDestroyed());
}

TEST_F(NearbySharingServiceImplTest,
       StopFastInitiationAdvertising_BluetoothBecomesNotPowered) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  adapter_observer_->AdapterPoweredChanged(mock_bluetooth_adapter_.get(),
                                           false);
  EXPECT_TRUE(fast_initiation_manager_factory_
                  ->StopAdvertisingCalledAndManagerDestroyed());
}

TEST_F(NearbySharingServiceImplTest,
       RegisterSendSurfaceNoActiveProfilesNotDiscovering) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  ON_CALL(mock_nearby_process_manager_, IsActiveProfile(_))
      .WillByDefault(Return(false));
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kError,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
}

TEST_F(NearbySharingServiceImplTest,
       ForegroundRegisterSendSurfaceStartsDiscovering) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());
}

TEST_F(NearbySharingServiceImplTest,
       ForegroundRegisterSendSurfaceTwiceKeepsDiscovering) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());

  EXPECT_EQ(
      NearbySharingService::StatusCodes::kError,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());
}

TEST_F(NearbySharingServiceImplTest,
       RegisterSendSurfaceAlreadyReceivingNotDiscovering) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  // TODO(himanshujaju) is_receiving_files_ should be set to true when
  // receiving. Test that WHEN receiving files, THEN below passes.
  // EXPECT_EQ(NearbySharingService::StatusCodes::kTransferAlreadyInProgress,
  //           RegisterSendSurface(SendSurfaceState::kForeground));
  // EXPECT_FALSE(fake_nearby_connections_manager_->IsDiscovering());
  // EXPECT_FALSE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest,
       BackgroundRegisterSendSurfaceNotDiscovering) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kBackground));
  EXPECT_FALSE(fake_nearby_connections_manager_->IsDiscovering());
  EXPECT_FALSE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest,
       DifferentSurfaceRegisterSendSurfaceTwiceKeepsDiscovering) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());

  EXPECT_EQ(
      NearbySharingService::StatusCodes::kError,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kBackground));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());
}

TEST_F(NearbySharingServiceImplTest,
       RegisterSendSurfaceEndpointFoundDiscoveryCallbackNotified) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);

  // Ensure decoder parses a valid endpoint advertisement.
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);

  // Start discovering, to ensure a discovery listener is registered.
  base::RunLoop run_loop;
  MockTransferUpdateCallback transfer_callback;
  NiceMock<MockShareTargetDiscoveredCallback> discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());

  // Discover a new endpoint, with fields set up a valid certificate.
  EXPECT_CALL(discovery_callback, OnShareTargetDiscovered)
      .WillOnce([&run_loop](ShareTarget share_target) {
        EXPECT_FALSE(share_target.is_incoming);
        EXPECT_TRUE(share_target.is_known);
        EXPECT_FALSE(share_target.has_attachments());
        EXPECT_EQ(kDeviceName, share_target.device_name);
        EXPECT_EQ(GURL(kTestMetadataIconUrl), share_target.image_url);
        EXPECT_EQ(nearby_share::mojom::ShareTargetType::kUnknown,
                  share_target.type);
        EXPECT_TRUE(share_target.device_id);
        EXPECT_NE(kEndpointId, share_target.device_id);
        EXPECT_EQ(kTestMetadataFullName, share_target.full_name);
        run_loop.Quit();
      });
  fake_nearby_connections_manager_->OnEndpointFound(
      kEndpointId,
      location::nearby::connections::mojom::DiscoveredEndpointInfo::New(
          kValidV1EndpointInfo, kServiceId));
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/true);
  run_loop.Run();

  // Register another send surface, which will automatically catch up discovered
  // endpoints.
  base::RunLoop run_loop2;
  MockTransferUpdateCallback transfer_callback2;
  NiceMock<MockShareTargetDiscoveredCallback> discovery_callback2;
  EXPECT_CALL(discovery_callback2, OnShareTargetDiscovered)
      .WillOnce([&run_loop2](ShareTarget share_target) {
        EXPECT_EQ(kDeviceName, share_target.device_name);
        run_loop2.Quit();
      });

  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback2, &discovery_callback2,
                                    SendSurfaceState::kForeground));
  run_loop2.Run();
}

TEST_F(NearbySharingServiceImplTest, RegisterSendSurfaceEmptyCertificate) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);

  // Ensure decoder parses a valid endpoint advertisement.
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);

  // Start discovering, to ensure a discovery listener is registered.
  base::RunLoop run_loop;
  MockTransferUpdateCallback transfer_callback;
  NiceMock<MockShareTargetDiscoveredCallback> discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());

  // Discover a new endpoint, with fields set up a valid certificate.
  EXPECT_CALL(discovery_callback, OnShareTargetDiscovered)
      .WillOnce([&run_loop](ShareTarget share_target) {
        EXPECT_FALSE(share_target.is_incoming);
        EXPECT_FALSE(share_target.is_known);
        EXPECT_FALSE(share_target.has_attachments());
        EXPECT_EQ(kDeviceName, share_target.device_name);
        EXPECT_FALSE(share_target.image_url);
        EXPECT_EQ(nearby_share::mojom::ShareTargetType::kUnknown,
                  share_target.type);
        EXPECT_TRUE(share_target.device_id);
        EXPECT_EQ(kEndpointId, share_target.device_id);
        EXPECT_FALSE(share_target.full_name);
        run_loop.Quit();
      });
  fake_nearby_connections_manager_->OnEndpointFound(
      kEndpointId,
      location::nearby::connections::mojom::DiscoveredEndpointInfo::New(
          kValidV1EndpointInfo, kServiceId));
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/false);
  run_loop.Run();

  // Register another send surface, which will automatically catch up discovered
  // endpoints.
  base::RunLoop run_loop2;
  MockTransferUpdateCallback transfer_callback2;
  NiceMock<MockShareTargetDiscoveredCallback> discovery_callback2;
  EXPECT_CALL(discovery_callback2, OnShareTargetDiscovered)
      .WillOnce([&run_loop2](ShareTarget share_target) {
        EXPECT_EQ(kDeviceName, share_target.device_name);
        run_loop2.Quit();
      });

  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback2, &discovery_callback2,
                                    SendSurfaceState::kForeground));
  run_loop2.Run();
}

TEST_P(NearbySharingServiceImplValidSendTest,
       RegisterSendSurfaceIsDiscovering) {
  ui::ScopedSetIdleState idle_state(GetParam().idle_state);
  is_bluetooth_present_ = GetParam().bluetooth_enabled;
  SetConnectionType(GetParam().connection_type);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());
}

INSTANTIATE_TEST_SUITE_P(NearbySharingServiceImplTest,
                         NearbySharingServiceImplValidSendTest,
                         testing::ValuesIn(kValidSendSurfaceTestData));

TEST_P(NearbySharingServiceImplInvalidSendTest,
       RegisterSendSurfaceNotDiscovering) {
  ui::ScopedSetIdleState idle_state(GetParam().idle_state);
  is_bluetooth_present_ = GetParam().bluetooth_enabled;
  SetConnectionType(GetParam().connection_type);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_FALSE(fake_nearby_connections_manager_->IsDiscovering());
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_FALSE(fake_nearby_connections_manager_->IsDiscovering());
  EXPECT_FALSE(fake_nearby_connections_manager_->IsShutdown());
}

INSTANTIATE_TEST_SUITE_P(NearbySharingServiceImplTest,
                         NearbySharingServiceImplInvalidSendTest,
                         testing::ValuesIn(kInvalidSendSurfaceTestData));

TEST_F(NearbySharingServiceImplTest, DisableFeatureSendSurfaceNotDiscovering) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  prefs_.SetBoolean(prefs::kNearbySharingEnabledPrefName, false);
  service_->FlushMojoForTesting();
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_FALSE(fake_nearby_connections_manager_->IsDiscovering());
  EXPECT_TRUE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest,
       DisableFeatureSendSurfaceStopsDiscovering) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());

  prefs_.SetBoolean(prefs::kNearbySharingEnabledPrefName, false);
  service_->FlushMojoForTesting();
  EXPECT_FALSE(fake_nearby_connections_manager_->IsDiscovering());
  EXPECT_TRUE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest, UnregisterSendSurfaceStopsDiscovering) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());

  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->UnregisterSendSurface(&transfer_callback, &discovery_callback));
  EXPECT_FALSE(fake_nearby_connections_manager_->IsDiscovering());
  EXPECT_FALSE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest,
       UnregisterSendSurfaceDifferentCallbackKeepDiscovering) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kOk,
      service_->RegisterSendSurface(&transfer_callback, &discovery_callback,
                                    SendSurfaceState::kForeground));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());

  MockTransferUpdateCallback transfer_callback2;
  MockShareTargetDiscoveredCallback discovery_callback2;
  EXPECT_EQ(NearbySharingService::StatusCodes::kError,
            service_->UnregisterSendSurface(&transfer_callback2,
                                            &discovery_callback2));
  EXPECT_TRUE(fake_nearby_connections_manager_->IsDiscovering());
}

TEST_F(NearbySharingServiceImplTest, UnregisterSendSurfaceNeverRegistered) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback transfer_callback;
  MockShareTargetDiscoveredCallback discovery_callback;
  EXPECT_EQ(
      NearbySharingService::StatusCodes::kError,
      service_->UnregisterSendSurface(&transfer_callback, &discovery_callback));
  EXPECT_FALSE(fake_nearby_connections_manager_->IsDiscovering());
}

TEST_F(NearbySharingServiceImplTest,
       ForegroundRegisterReceiveSurfaceIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_EQ(PowerLevel::kHighPower,
            fake_nearby_connections_manager_->GetAdvertisingPowerLevel());
}

TEST_F(NearbySharingServiceImplTest,
       BackgroundRegisterReceiveSurfaceIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  prefs_.SetInteger(prefs::kNearbySharingBackgroundVisibilityName,
                    static_cast<int>(Visibility::kSelectedContacts));
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kBackground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_EQ(PowerLevel::kLowPower,
            fake_nearby_connections_manager_->GetAdvertisingPowerLevel());
}

TEST_F(NearbySharingServiceImplTest,
       RegisterReceiveSurfaceTwiceSameCallbackKeepAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());

  NearbySharingService::StatusCodes result2 = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result2, NearbySharingService::StatusCodes::kError);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       RegisterReceiveSurfaceTwiceKeepAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());

  MockTransferUpdateCallback callback2;
  NearbySharingService::StatusCodes result2 = service_->RegisterReceiveSurface(
      &callback2, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result2, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       ScreenLockedRegisterReceiveSurfaceNotAdvertising) {
  ui::ScopedSetIdleState locked(ui::IDLE_STATE_LOCKED);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_FALSE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_FALSE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest,
       DataUsageChangedRegisterReceiveSurfaceRestartsAdvertising) {
  ui::ScopedSetIdleState locked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);

  prefs_.SetInteger(prefs::kNearbySharingDataUsageName,
                    static_cast<int>(DataUsage::kOffline));
  service_->FlushMojoForTesting();
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_EQ(DataUsage::kOffline,
            fake_nearby_connections_manager_->GetAdvertisingDataUsage());

  prefs_.SetInteger(prefs::kNearbySharingDataUsageName,
                    static_cast<int>(DataUsage::kOnline));
  service_->FlushMojoForTesting();
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_EQ(DataUsage::kOnline,
            fake_nearby_connections_manager_->GetAdvertisingDataUsage());
}

TEST_F(NearbySharingServiceImplTest,
       NoNetworkRegisterReceiveSurfaceIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  // Succeeds since bluetooth is present.
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       NoBluetoothNoNetworkRegisterReceiveSurfaceNotAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  is_bluetooth_present_ = false;
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_FALSE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_FALSE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest, WifiRegisterReceiveSurfaceIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       EthernetRegisterReceiveSurfaceIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_ETHERNET);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       ThreeGRegisterReceiveSurfaceIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_3G);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  // Since bluetooth is on, connection still succeeds.
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       NoBluetoothWifiReceiveSurfaceIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  is_bluetooth_present_ = false;
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       NoBluetoothEthernetReceiveSurfaceIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  is_bluetooth_present_ = false;
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_ETHERNET);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       NoBluetoothThreeGReceiveSurfaceNotAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  is_bluetooth_present_ = false;
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_3G);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_FALSE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_FALSE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest,
       DisableFeatureReceiveSurfaceNotAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  prefs_.SetBoolean(prefs::kNearbySharingEnabledPrefName, false);
  service_->FlushMojoForTesting();
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_FALSE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_TRUE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest,
       DisableFeatureReceiveSurfaceStopsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());

  prefs_.SetBoolean(prefs::kNearbySharingEnabledPrefName, false);
  service_->FlushMojoForTesting();
  EXPECT_FALSE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_TRUE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest,
       ForegroundReceiveSurfaceNoOneVisibilityIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  prefs_.SetInteger(prefs::kNearbySharingBackgroundVisibilityName,
                    static_cast<int>(Visibility::kNoOne));
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       BackgroundReceiveSurfaceNoOneVisibilityNotAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  prefs_.SetInteger(prefs::kNearbySharingBackgroundVisibilityName,
                    static_cast<int>(Visibility::kNoOne));
  service_->FlushMojoForTesting();
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kBackground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_FALSE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_FALSE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest,
       BackgroundReceiveSurfaceVisibilityToNoOneStopsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  prefs_.SetInteger(prefs::kNearbySharingBackgroundVisibilityName,
                    static_cast<int>(Visibility::kSelectedContacts));
  service_->FlushMojoForTesting();
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kBackground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());

  prefs_.SetInteger(prefs::kNearbySharingBackgroundVisibilityName,
                    static_cast<int>(Visibility::kNoOne));
  service_->FlushMojoForTesting();
  EXPECT_FALSE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_FALSE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest,
       BackgroundReceiveSurfaceVisibilityToSelectedStartsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  prefs_.SetInteger(prefs::kNearbySharingBackgroundVisibilityName,
                    static_cast<int>(Visibility::kNoOne));
  service_->FlushMojoForTesting();
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kBackground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_FALSE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_FALSE(fake_nearby_connections_manager_->IsShutdown());

  prefs_.SetInteger(prefs::kNearbySharingBackgroundVisibilityName,
                    static_cast<int>(Visibility::kSelectedContacts));
  service_->FlushMojoForTesting();
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       ForegroundReceiveSurfaceSelectedContactsVisibilityIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  prefs_.SetInteger(prefs::kNearbySharingBackgroundVisibilityName,
                    static_cast<int>(Visibility::kSelectedContacts));
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       BackgroundReceiveSurfaceSelectedContactsVisibilityIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  prefs_.SetInteger(prefs::kNearbySharingBackgroundVisibilityName,
                    static_cast<int>(Visibility::kSelectedContacts));
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kBackground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       ForegroundReceiveSurfaceAllContactsVisibilityIsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  prefs_.SetInteger(prefs::kNearbySharingBackgroundVisibilityName,
                    static_cast<int>(Visibility::kAllContacts));
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       BackgroundReceiveSurfaceAllContactsVisibilityNotAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  prefs_.SetInteger(prefs::kNearbySharingBackgroundVisibilityName,
                    static_cast<int>(Visibility::kAllContacts));
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kBackground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest, UnregisterReceiveSurfaceStopsAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());

  NearbySharingService::StatusCodes result2 =
      service_->UnregisterReceiveSurface(&callback);
  EXPECT_EQ(result2, NearbySharingService::StatusCodes::kOk);
  EXPECT_FALSE(fake_nearby_connections_manager_->IsAdvertising());
  EXPECT_FALSE(fake_nearby_connections_manager_->IsShutdown());
}

TEST_F(NearbySharingServiceImplTest,
       UnregisterReceiveSurfaceDifferentCallbackKeepAdvertising) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kForeground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());

  MockTransferUpdateCallback callback2;
  NearbySharingService::StatusCodes result2 =
      service_->UnregisterReceiveSurface(&callback2);
  EXPECT_EQ(result2, NearbySharingService::StatusCodes::kError);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest, UnregisterReceiveSurfaceNeverRegistered) {
  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  MockTransferUpdateCallback callback;
  NearbySharingService::StatusCodes result =
      service_->UnregisterReceiveSurface(&callback);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kError);
  EXPECT_FALSE(fake_nearby_connections_manager_->IsAdvertising());
}

TEST_F(NearbySharingServiceImplTest,
       IncomingConnection_ClosedReadingIntroduction) {
  fake_nearby_connections_manager_->SetRawAuthenticationToken(kEndpointId,
                                                              kToken);
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);

  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  NiceMock<MockTransferUpdateCallback> callback;
  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_)).Times(0);

  SetUpKeyVerification(sharing::mojom::PairedKeyResultFrame_Status::kSuccess);
  SetUpForegroundReceiveSurface(callback);
  service_->OnIncomingConnection(kEndpointId, kValidV1EndpointInfo,
                                 &connection_);
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/true);
  connection_.Close();

  // Introduction is ignored without any side effect.

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest,
       IncomingConnection_EmptyIntroductionFrame) {
  fake_nearby_connections_manager_->SetRawAuthenticationToken(kEndpointId,
                                                              kToken);
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);
  SetUpIntroductionFrameDecoder(/*return_empty_introduction_frame=*/true);

  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  NiceMock<MockTransferUpdateCallback> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
      .WillOnce(testing::Invoke([&run_loop](const ShareTarget& share_target,
                                            TransferMetadata metadata) {
        EXPECT_TRUE(share_target.is_incoming);
        EXPECT_TRUE(share_target.is_known);
        EXPECT_FALSE(share_target.has_attachments());
        EXPECT_EQ(kDeviceName, share_target.device_name);
        EXPECT_EQ(GURL(kTestMetadataIconUrl), share_target.image_url);
        EXPECT_EQ(nearby_share::mojom::ShareTargetType::kUnknown,
                  share_target.type);
        EXPECT_TRUE(share_target.device_id);
        EXPECT_NE(kEndpointId, share_target.device_id);
        EXPECT_EQ(kTestMetadataFullName, share_target.full_name);

        EXPECT_EQ(TransferMetadata::Status::kUnsupportedAttachmentType,
                  metadata.status());
        EXPECT_TRUE(metadata.is_final_status());
        run_loop.Quit();
      }));

  SetUpKeyVerification(sharing::mojom::PairedKeyResultFrame_Status::kSuccess);
  SetUpForegroundReceiveSurface(callback);
  service_->OnIncomingConnection(kEndpointId, kValidV1EndpointInfo,
                                 &connection_);
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/true);
  run_loop.Run();

  // Check data written to connection_.
  ExpectPairedKeyEncryptionFrame();
  ExpectPairedKeyResultFrame();
  ExpectConnectionResponseFrame(
      sharing::nearby::ConnectionResponseFrame::UNSUPPORTED_ATTACHMENT_TYPE);

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest,
       IncomingConnection_ValidIntroductionFrame_InvalidCertificate) {
  fake_nearby_connections_manager_->SetRawAuthenticationToken(kEndpointId,
                                                              kToken);
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);
  SetUpIntroductionFrameDecoder(/*return_empty_introduction_frame=*/false);

  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  NiceMock<MockTransferUpdateCallback> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
      .WillOnce(testing::Invoke([&run_loop](const ShareTarget& share_target,
                                            TransferMetadata metadata) {
        EXPECT_TRUE(share_target.is_incoming);
        EXPECT_FALSE(share_target.is_known);
        EXPECT_TRUE(share_target.has_attachments());
        EXPECT_EQ(3u, share_target.text_attachments.size());
        EXPECT_EQ(0u, share_target.file_attachments.size());
        EXPECT_EQ(kDeviceName, share_target.device_name);
        EXPECT_FALSE(share_target.image_url);
        EXPECT_EQ(nearby_share::mojom::ShareTargetType::kUnknown,
                  share_target.type);
        EXPECT_EQ(kEndpointId, share_target.device_id);
        EXPECT_FALSE(share_target.full_name);

        EXPECT_EQ(TransferMetadata::Status::kAwaitingLocalConfirmation,
                  metadata.status());
        EXPECT_FALSE(metadata.is_final_status());
        run_loop.Quit();
      }));

  SetUpKeyVerification(sharing::mojom::PairedKeyResultFrame_Status::kSuccess);
  SetUpForegroundReceiveSurface(callback);
  service_->OnIncomingConnection(kEndpointId, kValidV1EndpointInfo,
                                 &connection_);
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/false);
  run_loop.Run();

  EXPECT_FALSE(connection_.IsClosed());

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest, IncomingConnection_TimedOut) {
  NiceMock<MockTransferUpdateCallback> callback;
  ShareTarget share_target = SetUpIncomingConnection(callback);
  EXPECT_FALSE(connection_.IsClosed());

  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
      .WillOnce(testing::Invoke(
          [](const ShareTarget& share_target, TransferMetadata metadata) {
            EXPECT_EQ(TransferMetadata::Status::kTimedOut, metadata.status());
            EXPECT_TRUE(metadata.is_final_status());
          }));

  task_environment_.FastForwardBy(kReadResponseFrameTimeout +
                                  kIncomingRejectionDelay + kDelta);
  EXPECT_TRUE(connection_.IsClosed());
}

TEST_F(NearbySharingServiceImplTest,
       IncomingConnection_ClosedWaitingLocalConfirmation) {
  NiceMock<MockTransferUpdateCallback> callback;
  ShareTarget share_target = SetUpIncomingConnection(callback);

  base::RunLoop run_loop_2;
  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
      .WillOnce(testing::Invoke([&run_loop_2](const ShareTarget& share_target,
                                              TransferMetadata metadata) {
        EXPECT_EQ(TransferMetadata::Status::kFailed, metadata.status());
        EXPECT_TRUE(metadata.is_final_status());
        run_loop_2.Quit();
      }));

  connection_.Close();
  run_loop_2.Run();

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest, IncomingConnection_OutOfStorage) {
  fake_nearby_connections_manager_->SetRawAuthenticationToken(kEndpointId,
                                                              kToken);
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);

  base::FilePath file_path =
      DownloadPrefs::FromDownloadManager(
          content::BrowserContext::GetDownloadManager(profile_))
          ->DownloadPath();
  int64_t free_space = base::SysInfo::AmountOfFreeDiskSpace(file_path);

  // Might return -1 for failure.
  if (free_space < 0)
    free_space = 0;

  // Set a huge file size in introduction frame to go out of storage.
  std::string intro = "introduction_frame";
  std::vector<uint8_t> bytes(intro.begin(), intro.end());
  EXPECT_CALL(mock_decoder_, DecodeFrame(testing::Eq(bytes), testing::_))
      .WillOnce(testing::Invoke(
          [&free_space](
              const std::vector<uint8_t>& data,
              MockNearbySharingDecoder::DecodeFrameCallback callback) {
            std::vector<sharing::mojom::FileMetadataPtr> mojo_file_metadatas;
            mojo_file_metadatas.push_back(sharing::mojom::FileMetadata::New(
                "name", sharing::mojom::FileMetadata::Type::kAudio,
                /*payload_id=*/1, free_space + 1, "mime_type",
                /*id=*/123));

            sharing::mojom::V1FramePtr mojo_v1frame =
                sharing::mojom::V1Frame::New();
            mojo_v1frame->set_introduction(
                sharing::mojom::IntroductionFrame::New(
                    std::move(mojo_file_metadatas),
                    std::vector<sharing::mojom::TextMetadataPtr>(),
                    /*required_package=*/base::nullopt,
                    std::vector<sharing::mojom::WifiCredentialsMetadataPtr>()));

            sharing::mojom::FramePtr mojo_frame = sharing::mojom::Frame::New();
            mojo_frame->set_v1(std::move(mojo_v1frame));

            std::move(callback).Run(std::move(mojo_frame));
          }));
  connection_.AppendReadableData(std::move(bytes));

  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  NiceMock<MockTransferUpdateCallback> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
      .WillOnce(testing::Invoke([&run_loop](const ShareTarget& share_target,
                                            TransferMetadata metadata) {
        EXPECT_TRUE(share_target.is_incoming);
        EXPECT_TRUE(share_target.is_known);
        EXPECT_TRUE(share_target.has_attachments());
        EXPECT_EQ(0u, share_target.text_attachments.size());
        EXPECT_EQ(1u, share_target.file_attachments.size());
        EXPECT_EQ(kDeviceName, share_target.device_name);
        EXPECT_EQ(GURL(kTestMetadataIconUrl), share_target.image_url);
        EXPECT_EQ(nearby_share::mojom::ShareTargetType::kUnknown,
                  share_target.type);
        EXPECT_TRUE(share_target.device_id);
        EXPECT_NE(kEndpointId, share_target.device_id);
        EXPECT_EQ(kTestMetadataFullName, share_target.full_name);

        EXPECT_EQ(TransferMetadata::Status::kNotEnoughSpace, metadata.status());
        run_loop.Quit();
      }));

  SetUpKeyVerification(sharing::mojom::PairedKeyResultFrame_Status::kSuccess);
  SetUpForegroundReceiveSurface(callback);
  service_->OnIncomingConnection(kEndpointId, kValidV1EndpointInfo,
                                 &connection_);
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/true);
  run_loop.Run();

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest, IncomingConnection_FileSizeOverflow) {
  fake_nearby_connections_manager_->SetRawAuthenticationToken(kEndpointId,
                                                              kToken);
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);

  // Set file size sum huge to check for overflow.
  std::string intro = "introduction_frame";
  std::vector<uint8_t> bytes(intro.begin(), intro.end());
  EXPECT_CALL(mock_decoder_, DecodeFrame(testing::Eq(bytes), testing::_))
      .WillOnce(testing::Invoke(
          [](const std::vector<uint8_t>& data,
             MockNearbySharingDecoder::DecodeFrameCallback callback) {
            std::vector<sharing::mojom::FileMetadataPtr> mojo_file_metadatas;
            mojo_file_metadatas.push_back(sharing::mojom::FileMetadata::New(
                "name_1", sharing::mojom::FileMetadata::Type::kAudio,
                /*payload_id=*/1, /*size=*/std::numeric_limits<int64_t>::max(),
                "mime_type",
                /*id=*/123));
            mojo_file_metadatas.push_back(sharing::mojom::FileMetadata::New(
                "name_2", sharing::mojom::FileMetadata::Type::kVideo,
                /*payload_id=*/2, /*size=*/100, "mime_type",
                /*id=*/124));

            sharing::mojom::V1FramePtr mojo_v1frame =
                sharing::mojom::V1Frame::New();
            mojo_v1frame->set_introduction(
                sharing::mojom::IntroductionFrame::New(
                    std::move(mojo_file_metadatas),
                    std::vector<sharing::mojom::TextMetadataPtr>(),
                    /*required_package=*/base::nullopt,
                    std::vector<sharing::mojom::WifiCredentialsMetadataPtr>()));

            sharing::mojom::FramePtr mojo_frame = sharing::mojom::Frame::New();
            mojo_frame->set_v1(std::move(mojo_v1frame));

            std::move(callback).Run(std::move(mojo_frame));
          }));
  connection_.AppendReadableData(std::move(bytes));

  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  NiceMock<MockTransferUpdateCallback> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
      .WillOnce(testing::Invoke([&run_loop](const ShareTarget& share_target,
                                            TransferMetadata metadata) {
        EXPECT_TRUE(share_target.is_incoming);
        EXPECT_TRUE(share_target.is_known);
        EXPECT_EQ(kDeviceName, share_target.device_name);
        EXPECT_EQ(GURL(kTestMetadataIconUrl), share_target.image_url);
        EXPECT_EQ(nearby_share::mojom::ShareTargetType::kUnknown,
                  share_target.type);
        EXPECT_TRUE(share_target.device_id);
        EXPECT_NE(kEndpointId, share_target.device_id);
        EXPECT_EQ(kTestMetadataFullName, share_target.full_name);

        EXPECT_EQ(TransferMetadata::Status::kNotEnoughSpace, metadata.status());
        run_loop.Quit();
      }));

  SetUpKeyVerification(sharing::mojom::PairedKeyResultFrame_Status::kSuccess);
  SetUpForegroundReceiveSurface(callback);
  service_->OnIncomingConnection(kEndpointId, kValidV1EndpointInfo,
                                 &connection_);
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/true);
  run_loop.Run();

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest,
       IncomingConnection_ValidIntroductionFrame_ValidCertificate) {
  fake_nearby_connections_manager_->SetRawAuthenticationToken(kEndpointId,
                                                              kToken);
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);
  SetUpIntroductionFrameDecoder(/*return_empty_introduction_frame=*/false);

  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  NiceMock<MockTransferUpdateCallback> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
      .WillOnce(testing::Invoke([&run_loop](const ShareTarget& share_target,
                                            TransferMetadata metadata) {
        EXPECT_TRUE(share_target.is_incoming);
        EXPECT_TRUE(share_target.is_known);
        EXPECT_TRUE(share_target.has_attachments());
        EXPECT_EQ(3u, share_target.text_attachments.size());
        EXPECT_EQ(0u, share_target.file_attachments.size());
        EXPECT_EQ(kDeviceName, share_target.device_name);
        EXPECT_EQ(GURL(kTestMetadataIconUrl), share_target.image_url);
        EXPECT_EQ(nearby_share::mojom::ShareTargetType::kUnknown,
                  share_target.type);
        EXPECT_TRUE(share_target.device_id);
        EXPECT_NE(kEndpointId, share_target.device_id);
        EXPECT_EQ(kTestMetadataFullName, share_target.full_name);

        EXPECT_FALSE(metadata.token().has_value());

        EXPECT_EQ(TransferMetadata::Status::kAwaitingLocalConfirmation,
                  metadata.status());
        EXPECT_FALSE(metadata.is_final_status());
        run_loop.Quit();
      }));

  SetUpKeyVerification(sharing::mojom::PairedKeyResultFrame_Status::kSuccess);
  SetUpForegroundReceiveSurface(callback);
  service_->OnIncomingConnection(kEndpointId, kValidV1EndpointInfo,
                                 &connection_);
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/true);
  run_loop.Run();

  EXPECT_FALSE(connection_.IsClosed());

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest, AcceptInvalidShareTarget) {
  ShareTarget share_target;
  base::RunLoop run_loop;
  service_->Accept(
      share_target,
      base::BindLambdaForTesting(
          [&](NearbySharingServiceImpl::StatusCodes status_code) {
            EXPECT_EQ(NearbySharingServiceImpl::StatusCodes::kOutOfOrderApiCall,
                      status_code);
            run_loop.Quit();
          }));

  run_loop.Run();
}

TEST_F(NearbySharingServiceImplTest, AcceptValidShareTarget) {
  NiceMock<MockTransferUpdateCallback> callback;
  ShareTarget share_target = SetUpIncomingConnection(callback);

  base::RunLoop run_loop_accept;
  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
      .WillOnce(testing::Invoke(
          [](const ShareTarget& share_target, TransferMetadata metadata) {
            EXPECT_EQ(TransferMetadata::Status::kAwaitingRemoteAcceptance,
                      metadata.status());
            EXPECT_FALSE(metadata.is_final_status());
          }));

  service_->Accept(share_target,
                   base::BindLambdaForTesting(
                       [&](NearbySharingServiceImpl::StatusCodes status_code) {
                         EXPECT_EQ(NearbySharingServiceImpl::StatusCodes::kOk,
                                   status_code);
                         run_loop_accept.Quit();
                       }));

  run_loop_accept.Run();

  EXPECT_TRUE(
      fake_nearby_connections_manager_->DidUpgradeBandwidth(kEndpointId));

  // Check data written to connection_.
  ExpectPairedKeyEncryptionFrame();
  ExpectPairedKeyResultFrame();
  ExpectConnectionResponseFrame(
      sharing::nearby::ConnectionResponseFrame::ACCEPT);

  EXPECT_FALSE(connection_.IsClosed());

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest, RejectInvalidShareTarget) {
  ShareTarget share_target;
  base::RunLoop run_loop;
  service_->Reject(
      share_target,
      base::BindLambdaForTesting(
          [&](NearbySharingServiceImpl::StatusCodes status_code) {
            EXPECT_EQ(NearbySharingServiceImpl::StatusCodes::kOutOfOrderApiCall,
                      status_code);
            run_loop.Quit();
          }));

  run_loop.Run();
}

TEST_F(NearbySharingServiceImplTest, RejectValidShareTarget) {
  NiceMock<MockTransferUpdateCallback> callback;
  ShareTarget share_target = SetUpIncomingConnection(callback);

  base::RunLoop run_loop_reject;
  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
      .WillOnce(testing::Invoke(
          [](const ShareTarget& share_target, TransferMetadata metadata) {
            EXPECT_EQ(TransferMetadata::Status::kRejected, metadata.status());
            EXPECT_TRUE(metadata.is_final_status());
          }));

  service_->Reject(share_target,
                   base::BindLambdaForTesting(
                       [&](NearbySharingServiceImpl::StatusCodes status_code) {
                         EXPECT_EQ(NearbySharingServiceImpl::StatusCodes::kOk,
                                   status_code);
                         run_loop_reject.Quit();
                       }));

  run_loop_reject.Run();

  // Check data written to connection_.
  ExpectPairedKeyEncryptionFrame();
  ExpectPairedKeyResultFrame();
  ExpectConnectionResponseFrame(
      sharing::nearby::ConnectionResponseFrame::REJECT);

  task_environment_.FastForwardBy(kIncomingRejectionDelay + kDelta);
  EXPECT_TRUE(connection_.IsClosed());

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest,
       IncomingConnection_KeyVerificationRunnerStatusUnable) {
  fake_nearby_connections_manager_->SetRawAuthenticationToken(kEndpointId,
                                                              kToken);
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);
  SetUpIntroductionFrameDecoder(/*return_empty_introduction_frame=*/false);

  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  NiceMock<MockTransferUpdateCallback> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
      .WillOnce(testing::Invoke([&run_loop](const ShareTarget& share_target,
                                            TransferMetadata metadata) {
        EXPECT_TRUE(share_target.is_incoming);
        EXPECT_TRUE(share_target.is_known);
        EXPECT_TRUE(share_target.has_attachments());
        EXPECT_EQ(3u, share_target.text_attachments.size());
        EXPECT_EQ(0u, share_target.file_attachments.size());
        EXPECT_EQ(kDeviceName, share_target.device_name);
        EXPECT_EQ(GURL(kTestMetadataIconUrl), share_target.image_url);
        EXPECT_EQ(nearby_share::mojom::ShareTargetType::kUnknown,
                  share_target.type);
        EXPECT_TRUE(share_target.device_id);
        EXPECT_NE(kEndpointId, share_target.device_id);
        EXPECT_EQ(kTestMetadataFullName, share_target.full_name);

        EXPECT_EQ(kFourDigitToken, metadata.token());
        EXPECT_EQ(TransferMetadata::Status::kAwaitingLocalConfirmation,
                  metadata.status());
        EXPECT_FALSE(metadata.is_final_status());
        run_loop.Quit();
      }));

  SetUpKeyVerification(sharing::mojom::PairedKeyResultFrame_Status::kUnable);
  SetUpForegroundReceiveSurface(callback);

  service_->OnIncomingConnection(kEndpointId, kValidV1EndpointInfo,
                                 &connection_);
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/true);
  run_loop.Run();

  EXPECT_TRUE(
      fake_nearby_connections_manager_->DidUpgradeBandwidth(kEndpointId));

  EXPECT_FALSE(connection_.IsClosed());

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest,
       IncomingConnection_KeyVerificationRunnerStatusUnable_LowPower) {
  fake_nearby_connections_manager_->SetRawAuthenticationToken(kEndpointId,
                                                              kToken);
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);
  SetUpIntroductionFrameDecoder(/*return_empty_introduction_frame=*/false);

  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  NiceMock<MockTransferUpdateCallback> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, OnTransferUpdate(testing::_, testing::_))
      .WillOnce(testing::Invoke([&run_loop](const ShareTarget& share_target,
                                            TransferMetadata metadata) {
        EXPECT_TRUE(share_target.is_incoming);
        EXPECT_TRUE(share_target.is_known);
        EXPECT_TRUE(share_target.has_attachments());
        EXPECT_EQ(3u, share_target.text_attachments.size());
        EXPECT_EQ(0u, share_target.file_attachments.size());
        EXPECT_EQ(kDeviceName, share_target.device_name);
        EXPECT_EQ(GURL(kTestMetadataIconUrl), share_target.image_url);
        EXPECT_EQ(nearby_share::mojom::ShareTargetType::kUnknown,
                  share_target.type);
        EXPECT_TRUE(share_target.device_id);
        EXPECT_NE(kEndpointId, share_target.device_id);
        EXPECT_EQ(kTestMetadataFullName, share_target.full_name);

        EXPECT_EQ(kFourDigitToken, metadata.token());
        EXPECT_EQ(TransferMetadata::Status::kAwaitingLocalConfirmation,
                  metadata.status());
        EXPECT_FALSE(metadata.is_final_status());
        run_loop.Quit();
      }));

  SetUpKeyVerification(sharing::mojom::PairedKeyResultFrame_Status::kUnable);

  NearbySharingService::StatusCodes result = service_->RegisterReceiveSurface(
      &callback, NearbySharingService::ReceiveSurfaceState::kBackground);
  EXPECT_EQ(result, NearbySharingService::StatusCodes::kOk);
  EXPECT_TRUE(fake_nearby_connections_manager_->IsAdvertising());

  service_->OnIncomingConnection(kEndpointId, kValidV1EndpointInfo,
                                 &connection_);
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/true);
  run_loop.Run();

  EXPECT_FALSE(
      fake_nearby_connections_manager_->DidUpgradeBandwidth(kEndpointId));

  EXPECT_FALSE(connection_.IsClosed());

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest,
       IncomingConnection_KeyVerificationRunnerStatusFail) {
  fake_nearby_connections_manager_->SetRawAuthenticationToken(kEndpointId,
                                                              kToken);
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);

  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  NiceMock<MockTransferUpdateCallback> callback;

  SetUpKeyVerification(sharing::mojom::PairedKeyResultFrame_Status::kFail);
  SetUpForegroundReceiveSurface(callback);

  // Ensures that introduction is never received for failed key verification.
  std::string intro = "introduction_frame";
  std::vector<uint8_t> bytes(intro.begin(), intro.end());
  EXPECT_CALL(mock_decoder_, DecodeFrame(testing::Eq(bytes), testing::_))
      .Times(0);
  connection_.AppendReadableData(bytes);

  service_->OnIncomingConnection(kEndpointId, kValidV1EndpointInfo,
                                 &connection_);
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/true);

  EXPECT_TRUE(connection_.IsClosed());

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}

TEST_F(NearbySharingServiceImplTest,
       IncomingConnection_EmptyAuthToken_KeyVerificationRunnerStatusFail) {
  SetUpAdvertisementDecoder(kValidV1EndpointInfo,
                            /*return_empty_advertisement=*/false);

  ui::ScopedSetIdleState unlocked(ui::IDLE_STATE_IDLE);
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI);
  NiceMock<MockTransferUpdateCallback> callback;

  SetUpForegroundReceiveSurface(callback);

  // Ensures that introduction is never received for empty auth token.
  std::string intro = "introduction_frame";
  std::vector<uint8_t> bytes(intro.begin(), intro.end());
  EXPECT_CALL(mock_decoder_, DecodeFrame(testing::Eq(bytes), testing::_))
      .Times(0);
  connection_.AppendReadableData(bytes);

  service_->OnIncomingConnection(kEndpointId, kValidV1EndpointInfo,
                                 &connection_);
  ProcessLatestPublicCertificateDecryption(/*expected_num_calls=*/1,
                                           /*success=*/true);

  EXPECT_TRUE(connection_.IsClosed());

  // To avoid UAF in OnIncomingTransferUpdate().
  service_->UnregisterReceiveSurface(&callback);
}
