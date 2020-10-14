// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/performance_manager/public/v8_memory/v8_per_frame_memory_decorator.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/test/bind_test_util.h"
#include "base/test/gtest_util.h"
#include "base/time/time.h"
#include "components/performance_manager/graph/frame_node_impl.h"
#include "components/performance_manager/graph/page_node_impl.h"
#include "components/performance_manager/graph/process_node_impl.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/performance_manager/public/render_process_host_id.h"
#include "components/performance_manager/public/render_process_host_proxy.h"
#include "components/performance_manager/test_support/graph_test_harness.h"
#include "components/performance_manager/test_support/performance_manager_test_harness.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/navigation_simulator.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/tokens/tokens.h"
#include "url/gurl.h"

namespace performance_manager {

namespace v8_memory {

using testing::_;
using testing::Eq;
using testing::Property;

constexpr RenderProcessHostId kTestProcessID = RenderProcessHostId(0xFAB);
constexpr uint64_t kUnassociatedBytes = 0xABBA;

namespace {

class LenientMockV8PerFrameMemoryReporter
    : public blink::mojom::V8PerFrameMemoryReporter {
 public:
  LenientMockV8PerFrameMemoryReporter() : receiver_(this) {}

  MOCK_METHOD(void,
              GetPerFrameV8MemoryUsageData,
              (Mode mode, GetPerFrameV8MemoryUsageDataCallback callback),
              (override));

  void Bind(mojo::PendingReceiver<blink::mojom::V8PerFrameMemoryReporter>
                pending_receiver) {
    return receiver_.Bind(std::move(pending_receiver));
  }

 private:
  mojo::Receiver<blink::mojom::V8PerFrameMemoryReporter> receiver_;
};

using MockV8PerFrameMemoryReporter =
    testing::StrictMock<LenientMockV8PerFrameMemoryReporter>;

class LenientMockV8PerFrameMemoryObserver : public V8PerFrameMemoryObserver {
 public:
  MOCK_METHOD(void,
              OnV8MemoryMeasurementAvailable,
              (const ProcessNode* process_node,
               const V8PerFrameMemoryProcessData* process_data),
              (override));

  void ExpectObservationOnProcess(
      const ProcessNode* process_node,
      uint64_t expected_unassociated_v8_bytes_used) {
    EXPECT_CALL(
        *this,
        OnV8MemoryMeasurementAvailable(
            process_node,
            Property(&V8PerFrameMemoryProcessData::unassociated_v8_bytes_used,
                     Eq(expected_unassociated_v8_bytes_used))));
  }
};

using MockV8PerFrameMemoryObserver =
    testing::StrictMock<LenientMockV8PerFrameMemoryObserver>;

class LenientMockV8PerFrameMemoryObserverAnySeq
    : public V8PerFrameMemoryObserverAnySeq {
 public:
  MOCK_METHOD(void,
              OnV8MemoryMeasurementAvailable,
              (RenderProcessHostId render_process_host_id,
               const V8PerFrameMemoryProcessData& process_data,
               const V8PerFrameMemoryObserverAnySeq::FrameDataMap& frame_data),
              (override));
};
using MockV8PerFrameMemoryObserverAnySeq =
    testing::StrictMock<LenientMockV8PerFrameMemoryObserverAnySeq>;

class V8PerFrameMemoryDecoratorTestBase {
 public:
  static constexpr base::TimeDelta kMinTimeBetweenRequests =
      base::TimeDelta::FromSeconds(30);

  V8PerFrameMemoryDecoratorTestBase() {
    internal::SetBindV8PerFrameMemoryReporterCallbackForTesting(
        &bind_callback_);
  }

  virtual ~V8PerFrameMemoryDecoratorTestBase() {
    internal::SetBindV8PerFrameMemoryReporterCallbackForTesting(nullptr);
  }

  // Adaptor that calls GetMainThreadTaskRunner for the test harness's task
  // environment.
  virtual scoped_refptr<base::SingleThreadTaskRunner>
  GetMainThreadTaskRunner() = 0;

  void ReplyWithData(
      blink::mojom::PerProcessV8MemoryUsageDataPtr data,
      MockV8PerFrameMemoryReporter::GetPerFrameV8MemoryUsageDataCallback
          callback) {
    std::move(callback).Run(std::move(data));
  }

  void DelayedReplyWithData(
      const base::TimeDelta& delay,
      blink::mojom::PerProcessV8MemoryUsageDataPtr data,
      MockV8PerFrameMemoryReporter::GetPerFrameV8MemoryUsageDataCallback
          callback) {
    GetMainThreadTaskRunner()->PostDelayedTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::move(data)), delay);
  }

  void ExpectQuery(
      MockV8PerFrameMemoryReporter* mock_reporter,
      base::RepeatingCallback<void(
          MockV8PerFrameMemoryReporter::GetPerFrameV8MemoryUsageDataCallback
              callback)> responder,
      MockV8PerFrameMemoryReporter::Mode expected_mode =
          MockV8PerFrameMemoryReporter::Mode::DEFAULT) {
    EXPECT_CALL(*mock_reporter, GetPerFrameV8MemoryUsageData(expected_mode, _))
        .WillOnce([this, responder](
                      MockV8PerFrameMemoryReporter::Mode mode,
                      MockV8PerFrameMemoryReporter::
                          GetPerFrameV8MemoryUsageDataCallback callback) {
          this->last_query_time_ = base::TimeTicks::Now();
          responder.Run(std::move(callback));
        });
  }

  void ExpectQueryAndReply(MockV8PerFrameMemoryReporter* mock_reporter,
                           blink::mojom::PerProcessV8MemoryUsageDataPtr data,
                           MockV8PerFrameMemoryReporter::Mode expected_mode =
                               MockV8PerFrameMemoryReporter::Mode::DEFAULT) {
    ExpectQuery(
        mock_reporter,
        base::BindRepeating(&V8PerFrameMemoryDecoratorTestBase::ReplyWithData,
                            base::Unretained(this), base::Passed(&data)),
        expected_mode);
  }

  void ExpectQueryAndDelayReply(
      MockV8PerFrameMemoryReporter* mock_reporter,
      const base::TimeDelta& delay,
      blink::mojom::PerProcessV8MemoryUsageDataPtr data,
      MockV8PerFrameMemoryReporter::Mode expected_mode =
          MockV8PerFrameMemoryReporter::Mode::DEFAULT) {
    ExpectQuery(mock_reporter,
                base::BindRepeating(
                    &V8PerFrameMemoryDecoratorTestBase::DelayedReplyWithData,
                    base::Unretained(this), delay, base::Passed(&data)),
                expected_mode);
  }

