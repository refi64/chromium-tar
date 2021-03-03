// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/power/peripheral_battery_notifier.h"

#include <memory>

#include "ash/shell.h"
#include "ash/system/power/peripheral_battery_listener.h"
#include "ash/system/power/peripheral_battery_tests.h"
#include "ash/test/ash_test_base.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/simple_test_tick_clock.h"
#include "ui/events/devices/device_data_manager_test_api.h"
#include "ui/events/devices/touchscreen_device.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/public/cpp/notification.h"

namespace {

const base::string16& NotificationMessagePrefix() {
  static const base::string16 prefix(base::ASCIIToUTF16("Battery low ("));
  return prefix;
}

const base::string16& NotificationMessageSuffix() {
  static const base::string16 suffix(base::ASCIIToUTF16("%)"));
  return suffix;
}

}  // namespace

namespace ash {

class PeripheralBatteryNotifierTest : public AshTestBase {
 public:
  PeripheralBatteryNotifierTest() = default;
  PeripheralBatteryNotifierTest(const PeripheralBatteryNotifierTest&) = delete;
  PeripheralBatteryNotifierTest& operator=(
      const PeripheralBatteryNotifierTest&) = delete;
  ~PeripheralBatteryNotifierTest() override = default;

  void SetUp() override {
    AshTestBase::SetUp();

    message_center_ = message_center::MessageCenter::Get();

    battery_listener_ = std::make_unique<PeripheralBatteryListener>();
    battery_notifier_ =
        std::make_unique<PeripheralBatteryNotifier>(battery_listener_.get());
    // No notifications should have been posted yet.
    ASSERT_EQ(0u, message_center_->NotificationCount());
  }

  void TearDown() override {
    battery_notifier_.reset();
    battery_listener_.reset();
    AshTestBase::TearDown();
  }

  // Extracts the battery percentage from the message of a notification.
  uint8_t ExtractBatteryPercentage(message_center::Notification* notification) {
    const base::string16& message = notification->message();
    EXPECT_TRUE(base::StartsWith(message, NotificationMessagePrefix(),
                                 base::CompareCase::SENSITIVE));
    EXPECT_TRUE(base::EndsWith(message, NotificationMessageSuffix(),
                               base::CompareCase::SENSITIVE));

    int prefix_size = NotificationMessagePrefix().size();
    int suffix_size = NotificationMessageSuffix().size();
    int key_len = message.size() - prefix_size - suffix_size;
    EXPECT_GT(key_len, 0);

    int battery_percentage;
    EXPECT_TRUE(base::StringToInt(message.substr(prefix_size, key_len),
                                  &battery_percentage));
    EXPECT_GE(battery_percentage, 0);
    EXPECT_LE(battery_percentage, 100);
    return battery_percentage;
  }

  void SetTestingClock(base::SimpleTestTickClock* clock) {
    battery_notifier_->clock_ = clock;
    battery_listener_->clock_ = clock;
  }

  base::TimeTicks GetTestingClock() {
    // TODO(crbug/1153985): the next line should use clock_->NowTicks().
    return base::TimeTicks();
  }

  void UpdateBatteryLevel(bool add_first,
                          const std::string key,
                          const std::string name,
                          base::Optional<uint8_t> level,
                          bool is_stylus,
                          const std::string btaddr) {
    PeripheralBatteryListener::BatteryInfo info(key, base::ASCIIToUTF16(name),
                                                level, GetTestingClock(),
                                                is_stylus, btaddr);
    if (add_first)
      battery_notifier_->OnAddingBattery(info);
    battery_notifier_->OnUpdatedBatteryLevel(info);
  }

  void RemoveBattery(const std::string key,
                     const std::string name,
                     base::Optional<uint8_t> level,
                     bool is_stylus,
                     const std::string btaddr) {
    PeripheralBatteryListener::BatteryInfo info(key, base::ASCIIToUTF16(name),
                                                level, GetTestingClock(),
                                                is_stylus, btaddr);
    battery_notifier_->OnRemovingBattery(info);
  }

