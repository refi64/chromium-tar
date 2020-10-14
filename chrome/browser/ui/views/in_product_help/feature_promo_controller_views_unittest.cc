// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/in_product_help/feature_promo_controller_views.h"

#include "base/bind.h"
#include "base/feature_list.h"
#include "base/optional.h"
#include "chrome/browser/feature_engagement/tracker_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/chrome_view_class_properties.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/test_with_browser_view.h"
#include "chrome/browser/ui/views/in_product_help/feature_promo_bubble_params.h"
#include "chrome/browser/ui/views/in_product_help/feature_promo_bubble_view.h"
#include "chrome/browser/ui/views/toolbar/browser_app_menu_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/testing_profile.h"
#include "components/feature_engagement/test/mock_tracker.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/test/widget_test.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::NiceMock;
using ::testing::Ref;
using ::testing::Return;

namespace {
base::Feature kTestIPHFeature{"TestIPHFeature",
                              base::FEATURE_ENABLED_BY_DEFAULT};
}  // namespace

class FeaturePromoControllerViewsTest : public TestWithBrowserView {
 public:
  void SetUp() override {
    TestWithBrowserView::SetUp();
    controller_ = browser_view()->feature_promo_controller();

    mock_tracker_ =
        static_cast<NiceMock<feature_engagement::test::MockTracker>*>(
            feature_engagement::TrackerFactory::GetForBrowserContext(
                profile()));
  }

  TestingProfile::TestingFactories GetTestingFactories() override {
    TestingProfile::TestingFactories factories =
        TestWithBrowserView::GetTestingFactories();
    factories.emplace_back(
        feature_engagement::TrackerFactory::GetInstance(),
        base::BindRepeating(FeaturePromoControllerViewsTest::MakeTestTracker));
    return factories;
  }

 protected:
  views::View* GetAnchorView() {
    return browser_view()->toolbar()->app_menu_button();
  }

  FeaturePromoBubbleParams DefaultBubbleParams() {
    FeaturePromoBubbleParams params;
    params.body_string_specifier = IDS_REOPEN_TAB_PROMO;
    params.anchor_view = GetAnchorView();
    params.arrow = views::BubbleBorder::TOP_RIGHT;
    return params;
  }

  FeaturePromoControllerViews* controller_;
  NiceMock<feature_engagement::test::MockTracker>* mock_tracker_;

 private:
  static std::unique_ptr<KeyedService> MakeTestTracker(
      content::BrowserContext* context) {
    auto tracker =
        std::make_unique<NiceMock<feature_engagement::test::MockTracker>>();

    // Allow other code to call into the tracker.
    EXPECT_CALL(*tracker, NotifyEvent(_)).Times(AnyNumber());
    EXPECT_CALL(*tracker, ShouldTriggerHelpUI(_))
        .Times(AnyNumber())
        .WillRepeatedly(Return(false));

    return tracker;
  }
};

TEST_F(FeaturePromoControllerViewsTest, AsksBackendToShowPromo) {
  EXPECT_CALL(*mock_tracker_, ShouldTriggerHelpUI(Ref(kTestIPHFeature)))
      .Times(1)
      .WillOnce(Return(false));
  EXPECT_FALSE(
      controller_->MaybeShowPromo(kTestIPHFeature, DefaultBubbleParams()));
  EXPECT_FALSE(controller_->BubbleIsShowing(kTestIPHFeature));
  EXPECT_FALSE(controller_->promo_bubble_for_testing());
}

TEST_F(FeaturePromoControllerViewsTest, ShowsBubble) {
  EXPECT_CALL(*mock_tracker_, ShouldTriggerHelpUI(Ref(kTestIPHFeature)))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_TRUE(
      controller_->MaybeShowPromo(kTestIPHFeature, DefaultBubbleParams()));
  EXPECT_TRUE(controller_->BubbleIsShowing(kTestIPHFeature));
  EXPECT_TRUE(controller_->promo_bubble_for_testing());
}