  void ExpectBindAndRespondToQuery(
      MockV8PerFrameMemoryReporter* mock_reporter,
      blink::mojom::PerProcessV8MemoryUsageDataPtr data,
      RenderProcessHostId expected_process_id = kTestProcessID,
      MockV8PerFrameMemoryReporter::Mode expected_mode =
          MockV8PerFrameMemoryReporter::Mode::DEFAULT) {
    // Wrap the move-only |data| in a callback for the expectation below.
    ExpectQueryAndReply(mock_reporter, std::move(data), expected_mode);

    EXPECT_CALL(*this, BindReceiverWithProxyHost(_, _))
        .WillOnce(
            [mock_reporter, expected_process_id](
                mojo::PendingReceiver<blink::mojom::V8PerFrameMemoryReporter>
                    pending_receiver,
                RenderProcessHostProxy proxy) {
              DCHECK_EQ(expected_process_id, proxy.render_process_host_id());
              mock_reporter->Bind(std::move(pending_receiver));
            });
  }

  MOCK_METHOD(void,
              BindReceiverWithProxyHost,
              (mojo::PendingReceiver<blink::mojom::V8PerFrameMemoryReporter>
                   pending_receiver,
               RenderProcessHostProxy proxy),
              (const));

  // Always bind the receiver callback on the main sequence.
  internal::BindV8PerFrameMemoryReporterCallback bind_callback_ =
      base::BindLambdaForTesting(
          [this](mojo::PendingReceiver<blink::mojom::V8PerFrameMemoryReporter>
                     pending_receiver,
                 RenderProcessHostProxy proxy) {
            this->GetMainThreadTaskRunner()->PostTask(
                FROM_HERE, base::BindOnce(&V8PerFrameMemoryDecoratorTestBase::
                                              BindReceiverWithProxyHost,
                                          base::Unretained(this),
                                          std::move(pending_receiver), proxy));
          });

  base::TimeTicks last_query_time_;
};

void AddPerFrameIsolateMemoryUsage(
    const blink::LocalFrameToken& frame_token,
    int64_t world_id,
    uint64_t bytes_used,
    blink::mojom::PerProcessV8MemoryUsageData* data) {
  blink::mojom::PerFrameV8MemoryUsageData* per_frame_data = nullptr;
  for (auto& datum : data->associated_memory) {
    if (datum->frame_token == frame_token.value()) {
      per_frame_data = datum.get();
      break;
    }
  }

  if (!per_frame_data) {
    blink::mojom::PerFrameV8MemoryUsageDataPtr datum =
        blink::mojom::PerFrameV8MemoryUsageData::New();
    datum->frame_token = frame_token.value();
    per_frame_data = datum.get();
    data->associated_memory.push_back(std::move(datum));
  }
  for (const auto& entry : per_frame_data->associated_bytes) {
    EXPECT_NE(world_id, entry->world_id);
  }

  auto isolated_world_usage = blink::mojom::V8IsolatedWorldMemoryUsage::New();
  isolated_world_usage->bytes_used = bytes_used;
  isolated_world_usage->world_id = world_id;
  per_frame_data->associated_bytes.push_back(std::move(isolated_world_usage));
}

}  // namespace

class V8PerFrameMemoryDecoratorTest : public GraphTestHarness,
                                      public V8PerFrameMemoryDecoratorTestBase {
 public:
  scoped_refptr<base::SingleThreadTaskRunner> GetMainThreadTaskRunner()
      override {
    return task_env().GetMainThreadTaskRunner();
  }
};

using V8PerFrameMemoryDecoratorDeathTest = V8PerFrameMemoryDecoratorTest;

class V8PerFrameMemoryRequestAnySeqTest
    : public PerformanceManagerTestHarness,
      public V8PerFrameMemoryDecoratorTestBase {
 public:
  scoped_refptr<base::SingleThreadTaskRunner> GetMainThreadTaskRunner()
      override {
    return task_environment()->GetMainThreadTaskRunner();
  }
};

constexpr base::TimeDelta
    V8PerFrameMemoryDecoratorTestBase::kMinTimeBetweenRequests;

TEST_F(V8PerFrameMemoryDecoratorTest, InstantiateOnEmptyGraph) {
  V8PerFrameMemoryRequest memory_request(kMinTimeBetweenRequests, graph());

  MockV8PerFrameMemoryReporter mock_reporter;
  auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
  data->unassociated_bytes_used = kUnassociatedBytes;
  ExpectBindAndRespondToQuery(&mock_reporter, std::move(data));

  // Create a process node and validate that it gets a request.
  auto process = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));

  // Data should not be available until the measurement is taken.
  EXPECT_FALSE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));

  // Run until idle to make sure the measurement isn't a hard loop.
  task_env().RunUntilIdle();

  EXPECT_TRUE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));
  EXPECT_EQ(kUnassociatedBytes,
            V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                ->unassociated_v8_bytes_used());
}

TEST_F(V8PerFrameMemoryDecoratorTest, InstantiateOnNonEmptyGraph) {
  // Instantiate the decorator with an existing process node and validate that
  // it gets a request.
  auto process = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));

  MockV8PerFrameMemoryReporter mock_reporter;
  auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
  data->unassociated_bytes_used = kUnassociatedBytes;
  ExpectBindAndRespondToQuery(&mock_reporter, std::move(data));

  V8PerFrameMemoryRequest memory_request(kMinTimeBetweenRequests, graph());

  // Data should not be available until the measurement is taken.
  EXPECT_FALSE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));

  // Run until idle to make sure the measurement isn't a hard loop.
  task_env().RunUntilIdle();

  EXPECT_TRUE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));
  EXPECT_EQ(kUnassociatedBytes,
            V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                ->unassociated_v8_bytes_used());
}

TEST_F(V8PerFrameMemoryDecoratorTest, OnlyMeasureRenderers) {
  V8PerFrameMemoryRequest memory_request(kMinTimeBetweenRequests, graph());

  for (int type = content::PROCESS_TYPE_BROWSER;
       type < content::PROCESS_TYPE_CONTENT_END; ++type) {
    if (type == content::PROCESS_TYPE_RENDERER)
      continue;

    // Instantiate a non-renderer process node and validate that it causes no
    // bind requests.
    EXPECT_CALL(*this, BindReceiverWithProxyHost(_, _)).Times(0);
    auto process = CreateNode<ProcessNodeImpl>(
        static_cast<content::ProcessType>(type),
        RenderProcessHostProxy::CreateForTesting(kTestProcessID));

    task_env().RunUntilIdle();
    testing::Mock::VerifyAndClearExpectations(this);
  }
}

