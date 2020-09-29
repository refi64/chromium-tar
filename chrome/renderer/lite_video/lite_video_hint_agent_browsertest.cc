// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/lite_video/lite_video_hint_agent.h"

#include <memory>

#include "base/macros.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/common/chrome_features.h"
#include "chrome/renderer/lite_video/lite_video_url_loader_throttle.h"
#include "chrome/test/base/chrome_render_view_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_view.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/platform/web_network_state_notifier.h"

namespace lite_video {

namespace {
constexpr char kTestURL[] = "https://litevideo.test.com";

}

// Encapsulates the media URLLoader throttle, its delegate, and maintains the
// current throttling state.
class MediaLoaderThrottleInfo : public blink::URLLoaderThrottle::Delegate {
 public:
  explicit MediaLoaderThrottleInfo(
      std::unique_ptr<LiteVideoURLLoaderThrottle> throttle)
      : throttle_(std::move(throttle)) {
    throttle_->set_delegate(this);
  }

  void SendResponse(network::mojom::URLResponseHead* response_head) {
    throttle_->WillProcessResponse(GURL(kTestURL), response_head,
                                   &is_throttled_);
  }

  // Implements blink::URLLoaderThrottle::Delegate.
  void CancelWithError(int error_code,
                       base::StringPiece custom_reason) override {
    NOTIMPLEMENTED();
  }
  void Resume() override {
    ASSERT_TRUE(is_throttled_);
    is_throttled_ = false;
  }

  bool is_throttled() { return is_throttled_; }

 private:
  // Current throttling state.
  bool is_throttled_ = false;

  std::unique_ptr<LiteVideoURLLoaderThrottle> throttle_;
};

class LiteVideoHintAgentTest : public ChromeRenderViewTest {
 public:
  void DisableLiteVideoFeature() {
    scoped_feature_list_.Reset();
    scoped_feature_list_.InitAndDisableFeature(features::kLiteVideo);
  }

  std::unique_ptr<LiteVideoURLLoaderThrottle> CreateLiteVideoURLLoaderThrottle(
      blink::mojom::RequestContextType request_context_type) {
    blink::WebURLRequest request;
    request.SetUrl(GURL(kTestURL));
    request.SetRequestContext(request_context_type);
    return LiteVideoURLLoaderThrottle::MaybeCreateThrottle(
        request, view_->GetMainRenderFrame()->GetRoutingID());
  }

  std::unique_ptr<MediaLoaderThrottleInfo> CreateThrottleAndSendResponse(
      net::HttpStatusCode response_code,
      const std::string& mime_type,
      int content_length) {
    auto throttle_info = std::make_unique<MediaLoaderThrottleInfo>(
        CreateLiteVideoURLLoaderThrottle(
            blink::mojom::RequestContextType::FETCH));
    network::mojom::URLResponseHeadPtr response_head =
        network::CreateURLResponseHead(response_code);
    response_head->mime_type = mime_type;
    response_head->mime_type = mime_type;
    response_head->content_length = content_length;
    response_head->network_accessed = true;
    response_head->was_fetched_via_cache = false;

    throttle_info->SendResponse(response_head.get());
    return throttle_info;
  }

  const std::set<LiteVideoURLLoaderThrottle*>& GetActiveThrottledResponses()
      const {
    return lite_video_hint_agent_->GetActiveThrottlesForTesting();
  }

  const base::HistogramTester& histogram_tester() { return histogram_tester_; }

  void StopThrottling() { lite_video_hint_agent_->StopThrottling(); }