 protected:
  message_center::MessageCenter* message_center_;
  std::unique_ptr<PeripheralBatteryNotifier> battery_notifier_;
  std::unique_ptr<PeripheralBatteryListener> battery_listener_;
};

TEST_F(PeripheralBatteryNotifierTest, Basic) {
  base::SimpleTestTickClock clock;
  SetTestingClock(&clock);

  // Level 50 at time 100, no low-battery notification.
  clock.Advance(base::TimeDelta::FromSeconds(100));
  UpdateBatteryLevel(true, kTestBatteryId, kTestDeviceName, 50, false,
                     kTestBatteryAddress);
  EXPECT_EQ(1u,
            battery_notifier_->battery_notifications_.count(kTestBatteryId));

  const PeripheralBatteryNotifier::NotificationInfo& info =
      battery_notifier_->battery_notifications_[kTestBatteryId];

  EXPECT_EQ(base::nullopt, info.level);
  EXPECT_EQ(GetTestingClock(), info.last_notification_timestamp);
  EXPECT_FALSE(
      message_center_->FindVisibleNotificationById(kTestBatteryNotificationId));

  // Level 5 at time 110, low-battery notification.
  clock.Advance(base::TimeDelta::FromSeconds(10));
  UpdateBatteryLevel(false, kTestBatteryId, kTestDeviceName, 5, false,
                     kTestBatteryAddress);
  EXPECT_EQ(5, info.level);

  // TODO(crbug/1153985): the next line should use GetTestingClock().
  EXPECT_EQ(clock.NowTicks(), info.last_notification_timestamp);
  EXPECT_TRUE(
      message_center_->FindVisibleNotificationById(kTestBatteryNotificationId));

  // Verify that the low-battery notification for stylus does not show up.
  EXPECT_FALSE(message_center_->FindVisibleNotificationById(
      PeripheralBatteryNotifier::kStylusNotificationId));

  // Level -1 at time 115, cancel previous notification.
  clock.Advance(base::TimeDelta::FromSeconds(5));
  UpdateBatteryLevel(false, kTestBatteryId, kTestDeviceName, base::nullopt,
                     false, kTestBatteryAddress);
  EXPECT_EQ(base::nullopt, info.level);
  // TODO(crbug/1153985): the next line should use GetTestingClock()
  EXPECT_EQ(clock.NowTicks() - base::TimeDelta::FromSeconds(5),
            info.last_notification_timestamp);
  EXPECT_FALSE(
      message_center_->FindVisibleNotificationById(kTestBatteryNotificationId));

  // Level 50 at time 120, no low-battery notification.
  clock.Advance(base::TimeDelta::FromSeconds(5));
  UpdateBatteryLevel(false, kTestBatteryId, kTestDeviceName, 50, false,
                     kTestBatteryAddress);
  EXPECT_EQ(base::nullopt, info.level);
  // TODO(crbug/1153985): the next line should use GetTestingClock()
  EXPECT_EQ(clock.NowTicks() - base::TimeDelta::FromSeconds(10),
            info.last_notification_timestamp);
  EXPECT_FALSE(
      message_center_->FindVisibleNotificationById(kTestBatteryNotificationId));

  // Level 5 at time 130, no low-battery notification (throttling).
  clock.Advance(base::TimeDelta::FromSeconds(10));
  UpdateBatteryLevel(false, kTestBatteryId, kTestDeviceName, 5, false,
                     kTestBatteryAddress);
  EXPECT_EQ(5, info.level);
  // TODO(crbug/1153985): the next line should use GetTestingClock().
  EXPECT_EQ(clock.NowTicks() - base::TimeDelta::FromSeconds(20),
            info.last_notification_timestamp);
  EXPECT_FALSE(
      message_center_->FindVisibleNotificationById(kTestBatteryNotificationId));
}

TEST_F(PeripheralBatteryNotifierTest, StylusNotification) {
  const std::string kTestStylusBatteryPath =
      "/sys/class/power_supply/hid-AAAA:BBBB:CCCC.DDDD-battery";
  const std::string kTestStylusBatteryId =
      "???hxxxxid-AAAA:BBBB:CCCC.DDDD-battery";
  const std::string kTestStylusName = "test_stylus";

  // Add an external stylus to our test device manager.
  ui::TouchscreenDevice stylus(/*id=*/0, ui::INPUT_DEVICE_USB, kTestStylusName,
                               gfx::Size(),
                               /*touch_points=*/1, /*has_stylus=*/true);
  stylus.sys_path = base::FilePath(kTestStylusBatteryPath);

  ui::DeviceDataManagerTestApi().SetTouchscreenDevices({stylus});

  // Verify that when the battery level is 50, no stylus low battery
  // notification is shown.
  UpdateBatteryLevel(true, kTestStylusBatteryId, kTestStylusName, 50, true, "");
  EXPECT_FALSE(message_center_->FindVisibleNotificationById(
      PeripheralBatteryNotifier::kStylusNotificationId));

  // Verify that when the battery level is 5, a stylus low battery notification
  // is shown. Also check that a non stylus device low battery notification will
  // not show up.
  UpdateBatteryLevel(false, kTestStylusBatteryId, kTestStylusName, 5, true, "");
  EXPECT_TRUE(message_center_->FindVisibleNotificationById(
      PeripheralBatteryNotifier::kStylusNotificationId));
  EXPECT_FALSE(
      message_center_->FindVisibleNotificationById(kTestBatteryAddress));

  // Verify that when the battery level is -1, the previous stylus low battery
  // notification is cancelled.
  UpdateBatteryLevel(false, kTestStylusBatteryId, kTestStylusName,
                     base::nullopt, true, "");
  EXPECT_FALSE(message_center_->FindVisibleNotificationById(
      PeripheralBatteryNotifier::kStylusNotificationId));
}

TEST_F(PeripheralBatteryNotifierTest,
       Bluetooth_CreatesANotificationForEachDevice) {
  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1, 5, false,
                     kBluetoothDeviceAddress1);
  UpdateBatteryLevel(true, kBluetoothDeviceId2, kBluetoothDeviceName2, 0, false,
                     kBluetoothDeviceAddress2);