TEST_F(V8PerFrameMemoryDecoratorTest, QueryRateIsLimited) {
  auto process = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));

  MockV8PerFrameMemoryReporter mock_reporter;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    // Response to request 1.
    data->unassociated_bytes_used = 1;
    ExpectBindAndRespondToQuery(&mock_reporter, std::move(data));
  }

  V8PerFrameMemoryRequest memory_request(kMinTimeBetweenRequests, graph());

  // Run until idle to make sure the measurement isn't a hard loop.
  task_env().RunUntilIdle();

  EXPECT_TRUE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));
  EXPECT_EQ(1u, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used());

  // There shouldn't be an additional request this soon.
  task_env().FastForwardBy(kMinTimeBetweenRequests / 2);
  testing::Mock::VerifyAndClearExpectations(&mock_reporter);

  // Set up another request and capture the callback for later invocation.
  MockV8PerFrameMemoryReporter::GetPerFrameV8MemoryUsageDataCallback callback;
  ExpectQuery(
      &mock_reporter,
      base::BindLambdaForTesting(
          [&callback](
              MockV8PerFrameMemoryReporter::GetPerFrameV8MemoryUsageDataCallback
                  result_callback) { callback = std::move(result_callback); }));

  // Skip forward to when another request should be issued.
  task_env().FastForwardBy(kMinTimeBetweenRequests);
  ASSERT_FALSE(callback.is_null());

  // Skip forward a long while, and validate that no additional requests are
  // issued until the pending request has completed.
  task_env().FastForwardBy(10 * kMinTimeBetweenRequests);
  testing::Mock::VerifyAndClearExpectations(&mock_reporter);

  EXPECT_TRUE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));
  EXPECT_EQ(1u, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used());

  // Expect another query once completing the query above.
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    // Response to request 3.
    data->unassociated_bytes_used = 3;
    ExpectQueryAndReply(&mock_reporter, std::move(data));
  }

  // Reply to the request above.
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    // Response to request 2.
    data->unassociated_bytes_used = 2;
    std::move(callback).Run(std::move(data));
  }

  task_env().RunUntilIdle();

  // This should have updated all the way to the third response.
  EXPECT_TRUE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));
  EXPECT_EQ(3u, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used());

  // Despite the long delay to respond to request 2, there shouldn't be another
  // request until kMinTimeBetweenRequests has expired.
  task_env().FastForwardBy(kMinTimeBetweenRequests / 2);
  testing::Mock::VerifyAndClearExpectations(&mock_reporter);
}

TEST_F(V8PerFrameMemoryDecoratorTest, MultipleProcessesHaveDistinctSchedules) {
  V8PerFrameMemoryRequest memory_request(kMinTimeBetweenRequests, graph());

  // Create a process node and validate that it gets a request.
  MockV8PerFrameMemoryReporter reporter1;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 1;
    ExpectBindAndRespondToQuery(&reporter1, std::move(data));
  }

  auto process1 = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));

  task_env().FastForwardBy(kMinTimeBetweenRequests / 4);
  testing::Mock::VerifyAndClearExpectations(&reporter1);

  // Create a second process node and validate that it gets a request.
  MockV8PerFrameMemoryReporter reporter2;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 2;
    ExpectBindAndRespondToQuery(&reporter2, std::move(data));
  }

  auto process2 = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));

  task_env().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&reporter2);

  EXPECT_TRUE(V8PerFrameMemoryProcessData::ForProcessNode(process1.get()));
  EXPECT_EQ(1u, V8PerFrameMemoryProcessData::ForProcessNode(process1.get())
                    ->unassociated_v8_bytes_used());
  EXPECT_TRUE(V8PerFrameMemoryProcessData::ForProcessNode(process2.get()));
  EXPECT_EQ(2u, V8PerFrameMemoryProcessData::ForProcessNode(process2.get())
                    ->unassociated_v8_bytes_used());

  // Capture the request time from each process.
  auto capture_time_lambda =
      [](base::TimeTicks* request_time,
         MockV8PerFrameMemoryReporter::GetPerFrameV8MemoryUsageDataCallback
             callback) {
        *request_time = base::TimeTicks::Now();
        std::move(callback).Run(
            blink::mojom::PerProcessV8MemoryUsageData::New());
      };

  base::TimeTicks process1_request_time;
  ExpectQuery(&reporter1,
              base::BindRepeating(capture_time_lambda,
                                  base::Unretained(&process1_request_time)));
  base::TimeTicks process2_request_time;
  ExpectQuery(&reporter2,
              base::BindRepeating(capture_time_lambda,
                                  base::Unretained(&process2_request_time)));

  task_env().FastForwardBy(kMinTimeBetweenRequests * 1.25);

  // Check that both processes got polled, and that process2 was polled after
  // process1.
  EXPECT_FALSE(process1_request_time.is_null());
  EXPECT_FALSE(process2_request_time.is_null());
  EXPECT_GT(process2_request_time, process1_request_time);
}