 protected:
  void SetUp() override {
    ChromeRenderViewTest::SetUp();
    scoped_feature_list_.InitAndEnableFeature(features::kLiteVideo);
    blink::WebNetworkStateNotifier::SetSaveDataEnabled(true);
    lite_video_hint_agent_ =
        new LiteVideoHintAgent(view_->GetMainRenderFrame());

    // Set some default hints.
    blink::mojom::LiteVideoHintPtr hint = blink::mojom::LiteVideoHint::New();
    hint->kilobytes_to_buffer_before_throttle = 10;
    hint->target_downlink_bandwidth_kbps = 60;
    hint->target_downlink_rtt_latency = base::TimeDelta::FromMilliseconds(500);
    hint->max_throttling_delay = base::TimeDelta::FromSeconds(5);
    lite_video_hint_agent_->SetLiteVideoHint(std::move(hint));
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;

  base::HistogramTester histogram_tester_;

  // Owned by the RenderFrame.
  LiteVideoHintAgent* lite_video_hint_agent_;
};

TEST_F(LiteVideoHintAgentTest, LiteVideoDisabled) {
  DisableLiteVideoFeature();
  EXPECT_FALSE(CreateLiteVideoURLLoaderThrottle(
      blink::mojom::RequestContextType::FETCH));
}

TEST_F(LiteVideoHintAgentTest, SaveDataDisabled) {
  blink::WebNetworkStateNotifier::SetSaveDataEnabled(false);
  EXPECT_FALSE(CreateLiteVideoURLLoaderThrottle(
      blink::mojom::RequestContextType::FETCH));
}

TEST_F(LiteVideoHintAgentTest, OnlyFetchAPIResponseThrottled) {
  EXPECT_FALSE(CreateLiteVideoURLLoaderThrottle(
      blink::mojom::RequestContextType::IMAGE));
}

TEST_F(LiteVideoHintAgentTest, OnlyMediaMimeTypeThrottled) {
  histogram_tester().ExpectUniqueSample("LiteVideo.HintAgent.HasHint", true, 1);

  auto throttle_info =
      CreateThrottleAndSendResponse(net::HTTP_OK, "image/jpeg", 11000);
  EXPECT_FALSE(throttle_info->is_throttled());

  throttle_info =
      CreateThrottleAndSendResponse(net::HTTP_OK, "image/jpeg", 11000);
  EXPECT_FALSE(throttle_info->is_throttled());
  histogram_tester().ExpectTotalCount("LiteVideo.URLLoader.ThrottleLatency", 0);
}

TEST_F(LiteVideoHintAgentTest, FailedMediaResponseNotThrottled) {
  histogram_tester().ExpectUniqueSample("LiteVideo.HintAgent.HasHint", true, 1);

  auto throttle_info = CreateThrottleAndSendResponse(
      net::HTTP_INTERNAL_SERVER_ERROR, "video/mp4", 11000);
  EXPECT_FALSE(throttle_info->is_throttled());

  throttle_info = CreateThrottleAndSendResponse(net::HTTP_INTERNAL_SERVER_ERROR,
                                                "video/mp4", 11000);
  EXPECT_FALSE(throttle_info->is_throttled());
  histogram_tester().ExpectTotalCount("LiteVideo.URLLoader.ThrottleLatency", 0);
}

TEST_F(LiteVideoHintAgentTest, MediaResponseThrottled) {
  histogram_tester().ExpectUniqueSample("LiteVideo.HintAgent.HasHint", true, 1);

  // Initial k media bytes will not be throttled.
  auto throttle_info =
      CreateThrottleAndSendResponse(net::HTTP_OK, "video/mp4", 11000);
  EXPECT_FALSE(throttle_info->is_throttled());
  histogram_tester().ExpectTotalCount("LiteVideo.URLLoader.ThrottleLatency", 0);
  EXPECT_TRUE(GetActiveThrottledResponses().empty());

  // Verify if a response gets throttled and eventually resumed.
  throttle_info =
      CreateThrottleAndSendResponse(net::HTTP_OK, "video/mp4", 440000);
  histogram_tester().ExpectTotalCount("LiteVideo.URLLoader.ThrottleLatency", 1);
  EXPECT_TRUE(throttle_info->is_throttled());
  // This is to wait until the throttle resumes, cannot fast-forward in
  // RenderViewTest.
  while (throttle_info->is_throttled())
    base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(throttle_info->is_throttled());

  // Verify a response that wasn't yet resumed, gets cleared from hint agent
  // when its destroyed.
  throttle_info =
      CreateThrottleAndSendResponse(net::HTTP_OK, "video/mp4", 440000);
  EXPECT_TRUE(throttle_info->is_throttled());
  histogram_tester().ExpectTotalCount("LiteVideo.URLLoader.ThrottleLatency", 2);

  EXPECT_FALSE(GetActiveThrottledResponses().empty());
  throttle_info.reset();
  EXPECT_TRUE(GetActiveThrottledResponses().empty());
}

TEST_F(LiteVideoHintAgentTest, StopThrottlingResumesResponsesImmediately) {
  histogram_tester().ExpectUniqueSample("LiteVideo.HintAgent.HasHint", true, 1);

  // Initial response is not throttled, and the next two are throttled.
  auto throttle_info1 =
      CreateThrottleAndSendResponse(net::HTTP_OK, "video/mp4", 11000);
  auto throttle_info2 =
      CreateThrottleAndSendResponse(net::HTTP_OK, "video/mp4", 11000);
  auto throttle_info3 =
      CreateThrottleAndSendResponse(net::HTTP_OK, "video/mp4", 11000);
  EXPECT_FALSE(throttle_info1->is_throttled());
  EXPECT_TRUE(throttle_info2->is_throttled());
  EXPECT_TRUE(throttle_info3->is_throttled());
  histogram_tester().ExpectTotalCount("LiteVideo.URLLoader.ThrottleLatency", 2);
  EXPECT_EQ(2U, GetActiveThrottledResponses().size());

  // Stop throttling will immediately resume.
  StopThrottling();
  EXPECT_FALSE(throttle_info2->is_throttled());
  EXPECT_FALSE(throttle_info3->is_throttled());

  // When the throttle destroys it should get removed from active throttles.
  throttle_info2.reset();
  throttle_info3.reset();
  EXPECT_TRUE(GetActiveThrottledResponses().empty());
}

}  // namespace lite_video