TEST_F(FeaturePromoControllerViewsTest, PromoEndsWhenRequested) {
  EXPECT_CALL(*mock_tracker_, ShouldTriggerHelpUI(Ref(kTestIPHFeature)))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_tracker_, Dismissed(Ref(kTestIPHFeature))).Times(0);
  ASSERT_TRUE(
      controller_->MaybeShowPromo(kTestIPHFeature, DefaultBubbleParams()));

  // Only valid before the widget is closed.
  FeaturePromoBubbleView* const bubble =
      controller_->promo_bubble_for_testing();
  ASSERT_TRUE(bubble);

  EXPECT_TRUE(controller_->BubbleIsShowing(kTestIPHFeature));
  views::test::WidgetClosingObserver widget_observer(bubble->GetWidget());

  EXPECT_CALL(*mock_tracker_, Dismissed(Ref(kTestIPHFeature))).Times(1);
  controller_->CloseBubble(kTestIPHFeature);
  EXPECT_FALSE(controller_->BubbleIsShowing(kTestIPHFeature));
  EXPECT_FALSE(controller_->promo_bubble_for_testing());

  // Ensure the widget does close.
  widget_observer.Wait();
}

TEST_F(FeaturePromoControllerViewsTest, PromoEndsOnBubbleClosure) {
  EXPECT_CALL(*mock_tracker_, ShouldTriggerHelpUI(Ref(kTestIPHFeature)))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_tracker_, Dismissed(Ref(kTestIPHFeature))).Times(0);
  ASSERT_TRUE(
      controller_->MaybeShowPromo(kTestIPHFeature, DefaultBubbleParams()));

  // Only valid before the widget is closed.
  FeaturePromoBubbleView* const bubble =
      controller_->promo_bubble_for_testing();
  ASSERT_TRUE(bubble);

  EXPECT_TRUE(controller_->BubbleIsShowing(kTestIPHFeature));
  views::test::WidgetClosingObserver widget_observer(bubble->GetWidget());

  EXPECT_CALL(*mock_tracker_, Dismissed(Ref(kTestIPHFeature))).Times(1);
  bubble->GetWidget()->Close();
  widget_observer.Wait();

  EXPECT_FALSE(controller_->BubbleIsShowing(kTestIPHFeature));
  EXPECT_FALSE(controller_->promo_bubble_for_testing());
}

TEST_F(FeaturePromoControllerViewsTest, ContinuedPromoDefersBackendDismissed) {
  EXPECT_CALL(*mock_tracker_, ShouldTriggerHelpUI(Ref(kTestIPHFeature)))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_tracker_, Dismissed(Ref(kTestIPHFeature))).Times(0);
  ASSERT_TRUE(
      controller_->MaybeShowPromo(kTestIPHFeature, DefaultBubbleParams()));

  // Only valid before the widget is closed.
  FeaturePromoBubbleView* const bubble =
      controller_->promo_bubble_for_testing();
  ASSERT_TRUE(bubble);

  EXPECT_TRUE(controller_->BubbleIsShowing(kTestIPHFeature));
  views::test::WidgetClosingObserver widget_observer(bubble->GetWidget());

  // First check that CloseBubbleAndContinuePromo() actually closes the
  // bubble, but doesn't yet tell the backend the promo finished.

  base::Optional<FeaturePromoController::PromoHandle> promo_handle =
      controller_->CloseBubbleAndContinuePromo(kTestIPHFeature);
  EXPECT_FALSE(controller_->BubbleIsShowing(kTestIPHFeature));
  EXPECT_FALSE(controller_->promo_bubble_for_testing());

  // Ensure the widget does close.
  widget_observer.Wait();

  // Check handle destruction causes the backend to be notified.

  EXPECT_CALL(*mock_tracker_, Dismissed(Ref(kTestIPHFeature))).Times(1);
  promo_handle.reset();
}

TEST_F(FeaturePromoControllerViewsTest,
       PropertySetOnAnchorViewWhileBubbleOpen) {
  EXPECT_CALL(*mock_tracker_, ShouldTriggerHelpUI(Ref(kTestIPHFeature)))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_FALSE(GetAnchorView()->GetProperty(kHasInProductHelpPromoKey));

  ASSERT_TRUE(
      controller_->MaybeShowPromo(kTestIPHFeature, DefaultBubbleParams()));
  EXPECT_TRUE(GetAnchorView()->GetProperty(kHasInProductHelpPromoKey));

  controller_->CloseBubble(kTestIPHFeature);
  EXPECT_FALSE(GetAnchorView()->GetProperty(kHasInProductHelpPromoKey));
}