TEST_F(V8PerFrameMemoryDecoratorTest, PerFrameDataIsDistributed) {
  V8PerFrameMemoryRequest memory_request(kMinTimeBetweenRequests, graph());

  MockV8PerFrameMemoryReporter reporter;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    // Add data for an unknown frame.
    AddPerFrameIsolateMemoryUsage(blink::LocalFrameToken(), 0, 1024u,
                                  data.get());

    ExpectBindAndRespondToQuery(&reporter, std::move(data));
  }

  auto process = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));

  task_env().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&reporter);

  // Since the frame was unknown, the usage should have accrued to unassociated.
  EXPECT_TRUE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));
  EXPECT_EQ(1024u, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                       ->unassociated_v8_bytes_used());

  // Create a couple of frames with specified IDs.
  auto page = CreateNode<PageNodeImpl>();

  blink::LocalFrameToken frame1_id = blink::LocalFrameToken();
  auto frame1 = CreateNode<FrameNodeImpl>(process.get(), page.get(), nullptr, 1,
                                          2, frame1_id);

  blink::LocalFrameToken frame2_id = blink::LocalFrameToken();
  auto frame2 = CreateNode<FrameNodeImpl>(process.get(), page.get(), nullptr, 3,
                                          4, frame2_id);
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    AddPerFrameIsolateMemoryUsage(frame1_id, 0, 1001u, data.get());
    AddPerFrameIsolateMemoryUsage(frame2_id, 0, 1002u, data.get());
    ExpectQueryAndReply(&reporter, std::move(data));
  }

  task_env().FastForwardBy(kMinTimeBetweenRequests * 1.5);
  testing::Mock::VerifyAndClearExpectations(&reporter);

  ASSERT_TRUE(V8PerFrameMemoryFrameData::ForFrameNode(frame1.get()));
  EXPECT_EQ(
      1001u,
      V8PerFrameMemoryFrameData::ForFrameNode(frame1.get())->v8_bytes_used());
  ASSERT_TRUE(V8PerFrameMemoryFrameData::ForFrameNode(frame2.get()));
  EXPECT_EQ(
      1002u,
      V8PerFrameMemoryFrameData::ForFrameNode(frame2.get())->v8_bytes_used());

  // Now verify that data is cleared for any frame that doesn't get an update,
  // plus verify that unknown frame data toes to unassociated bytes.
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    AddPerFrameIsolateMemoryUsage(frame1_id, 0, 1003u, data.get());
    AddPerFrameIsolateMemoryUsage(blink::LocalFrameToken(), 0, 2233u,
                                  data.get());
    ExpectQueryAndReply(&reporter, std::move(data));
  }
  task_env().FastForwardBy(kMinTimeBetweenRequests);
  testing::Mock::VerifyAndClearExpectations(&reporter);

  ASSERT_TRUE(V8PerFrameMemoryFrameData::ForFrameNode(frame1.get()));
  EXPECT_EQ(
      1003u,
      V8PerFrameMemoryFrameData::ForFrameNode(frame1.get())->v8_bytes_used());
  EXPECT_FALSE(V8PerFrameMemoryFrameData::ForFrameNode(frame2.get()));
  ASSERT_TRUE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));
  EXPECT_EQ(2233u, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                       ->unassociated_v8_bytes_used());
}

TEST_F(V8PerFrameMemoryDecoratorTest, LazyRequests) {
  constexpr base::TimeDelta kLazyRequestLength =
      base::TimeDelta::FromSeconds(30);
  V8PerFrameMemoryRequest lazy_request(
      kLazyRequestLength, V8PerFrameMemoryRequest::MeasurementMode::kLazy,
      graph());

  MockV8PerFrameMemoryReporter reporter;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    ExpectBindAndRespondToQuery(&reporter, std::move(data), kTestProcessID,
                                MockV8PerFrameMemoryReporter::Mode::LAZY);
  }

  auto process = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));

  task_env().FastForwardBy(base::TimeDelta::FromSeconds(1));
  testing::Mock::VerifyAndClearExpectations(&reporter);

  // If a lazy request takes too long to respond it should be upgraded to a
  // bounded request if one is in the queue.
  constexpr base::TimeDelta kLongBoundedRequestLength =
      base::TimeDelta::FromSeconds(45);
  V8PerFrameMemoryRequest long_bounded_request(kLongBoundedRequestLength,
                                               graph());
  auto* decorator = V8PerFrameMemoryDecorator::GetFromGraph(graph());
  ASSERT_TRUE(decorator);
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(decorator->GetNextRequest()->min_time_between_requests(),
            kLazyRequestLength);
  EXPECT_EQ(decorator->GetNextRequest()->mode(),
            V8PerFrameMemoryRequest::MeasurementMode::kLazy);
  {
    // Next lazy request sent after 30 sec + 10 sec delay until reply = 40 sec
    // until reply arrives. kLongBoundedRequestLength > 40 sec so the reply
    // should arrive in time to prevent upgrading the request.
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 1U;
    ExpectQueryAndDelayReply(&reporter, base::TimeDelta::FromSeconds(10),
                             std::move(data),
                             MockV8PerFrameMemoryReporter::Mode::LAZY);
  }

  // Wait long enough for the upgraded request to be sent, to verify that it
  // wasn't sent.
  task_env().FastForwardBy(kLongBoundedRequestLength);
  testing::Mock::VerifyAndClearExpectations(&reporter);

  constexpr base::TimeDelta kUpgradeRequestLength =
      base::TimeDelta::FromSeconds(40);
  V8PerFrameMemoryRequest bounded_request_upgrade(kUpgradeRequestLength,
                                                  graph());
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(decorator->GetNextRequest()->min_time_between_requests(),
            kLazyRequestLength);
  EXPECT_EQ(decorator->GetNextRequest()->mode(),
            V8PerFrameMemoryRequest::MeasurementMode::kLazy);

  {
    ::testing::InSequence seq;

    // Again, 40 sec total until reply arrives. kUpgradeRequestLength <= 40 sec
    // so a second upgraded request should be sent.
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 2U;
    ExpectQueryAndDelayReply(&reporter, base::TimeDelta::FromSeconds(10),
                             std::move(data),
                             MockV8PerFrameMemoryReporter::Mode::LAZY);

    auto data2 = blink::mojom::PerProcessV8MemoryUsageData::New();
    data2->unassociated_bytes_used = 3U;
    ExpectQueryAndReply(&reporter, std::move(data2),
                        MockV8PerFrameMemoryReporter::Mode::DEFAULT);
  }

  // Wait long enough for the upgraded request to be sent.
  task_env().FastForwardBy(kUpgradeRequestLength);
  testing::Mock::VerifyAndClearExpectations(&reporter);

  EXPECT_TRUE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));
  EXPECT_EQ(3u, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used());

  // Bounded requests should be preferred over lazy requests with the same
  // min_time_between_requests.
  V8PerFrameMemoryRequest short_bounded_request(kLazyRequestLength, graph());
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(decorator->GetNextRequest()->min_time_between_requests(),
            kLazyRequestLength);
  EXPECT_EQ(decorator->GetNextRequest()->mode(),
            V8PerFrameMemoryRequest::MeasurementMode::kBounded);
}