  // Verify 2 notifications were posted with the correct values.
  EXPECT_EQ(2u, message_center_->NotificationCount());
  message_center::Notification* notification_1 =
      message_center_->FindVisibleNotificationById(
          kBluetoothDeviceNotificationId1);
  message_center::Notification* notification_2 =
      message_center_->FindVisibleNotificationById(
          kBluetoothDeviceNotificationId2);

  EXPECT_TRUE(notification_1);
  EXPECT_EQ(base::ASCIIToUTF16(kBluetoothDeviceName1), notification_1->title());
  EXPECT_EQ(5, ExtractBatteryPercentage(notification_1));
  EXPECT_TRUE(notification_2);
  EXPECT_EQ(base::ASCIIToUTF16(kBluetoothDeviceName2), notification_2->title());
  EXPECT_EQ(0, ExtractBatteryPercentage(notification_2));
}

TEST_F(PeripheralBatteryNotifierTest,
       Bluetooth_RemovesNotificationForDisconnectedDevices) {
  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1, 5, false,
                     kBluetoothDeviceAddress1);
  UpdateBatteryLevel(true, kBluetoothDeviceId2, kBluetoothDeviceName2, 0, false,
                     kBluetoothDeviceAddress2);

  // Verify 2 notifications were posted.
  EXPECT_EQ(2u, message_center_->NotificationCount());

  // Verify only the notification for device 1 gets removed.
  RemoveBattery(kBluetoothDeviceId1, kBluetoothDeviceName1, 5, false,
                kBluetoothDeviceAddress1);
  EXPECT_EQ(1u, message_center_->NotificationCount());
  EXPECT_TRUE(message_center_->FindVisibleNotificationById(
      kBluetoothDeviceNotificationId2));

  // Remove the second notification.
  RemoveBattery(kBluetoothDeviceId2, kBluetoothDeviceName2, 0, false,
                kBluetoothDeviceAddress2);
  EXPECT_EQ(0u, message_center_->NotificationCount());
}

TEST_F(PeripheralBatteryNotifierTest,
       Bluetooth_CancelNotificationForInvalidBatteryLevel) {
  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1, 1, false,
                     kBluetoothDeviceAddress1);
  EXPECT_TRUE(message_center_->FindVisibleNotificationById(
      kBluetoothDeviceNotificationId1));

  // The notification should get canceled.
  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1,
                     base::nullopt, false, kBluetoothDeviceAddress1);
  EXPECT_FALSE(message_center_->FindVisibleNotificationById(
      kBluetoothDeviceNotificationId1));
}

