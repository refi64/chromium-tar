// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/hud_display/memory_graph_page_view.h"

#include <numeric>

#include "ash/hud_display/hud_constants.h"
#include "ui/gfx/canvas.h"

namespace ash {
namespace hud_display {

////////////////////////////////////////////////////////////////////////////////
// MemoryGraphPageView, public:

BEGIN_METADATA(MemoryGraphPageView)
METADATA_PARENT_CLASS(GraphPageViewBase)
END_METADATA()

MemoryGraphPageView::MemoryGraphPageView()
    : graph_chrome_rss_private_(Graph::Baseline::BASELINE_BOTTOM,
                                Graph::Fill::SOLID,
                                SkColorSetA(SK_ColorRED, kHUDAlpha)),
      graph_mem_free_(Graph::Baseline::BASELINE_BOTTOM,
                      Graph::Fill::NONE,
                      SkColorSetA(SK_ColorDKGRAY, kHUDAlpha)),
      graph_mem_used_unknown_(Graph::Baseline::BASELINE_BOTTOM,
                              Graph::Fill::SOLID,
                              SkColorSetA(SK_ColorLTGRAY, kHUDAlpha)),
      graph_renderers_rss_private_(Graph::Baseline::BASELINE_BOTTOM,
                                   Graph::Fill::SOLID,
                                   SkColorSetA(SK_ColorCYAN, kHUDAlpha)),
      graph_arc_rss_private_(Graph::Baseline::BASELINE_BOTTOM,
                             Graph::Fill::SOLID,
                             SkColorSetA(SK_ColorMAGENTA, kHUDAlpha)),
      graph_gpu_rss_private_(Graph::Baseline::BASELINE_BOTTOM,
                             Graph::Fill::SOLID,
                             SkColorSetA(SK_ColorRED, kHUDAlpha)),
      graph_gpu_kernel_(Graph::Baseline::BASELINE_BOTTOM,
                        Graph::Fill::SOLID,
                        SkColorSetA(SK_ColorYELLOW, kHUDAlpha)),
      graph_chrome_rss_shared_(Graph::Baseline::BASELINE_BOTTOM,
                               Graph::Fill::NONE,
                               SkColorSetA(SK_ColorBLUE, kHUDAlpha)) {}

MemoryGraphPageView::~MemoryGraphPageView() = default;

////////////////////////////////////////////////////////////////////////////////

void MemoryGraphPageView::OnPaint(gfx::Canvas* canvas) {
  // TODO: Should probably update last graph point more often than shift graph.

  // Layout graphs.
  const gfx::Rect rect = GetContentsBounds();
  graph_chrome_rss_private_.Layout(rect, /*base=*/nullptr);
  graph_mem_free_.Layout(rect, &graph_chrome_rss_private_);
  graph_mem_used_unknown_.Layout(rect, &graph_mem_free_);
  graph_renderers_rss_private_.Layout(rect, &graph_mem_used_unknown_);
  graph_arc_rss_private_.Layout(rect, &graph_renderers_rss_private_);
  graph_gpu_rss_private_.Layout(rect, &graph_arc_rss_private_);
  graph_gpu_kernel_.Layout(rect, &graph_gpu_rss_private_);
  // Not stacked.
  graph_chrome_rss_shared_.Layout(rect, /*base=*/nullptr);

  // Paint damaged area now that all parameters have been determined.
  graph_chrome_rss_private_.Draw(canvas);
  graph_mem_free_.Draw(canvas);
  graph_mem_used_unknown_.Draw(canvas);
  graph_renderers_rss_private_.Draw(canvas);
  graph_arc_rss_private_.Draw(canvas);
  graph_gpu_rss_private_.Draw(canvas);
  graph_gpu_kernel_.Draw(canvas);

  graph_chrome_rss_shared_.Draw(canvas);
}

void MemoryGraphPageView::UpdateData(const DataSource::Snapshot& snapshot) {
  // TODO: Should probably update last graph point more often than shift graph.
  const double total = snapshot.total_ram;
  // Nothing to do if data is not available yet.
  if (total < 1)
    return;

  const float chrome_rss_private =
      (snapshot.browser_rss - snapshot.browser_rss_shared) / total;
  const float mem_free = snapshot.free_ram / total;
  // mem_used_unknown is calculated below.
  const float renderers_rss_private =
      (snapshot.renderers_rss - snapshot.renderers_rss_shared) / total;
  const float arc_rss_private =
      (snapshot.arc_rss - snapshot.arc_rss_shared) / total;
  const float gpu_rss_private =
      (snapshot.gpu_rss - snapshot.gpu_rss_shared) / total;
  const float gpu_kernel = snapshot.gpu_kernel / total;

  // not stacked.
  const float chrome_rss_shared = snapshot.browser_rss_shared / total;

  std::vector<float> used_buckets;
  used_buckets.push_back(chrome_rss_private);
  used_buckets.push_back(mem_free);
  used_buckets.push_back(renderers_rss_private);
  used_buckets.push_back(arc_rss_private);
  used_buckets.push_back(gpu_rss_private);
  used_buckets.push_back(gpu_kernel);

  float mem_used_unknown =
      1 - std::accumulate(used_buckets.begin(), used_buckets.end(), 0.0f);

  if (mem_used_unknown < 0)
    LOG(WARNING) << "mem_used_unknown=" << mem_used_unknown << " < 0 !";

  // Update graph data.
  graph_chrome_rss_private_.AddValue(chrome_rss_private);
  graph_mem_free_.AddValue(mem_free);
  graph_mem_used_unknown_.AddValue(std::max(mem_used_unknown, 0.0f));
  graph_renderers_rss_private_.AddValue(renderers_rss_private);
  graph_arc_rss_private_.AddValue(arc_rss_private);
  graph_gpu_rss_private_.AddValue(gpu_rss_private);
  graph_gpu_kernel_.AddValue(gpu_kernel);
  // Not stacked.
  graph_chrome_rss_shared_.AddValue(chrome_rss_shared);
}

}  // namespace hud_display
}  // namespace ash