TEST_F(V8PerFrameMemoryDecoratorTest, MeasurementRequestsSorted) {
  // Create some queries with different sample frequencies.
  constexpr base::TimeDelta kShortInterval(kMinTimeBetweenRequests);
  constexpr base::TimeDelta kMediumInterval(2 * kMinTimeBetweenRequests);
  constexpr base::TimeDelta kLongInterval(3 * kMinTimeBetweenRequests);

  // Create longer requests first to be sure they sort correctly.
  auto medium_memory_request =
      std::make_unique<V8PerFrameMemoryRequest>(kMediumInterval, graph());

  auto short_memory_request =
      std::make_unique<V8PerFrameMemoryRequest>(kShortInterval, graph());

  auto long_memory_request =
      std::make_unique<V8PerFrameMemoryRequest>(kLongInterval, graph());

  auto* decorator = V8PerFrameMemoryDecorator::GetFromGraph(graph());
  ASSERT_TRUE(decorator);

  // A single measurement should be taken immediately regardless of the overall
  // frequency.
  MockV8PerFrameMemoryReporter mock_reporter;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 1U;
    ExpectBindAndRespondToQuery(&mock_reporter, std::move(data));
  }

  auto process = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));
  EXPECT_FALSE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));

  task_env().FastForwardBy(base::TimeDelta::FromSeconds(1));
  // All the following FastForwardBy calls will place the clock 1 sec after a
  // measurement is expected.

  ASSERT_TRUE(V8PerFrameMemoryProcessData::ForProcessNode(process.get()));
  EXPECT_EQ(1U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used());

  // Another measurement should be taken after the shortest interval.
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kShortInterval,
            decorator->GetNextRequest()->min_time_between_requests());
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 2U;
    ExpectQueryAndReply(&mock_reporter, std::move(data));

    task_env().FastForwardBy(kShortInterval);
    EXPECT_EQ(2U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                      ->unassociated_v8_bytes_used());
  }

  // Remove the shortest request. Now a measurement should be taken after the
  // medium interval, which is twice the short interval.
  short_memory_request.reset();
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kMediumInterval,
            decorator->GetNextRequest()->min_time_between_requests());
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 3U;
    ExpectQueryAndReply(&mock_reporter, std::move(data));

    task_env().FastForwardBy(kShortInterval);
    EXPECT_EQ(2U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                      ->unassociated_v8_bytes_used());
    task_env().FastForwardBy(kShortInterval);
    EXPECT_EQ(3U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                      ->unassociated_v8_bytes_used());
  }

  // Remove the longest request. A measurement should still be taken after the
  // medium interval.
  long_memory_request.reset();
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kMediumInterval,
            decorator->GetNextRequest()->min_time_between_requests());
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 4U;
    ExpectQueryAndReply(&mock_reporter, std::move(data));

    task_env().FastForwardBy(kMediumInterval);
    EXPECT_EQ(4U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                      ->unassociated_v8_bytes_used());
  }

  // Remove the medium request, making the queue empty.
  medium_memory_request.reset();
  EXPECT_FALSE(decorator->GetNextRequest());
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 5U;
    ExpectQueryAndReply(&mock_reporter, std::move(data));

    task_env().FastForwardBy(kLongInterval);
    EXPECT_EQ(4U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                      ->unassociated_v8_bytes_used());
  }

  // Create another request. Since this is the first request in an empty queue
  // the measurement should be taken immediately.
  long_memory_request =
      std::make_unique<V8PerFrameMemoryRequest>(kLongInterval, graph());
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kLongInterval,
            decorator->GetNextRequest()->min_time_between_requests());

  task_env().FastForwardBy(base::TimeDelta::FromSeconds(1));
  EXPECT_EQ(5U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used());

  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 6U;
    ExpectQueryAndReply(&mock_reporter, std::move(data));

    task_env().FastForwardBy(kLongInterval);
    EXPECT_EQ(6U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                      ->unassociated_v8_bytes_used());
  }

  // Now there should be kLongInterval - 1 sec until the next measurement.
  // Make sure a shorter request replaces this (the new interval should cause a
  // measurement and the old interval should not).
  medium_memory_request =
      std::make_unique<V8PerFrameMemoryRequest>(kMediumInterval, graph());
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kMediumInterval,
            decorator->GetNextRequest()->min_time_between_requests());

  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 7U;
    ExpectQueryAndReply(&mock_reporter, std::move(data));

    task_env().FastForwardBy(kMediumInterval);
    EXPECT_EQ(7U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                      ->unassociated_v8_bytes_used());
  }

  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 8U;
    ExpectQueryAndReply(&mock_reporter, std::move(data));

    constexpr base::TimeDelta kRestOfLongInterval =
        kLongInterval - kMediumInterval;
    task_env().FastForwardBy(kRestOfLongInterval);
    EXPECT_EQ(7U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                      ->unassociated_v8_bytes_used());

    task_env().FastForwardBy(kMediumInterval - kRestOfLongInterval);
    EXPECT_EQ(8U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                      ->unassociated_v8_bytes_used());
  }

  // Remove the medium request and add it back. The measurement interval should
  // not change.
  medium_memory_request.reset();
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kLongInterval,
            decorator->GetNextRequest()->min_time_between_requests());
  medium_memory_request =
      std::make_unique<V8PerFrameMemoryRequest>(kMediumInterval, graph());
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kMediumInterval,
            decorator->GetNextRequest()->min_time_between_requests());

  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 9U;
    ExpectQueryAndReply(&mock_reporter, std::move(data));

    task_env().FastForwardBy(kMediumInterval);
    EXPECT_EQ(9U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                      ->unassociated_v8_bytes_used());
  }

  // Add another long request. There should still be requests after the medium
  // interval.
  auto long_memory_request2 =
      std::make_unique<V8PerFrameMemoryRequest>(kLongInterval, graph());
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kMediumInterval,
            decorator->GetNextRequest()->min_time_between_requests());

  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 10U;
    ExpectQueryAndReply(&mock_reporter, std::move(data));

    task_env().FastForwardBy(kMediumInterval);
    EXPECT_EQ(10U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                       ->unassociated_v8_bytes_used());
  }

  // Remove the medium request. Now there are 2 requests which should cause
  // measurements at the same interval. Make sure only 1 measurement is taken.
  medium_memory_request.reset();
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kLongInterval,
            decorator->GetNextRequest()->min_time_between_requests());

  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 11U;
    ExpectQueryAndReply(&mock_reporter, std::move(data));

    task_env().FastForwardBy(kLongInterval);
    EXPECT_EQ(11U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                       ->unassociated_v8_bytes_used());
  }

  // Remove 1 of the 2 long requests. Measurements should not change.
  long_memory_request2.reset();
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kLongInterval,
            decorator->GetNextRequest()->min_time_between_requests());

  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 12U;
    ExpectQueryAndReply(&mock_reporter, std::move(data));

    task_env().FastForwardBy(kLongInterval);
    EXPECT_EQ(12U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                       ->unassociated_v8_bytes_used());
  }
}

