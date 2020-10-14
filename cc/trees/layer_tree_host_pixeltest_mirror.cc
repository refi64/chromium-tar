// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"
#include "cc/layers/mirror_layer.h"
#include "cc/layers/solid_color_layer.h"
#include "cc/test/layer_tree_pixel_test.h"
#include "cc/test/pixel_comparator.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/transform_util.h"

#if !defined(OS_ANDROID)

namespace cc {
namespace {

class LayerTreeHostMirrorPixelTest
    : public LayerTreePixelTest,
      public ::testing::WithParamInterface<TestRendererType> {
 protected:
  LayerTreeHostMirrorPixelTest() : LayerTreePixelTest(renderer_type()) {}

  TestRendererType renderer_type() const { return GetParam(); }
};

const TestRendererType kRendererTypes[] = {
    TestRendererType::kGL,       TestRendererType::kSkiaGL,
    TestRendererType::kSoftware,
#if defined(ENABLE_CC_VULKAN_TESTS)
    TestRendererType::kSkiaVk,
#endif  // defined(ENABLE_CC_VULKAN_TESTS)
#if defined(ENABLE_CC_DAWN_TESTS)
    TestRendererType::kSkiaDawn,
#endif  // defined(ENABLE_CC_DAWN_TESTS)
};

INSTANTIATE_TEST_SUITE_P(All,
                         LayerTreeHostMirrorPixelTest,
                         ::testing::ValuesIn(kRendererTypes));

// Verifies that a mirror layer with a scale mirrors another layer correctly.
TEST_P(LayerTreeHostMirrorPixelTest, MirrorLayer) {
  const float scale = 2.f;
  gfx::Rect background_bounds(120, 180);
  gfx::Rect mirrored_bounds(10, 10, 50, 50);
  gfx::Rect mirror_bounds(10, 70, 100, 100);

  auto background = CreateSolidColorLayer(background_bounds, SK_ColorWHITE);

  auto mirrored_layer = CreateSolidColorLayerWithBorder(
      mirrored_bounds, SK_ColorGREEN, 5, SK_ColorBLUE);

  auto mirror_layer = MirrorLayer::Create(mirrored_layer);
  mirror_layer->SetIsDrawable(true);
  mirror_layer->SetBounds(mirror_bounds.size());
  mirror_layer->SetPosition(gfx::PointF(mirror_bounds.origin()));
  mirror_layer->SetTransform(gfx::GetScaleTransform(gfx::Point(), scale));
  background->AddChild(mirrored_layer);
  background->AddChild(mirror_layer);

  if (use_software_renderer()) {
    const bool discard_alpha = true;
    const float error_pixels_percentage_limit = 3.f;
    const float small_error_pixels_percentage_limit = 0.f;
    const float avg_abs_error_limit = 65.f;
    const int max_abs_error_limit = 120;
    const int small_error_threshold = 0;
    pixel_comparator_ = std::make_unique<FuzzyPixelComparator>(
        discard_alpha, error_pixels_percentage_limit,
        small_error_pixels_percentage_limit, avg_abs_error_limit,
        max_abs_error_limit, small_error_threshold);
  }

  if (use_skia_vulkan())
    pixel_comparator_ = std::make_unique<FuzzyPixelOffByOneComparator>(true);

  RunPixelTest(background,
               base::FilePath(FILE_PATH_LITERAL("mirror_layer.png")));
}

}  // namespace
}  // namespace cc

#endif  // !defined(OS_ANDROID)