// Don't post a notification if the battery level drops again under the
// threshold before kNotificationInterval is completed.
TEST_F(PeripheralBatteryNotifierTest,
       DontShowSecondNotificationWithinASmallTimeInterval) {
  base::SimpleTestTickClock clock;
  SetTestingClock(&clock);
  clock.Advance(base::TimeDelta::FromSeconds(100));

  // Post a notification.
  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1, 1, false,
                     kBluetoothDeviceAddress1);
  EXPECT_TRUE(message_center_->FindVisibleNotificationById(
      kBluetoothDeviceNotificationId1));

  // Cancel the notification.
  clock.Advance(base::TimeDelta::FromSeconds(1));
  UpdateBatteryLevel(false, kBluetoothDeviceId1, kBluetoothDeviceName1,
                     base::nullopt, false, kBluetoothDeviceAddress1);
  EXPECT_FALSE(message_center_->FindVisibleNotificationById(
      kBluetoothDeviceNotificationId1));

  // The battery level falls below the threshold after a short time period. No
  // notification should get posted.
  clock.Advance(base::TimeDelta::FromSeconds(1));
  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1, 1, false,
                     kBluetoothDeviceAddress1);
  EXPECT_FALSE(message_center_->FindVisibleNotificationById(
      kBluetoothDeviceNotificationId1));
}

// Post a notification if the battery is under threshold, then unknown level and
// then is again under the threshold after kNotificationInterval is completed.
TEST_F(PeripheralBatteryNotifierTest,
       PostNotificationIfBatteryGoesFromUnknownLevelToBelowThreshold) {
  base::SimpleTestTickClock clock;
  SetTestingClock(&clock);
  clock.Advance(base::TimeDelta::FromSeconds(100));

  // Post a notification.
  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1, 1, false,
                     kBluetoothDeviceAddress1);
  EXPECT_TRUE(message_center_->FindVisibleNotificationById(
      kBluetoothDeviceNotificationId1));

  // Cancel the notification.
  clock.Advance(base::TimeDelta::FromSeconds(1));
  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1,
                     base::nullopt, false, kBluetoothDeviceAddress1);
  EXPECT_FALSE(message_center_->FindVisibleNotificationById(
      kBluetoothDeviceNotificationId1));

  // Post notification if we are out of the kNotificationInterval.
  clock.Advance(base::TimeDelta::FromSeconds(100));
  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1, 1, false,
                     kBluetoothDeviceAddress1);
  EXPECT_TRUE(message_center_->FindVisibleNotificationById(
      kBluetoothDeviceNotificationId1));
}

// Don't Post another notification if the battery level keeps low and the user
// dismissed the previous notification.
TEST_F(PeripheralBatteryNotifierTest,
       DontRepostNotificationIfUserDismissedPreviousOne) {
  base::SimpleTestTickClock clock;
  SetTestingClock(&clock);
  clock.Advance(base::TimeDelta::FromSeconds(100));

  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1, 5, false,
                     kBluetoothDeviceAddress1);
  EXPECT_EQ(1u, message_center_->NotificationCount());

  // Simulate the user clears the notification.
  message_center_->RemoveAllNotifications(
      /*by_user=*/true, message_center::MessageCenter::RemoveType::ALL);

  // The battery level remains low, but shouldn't post a notificaiton.
  clock.Advance(base::TimeDelta::FromSeconds(100));
  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1, 5, false,
                     kBluetoothDeviceAddress1);
  EXPECT_EQ(0u, message_center_->NotificationCount());
}

// If there is an existing notificaiton and the battery level remains low,
// update its content.
TEST_F(PeripheralBatteryNotifierTest, UpdateNotificationIfVisible) {
  base::SimpleTestTickClock clock;
  SetTestingClock(&clock);
  clock.Advance(base::TimeDelta::FromSeconds(100));

  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1, 5, false,
                     kBluetoothDeviceAddress1);
  EXPECT_EQ(1u, message_center_->NotificationCount());

  // The battery level remains low, should update the notification.
  clock.Advance(base::TimeDelta::FromSeconds(100));
  UpdateBatteryLevel(true, kBluetoothDeviceId1, kBluetoothDeviceName1, 3, false,
                     kBluetoothDeviceAddress1);
  message_center::Notification* notification =
      message_center_->FindVisibleNotificationById(
          kBluetoothDeviceNotificationId1);
  EXPECT_TRUE(notification);
  EXPECT_EQ(base::ASCIIToUTF16(kBluetoothDeviceName1), notification->title());
  EXPECT_EQ(3, ExtractBatteryPercentage(notification));
}

}  // namespace ash