TEST_F(V8PerFrameMemoryDecoratorTest, MeasurementRequestsWithDelay) {
  // Create some queries with different sample frequencies.
  constexpr base::TimeDelta kShortInterval(kMinTimeBetweenRequests);
  constexpr base::TimeDelta kMediumInterval(2 * kMinTimeBetweenRequests);
  constexpr base::TimeDelta kLongInterval(3 * kMinTimeBetweenRequests);

  // Make measurements take long enough that a second request could be sent.
  constexpr base::TimeDelta kMeasurementLength(1.5 * kShortInterval);
  constexpr base::TimeDelta kOneSecond = base::TimeDelta::FromSeconds(1);

  auto long_memory_request =
      std::make_unique<V8PerFrameMemoryRequest>(kLongInterval, graph());

  auto* decorator = V8PerFrameMemoryDecorator::GetFromGraph(graph());
  ASSERT_TRUE(decorator);

  // Move past the first request since it's complicated to untangle the Bind
  // and QueryAndDelayReply expectations.
  MockV8PerFrameMemoryReporter mock_reporter;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 0U;
    ExpectBindAndRespondToQuery(&mock_reporter, std::move(data));
  }
  auto process = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));
  task_env().FastForwardBy(kOneSecond);
  // All the following FastForwardBy calls will place the clock 1 sec after a
  // measurement is expected.

  // Advance to the middle of a measurement and create a new request. Should
  // update min_time_between_requests but not start a new measurement until the
  // existing measurement finishes.
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 1U;
    ExpectQueryAndDelayReply(&mock_reporter, kMeasurementLength,
                             std::move(data));
  }
  task_env().FastForwardBy(kLongInterval);
  EXPECT_EQ(last_query_time_, task_env().NowTicks() - kOneSecond)
      << "Measurement didn't start when expected";
  EXPECT_EQ(0U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used())
      << "Measurement ended early";
  base::TimeTicks measurement_start_time = last_query_time_;

  auto medium_memory_request =
      std::make_unique<V8PerFrameMemoryRequest>(kMediumInterval, graph());
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kMediumInterval,
            decorator->GetNextRequest()->min_time_between_requests());
  task_env().FastForwardBy(kMeasurementLength);
  ASSERT_EQ(1U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used())
      << "Measurement didn't end when expected";
  EXPECT_EQ(last_query_time_, measurement_start_time);

  // Next measurement should start kMediumInterval secs after the START of the
  // last measurement.
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 2U;
    ExpectQueryAndDelayReply(&mock_reporter, kMeasurementLength,
                             std::move(data));
  }
  task_env().FastForwardBy(kMediumInterval - kMeasurementLength);
  EXPECT_EQ(last_query_time_, task_env().NowTicks() - kOneSecond)
      << "Measurement didn't start when expected";
  EXPECT_EQ(1U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used())
      << "Measurement ended early";
  measurement_start_time = last_query_time_;

  task_env().FastForwardBy(kMeasurementLength);
  EXPECT_EQ(2U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used())
      << "Measurement didn't end when expected";
  EXPECT_EQ(last_query_time_, measurement_start_time);

  // Create a request that would be sent in the middle of a measurement. It
  // should start immediately after the measurement finishes.
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 3U;
    ExpectQueryAndDelayReply(&mock_reporter, kMeasurementLength,
                             std::move(data));
  }
  task_env().FastForwardBy(kMediumInterval - kMeasurementLength);
  EXPECT_EQ(last_query_time_, task_env().NowTicks() - kOneSecond)
      << "Measurement didn't start when expected";
  EXPECT_EQ(2U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used())
      << "Measurement ended early";
  measurement_start_time = last_query_time_;

  auto short_memory_request =
      std::make_unique<V8PerFrameMemoryRequest>(kShortInterval, graph());
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kShortInterval,
            decorator->GetNextRequest()->min_time_between_requests());
  EXPECT_EQ(last_query_time_, measurement_start_time);

  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 4U;
    ExpectQueryAndDelayReply(&mock_reporter, kMeasurementLength,
                             std::move(data));
  }
  task_env().FastForwardBy(kMeasurementLength);
  EXPECT_EQ(last_query_time_, task_env().NowTicks() - kOneSecond)
      << "Measurement didn't start when expected";
  EXPECT_EQ(3U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used())
      << "Measurement ended early";
  measurement_start_time = last_query_time_;

  // Delete the short request. Should update min_time_between_requests but not
  // start a new measurement until the existing measurement finishes.
  short_memory_request.reset();
  ASSERT_TRUE(decorator->GetNextRequest());
  EXPECT_EQ(kMediumInterval,
            decorator->GetNextRequest()->min_time_between_requests());
  task_env().FastForwardBy(kMeasurementLength);
  EXPECT_EQ(4U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used())
      << "Measurement didn't end when expected";
  EXPECT_EQ(last_query_time_, measurement_start_time);

  // Delete the last request while a measurement is in process. The
  // measurement should finish successfully but no more should be sent.
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 5U;
    ExpectQueryAndDelayReply(&mock_reporter, kMeasurementLength,
                             std::move(data));
  }
  task_env().FastForwardBy(kMediumInterval - kMeasurementLength);
  EXPECT_EQ(last_query_time_, task_env().NowTicks() - kOneSecond)
      << "Measurement didn't start when expected";
  EXPECT_EQ(4U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used())
      << "Measurement ended early";
  measurement_start_time = last_query_time_;

  medium_memory_request.reset();
  long_memory_request.reset();
  EXPECT_FALSE(decorator->GetNextRequest());
  task_env().FastForwardBy(kMeasurementLength);
  EXPECT_EQ(5U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used())
      << "Measurement didn't end when expected";
  EXPECT_EQ(last_query_time_, measurement_start_time);

  // No more requests should be sent.
  testing::Mock::VerifyAndClearExpectations(this);
  task_env().FastForwardBy(kLongInterval);
}

TEST_F(V8PerFrameMemoryDecoratorTest, MeasurementRequestOutlivesDecorator) {
  V8PerFrameMemoryRequest memory_request(kMinTimeBetweenRequests, graph());

  auto* decorator = V8PerFrameMemoryDecorator::GetFromGraph(graph());
  ASSERT_TRUE(decorator);

  MockV8PerFrameMemoryReporter mock_reporter;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 1U;
    ExpectBindAndRespondToQuery(&mock_reporter, std::move(data));
  }
  auto process = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));
  task_env().FastForwardBy(base::TimeDelta::FromSeconds(1));
  ASSERT_EQ(1U, V8PerFrameMemoryProcessData::ForProcessNode(process.get())
                    ->unassociated_v8_bytes_used())
      << "First measurement didn't happen when expected";

  graph()->TakeFromGraph(decorator);

  // No request should be sent, and the decorator destructor should not DCHECK.
  testing::Mock::VerifyAndClearExpectations(this);
  task_env().FastForwardBy(kMinTimeBetweenRequests);
}

TEST_F(V8PerFrameMemoryDecoratorTest, NotifyObservers) {
  V8PerFrameMemoryRequest memory_request(kMinTimeBetweenRequests, graph());

  MockV8PerFrameMemoryObserver observer1;
  MockV8PerFrameMemoryObserver observer2;
  memory_request.AddObserver(&observer1);
  memory_request.AddObserver(&observer2);

  // Create a process node and validate that all observers are notified when a
  // measurement is available for it.
  MockV8PerFrameMemoryReporter reporter1;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 1U;
    ExpectBindAndRespondToQuery(&reporter1, std::move(data));
  }

  auto process1 = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));

  observer1.ExpectObservationOnProcess(process1.get(), 1U);
  observer2.ExpectObservationOnProcess(process1.get(), 1U);

  task_env().FastForwardBy(kMinTimeBetweenRequests / 2);
  testing::Mock::VerifyAndClearExpectations(&reporter1);
  testing::Mock::VerifyAndClearExpectations(&observer1);
  testing::Mock::VerifyAndClearExpectations(&observer2);

  // Create a process node and validate that all observers are notified when
  // any measurement is available. After fast-forwarding the first measurement
  // for process2 and the second measurement for process1 will arrive.
  MockV8PerFrameMemoryReporter reporter2;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 2U;
    ExpectBindAndRespondToQuery(&reporter2, std::move(data));
  }
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 3U;
    ExpectQueryAndReply(&reporter1, std::move(data));
  }

  auto process2 = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));

  observer1.ExpectObservationOnProcess(process2.get(), 2U);
  observer2.ExpectObservationOnProcess(process2.get(), 2U);
  observer1.ExpectObservationOnProcess(process1.get(), 3U);
  observer2.ExpectObservationOnProcess(process1.get(), 3U);

  task_env().FastForwardBy(kMinTimeBetweenRequests / 2);
  testing::Mock::VerifyAndClearExpectations(&reporter1);
  testing::Mock::VerifyAndClearExpectations(&reporter2);
  testing::Mock::VerifyAndClearExpectations(&observer1);
  testing::Mock::VerifyAndClearExpectations(&observer2);

  // Remove an observer and make sure the other is still notified after the
  // next measurement.
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 4U;
    ExpectQueryAndReply(&reporter1, std::move(data));
  }
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 5U;
    ExpectQueryAndReply(&reporter2, std::move(data));
  }

  memory_request.RemoveObserver(&observer1);

  observer2.ExpectObservationOnProcess(process1.get(), 4U);
  observer2.ExpectObservationOnProcess(process2.get(), 5U);

  task_env().FastForwardBy(kMinTimeBetweenRequests);
  testing::Mock::VerifyAndClearExpectations(&reporter1);
  testing::Mock::VerifyAndClearExpectations(&reporter2);
  testing::Mock::VerifyAndClearExpectations(&observer1);
  testing::Mock::VerifyAndClearExpectations(&observer2);

  // Must remove the observer before destroying the request to avoid a DCHECK
  // from ObserverList.
  memory_request.RemoveObserver(&observer2);
}

TEST_F(V8PerFrameMemoryDecoratorTest, ObserverOutlivesDecorator) {
  V8PerFrameMemoryRequest memory_request(kMinTimeBetweenRequests, graph());

  MockV8PerFrameMemoryObserver observer;
  memory_request.AddObserver(&observer);

  // Create a process node and move past the initial request to it.
  MockV8PerFrameMemoryReporter reporter;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 1U;
    ExpectBindAndRespondToQuery(&reporter, std::move(data));
  }

  auto process = CreateNode<ProcessNodeImpl>(
      content::PROCESS_TYPE_RENDERER,
      RenderProcessHostProxy::CreateForTesting(kTestProcessID));
  observer.ExpectObservationOnProcess(process.get(), 1U);

  task_env().FastForwardBy(base::TimeDelta::FromSeconds(1));

  testing::Mock::VerifyAndClearExpectations(&reporter);
  testing::Mock::VerifyAndClearExpectations(&observer);

  // Start the next measurement.
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = 2U;
    ExpectQueryAndDelayReply(&reporter, kMinTimeBetweenRequests,
                             std::move(data));
  }
  task_env().FastForwardBy(kMinTimeBetweenRequests);

  // Destroy the decorator before the measurement completes. The observer
  // should not be notified.
  auto* decorator = V8PerFrameMemoryDecorator::GetFromGraph(graph());
  ASSERT_TRUE(decorator);
  graph()->TakeFromGraph(decorator);

  task_env().FastForwardBy(kMinTimeBetweenRequests);

  // Must remove the observer before destroying the request to avoid a DCHECK
  // from ObserverList.
  memory_request.RemoveObserver(&observer);
}

TEST_F(V8PerFrameMemoryDecoratorDeathTest, MultipleStartMeasurement) {
  EXPECT_DCHECK_DEATH({
    V8PerFrameMemoryRequest request(kMinTimeBetweenRequests);
    request.StartMeasurement(graph());
    request.StartMeasurement(graph());
  });

  EXPECT_DCHECK_DEATH({
    V8PerFrameMemoryRequest request(kMinTimeBetweenRequests, graph());
    request.StartMeasurement(graph());
  });
}

TEST_F(V8PerFrameMemoryDecoratorDeathTest, EnforceObserversRemoved) {
  EXPECT_DCHECK_DEATH({
    V8PerFrameMemoryRequest memory_request(kMinTimeBetweenRequests);
    MockV8PerFrameMemoryObserver observer;
    memory_request.AddObserver(&observer);
    // Request should explode if it still has observers registered when it goes
    // out of scope.
  });

  EXPECT_DCHECK_DEATH({
    V8PerFrameMemoryRequestAnySeq memory_request(kMinTimeBetweenRequests);
    MockV8PerFrameMemoryObserverAnySeq observer;
    memory_request.AddObserver(&observer);
    // Request should explode if it still has observers registered when it goes
    // out of scope.
  });
}

TEST_F(V8PerFrameMemoryRequestAnySeqTest, RequestIsSequenceSafe) {
  // Precondition: CallOnGraph must run on a different sequence.  Note that all
  // tasks passed to CallOnGraph will only run when run_loop.Run() is called
  // below.
  ASSERT_TRUE(GetMainThreadTaskRunner()->RunsTasksInCurrentSequence());
  PerformanceManager::CallOnGraph(
      FROM_HERE, base::BindLambdaForTesting([this] {
        EXPECT_FALSE(
            this->GetMainThreadTaskRunner()->RunsTasksInCurrentSequence());
      }));

  // Set the active contents and simulate a navigation, which adds nodes to the
  // graph.
  SetContents(CreateTestWebContents());
  content::NavigationSimulator::NavigateAndCommitFromBrowser(
      web_contents(), GURL("https://www.foo.com/"));

  // Create some test data to return for a measurement request.
  constexpr uint64_t kAssociatedBytes = 0x123;
  content::RenderFrameHost* main_frame = web_contents()->GetMainFrame();
  ASSERT_NE(nullptr, main_frame);
  const RenderProcessHostId process_id(main_frame->GetProcess()->GetID());
  const blink::LocalFrameToken frame_token(main_frame->GetFrameToken());
  const content::GlobalFrameRoutingId frame_id(process_id.value(),
                                               main_frame->GetRoutingID());

  V8PerFrameMemoryProcessData expected_process_data;
  expected_process_data.set_unassociated_v8_bytes_used(kUnassociatedBytes);
  V8PerFrameMemoryObserverAnySeq::FrameDataMap expected_frame_data;
  expected_frame_data[frame_id].set_v8_bytes_used(kAssociatedBytes);

  MockV8PerFrameMemoryReporter reporter;
  {
    auto data = blink::mojom::PerProcessV8MemoryUsageData::New();
    data->unassociated_bytes_used = kUnassociatedBytes;
    AddPerFrameIsolateMemoryUsage(frame_token, 0, kAssociatedBytes, data.get());
    ExpectBindAndRespondToQuery(&reporter, std::move(data), process_id);
  }

  // Decorator should not exist before creating a request.
  PerformanceManager::CallOnGraph(
      FROM_HERE, base::BindOnce([](Graph* graph) {
        EXPECT_FALSE(V8PerFrameMemoryDecorator::GetFromGraph(graph));
      }));

  // This object is created on the main sequence but should cause a
  // V8PerFrameMemoryRequest to be created on the graph sequence after the
  // above task.
  auto request = std::make_unique<V8PerFrameMemoryRequestAnySeq>(
      V8PerFrameMemoryDecoratorTest::kMinTimeBetweenRequests);
  MockV8PerFrameMemoryObserverAnySeq observer;
  request->AddObserver(&observer);

  // Decorator now exists and has the request frequency set, proving that the
  // V8PerFrameMemoryRequest was created.
  PerformanceManager::CallOnGraph(
      FROM_HERE, base::BindOnce([](Graph* graph) {
        auto* decorator = V8PerFrameMemoryDecorator::GetFromGraph(graph);
        ASSERT_TRUE(decorator);
        ASSERT_TRUE(decorator->GetNextRequest());
        EXPECT_EQ(V8PerFrameMemoryDecoratorTest::kMinTimeBetweenRequests,
                  decorator->GetNextRequest()->min_time_between_requests());
      }));

  // The observer should be invoked on the main sequence when a measurement is
  // available. Exit the RunLoop when this happens.
  base::RunLoop run_loop;
  EXPECT_CALL(observer,
              OnV8MemoryMeasurementAvailable(process_id, expected_process_data,
                                             expected_frame_data))
      .WillOnce([this, &run_loop, &process_id, &expected_frame_data]() {
        run_loop.Quit();
        ASSERT_TRUE(
            this->GetMainThreadTaskRunner()->RunsTasksInCurrentSequence())
            << "Observer invoked on wrong sequence";
        // Verify that the notification parameters can be used to retrieve a
        // RenderFrameHost and RenderProcessHost. This is safe on the main
        // thread.
        EXPECT_NE(nullptr,
                  content::RenderProcessHost::FromID(process_id.value()));
        const content::GlobalFrameRoutingId frame_id =
            expected_frame_data.cbegin()->first;
        EXPECT_NE(nullptr, content::RenderFrameHost::FromID(frame_id));
      });

  // Now execute all the above tasks.
  run_loop.Run();
  testing::Mock::VerifyAndClearExpectations(this);
  testing::Mock::VerifyAndClearExpectations(&reporter);
  testing::Mock::VerifyAndClearExpectations(&observer);

  // Destroying the object on the main sequence should cause the wrapped
  // V8PerFrameMemoryRequest to be destroyed on the graph sequence after any
  // scheduled tasks, which resets the request frequency to zero.
  PerformanceManager::CallOnGraph(
      FROM_HERE, base::BindOnce([](Graph* graph) {
        auto* decorator = V8PerFrameMemoryDecorator::GetFromGraph(graph);
        ASSERT_TRUE(decorator);
        ASSERT_TRUE(decorator->GetNextRequest());
        EXPECT_EQ(V8PerFrameMemoryDecoratorTest::kMinTimeBetweenRequests,
                  decorator->GetNextRequest()->min_time_between_requests());
      }));

  // Must remove the observer before destroying the request to avoid a DCHECK
  // from ObserverList.
  request->RemoveObserver(&observer);
  request.reset();

  PerformanceManager::CallOnGraph(
      FROM_HERE, base::BindOnce([](Graph* graph) {
        auto* decorator = V8PerFrameMemoryDecorator::GetFromGraph(graph);
        ASSERT_TRUE(decorator);
        EXPECT_FALSE(decorator->GetNextRequest());
      }));

  // Execute the above tasks and exit.
  base::RunLoop run_loop2;
  PerformanceManager::CallOnGraph(FROM_HERE, run_loop2.QuitClosure());
  run_loop2.Run();
}

}  // namespace v8_memory

}  // namespace performance_manager
