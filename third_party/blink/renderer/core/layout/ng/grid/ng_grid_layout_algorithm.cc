// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/ng/grid/ng_grid_layout_algorithm.h"

#include "third_party/blink/renderer/core/layout/ng/grid/ng_grid_child_iterator.h"
#include "third_party/blink/renderer/core/layout/ng/grid/ng_grid_placement.h"
#include "third_party/blink/renderer/core/layout/ng/ng_constraint_space_builder.h"
#include "third_party/blink/renderer/core/layout/ng/ng_length_utils.h"
#include "third_party/blink/renderer/core/layout/ng/ng_out_of_flow_layout_part.h"
#include "third_party/blink/renderer/core/layout/ng/ng_space_utils.h"

namespace blink {

NGGridLayoutAlgorithm::NGGridLayoutAlgorithm(
    const NGLayoutAlgorithmParams& params)
    : NGLayoutAlgorithm(params) {
  DCHECK(params.space.IsNewFormattingContext());
  DCHECK(!params.break_token);

  border_box_size_ = container_builder_.InitialBorderBoxSize();
  child_percentage_size_ = CalculateChildPercentageSize(
      ConstraintSpace(), Node(), ChildAvailableSize());
}

scoped_refptr<const NGLayoutResult> NGGridLayoutAlgorithm::Layout() {
  // Measure items.
  Vector<GridItemData> grid_items;
  Vector<GridItemData> out_of_flow_items;
  ConstructAndAppendGridItems(&grid_items, &out_of_flow_items);

  NGGridLayoutAlgorithmTrackCollection algorithm_column_track_collection;
  NGGridLayoutAlgorithmTrackCollection algorithm_row_track_collection;
  NGGridPlacement grid_placement(Style(),
                                 ComputeAutomaticRepetitions(kForColumns),
                                 ComputeAutomaticRepetitions(kForRows));

  BuildAlgorithmTrackCollections(
      &grid_items, &algorithm_column_track_collection,
      &algorithm_row_track_collection, &grid_placement);

  // Cache set indices.
  CacheItemSetIndices(algorithm_column_track_collection, &grid_items);
  CacheItemSetIndices(algorithm_row_track_collection, &grid_items);

  // Create a vector of grid item indices using |NGGridChildIterator| order.
  Vector<wtf_size_t> reordered_item_indices(grid_items.size());
  for (wtf_size_t i = 0; i < grid_items.size(); ++i)
    reordered_item_indices[i] = i;

  // Cache track span properties for grid items.
  CacheGridItemsTrackSpanProperties(algorithm_column_track_collection,
                                    &grid_items, &reordered_item_indices);
  CacheGridItemsTrackSpanProperties(algorithm_row_track_collection, &grid_items,
                                    &reordered_item_indices);

  // Resolve inline size.
  ComputeUsedTrackSizes(&algorithm_column_track_collection, &grid_items,
                        &reordered_item_indices);
  // Resolve block size.
  ComputeUsedTrackSizes(&algorithm_row_track_collection, &grid_items,
                        &reordered_item_indices);

  // Place grid and out of flow items.
  LayoutUnit intrinsic_block_size;
  LayoutUnit block_size;
  PlaceItems(grid_items, algorithm_column_track_collection,
             algorithm_row_track_collection, &out_of_flow_items,
             &intrinsic_block_size, &block_size);

  container_builder_.SetIntrinsicBlockSize(intrinsic_block_size);
  container_builder_.SetFragmentsTotalBlockSize(block_size);

  NGOutOfFlowLayoutPart(Node(), ConstraintSpace(), &container_builder_).Run();
  return container_builder_.ToBoxFragment();
}

MinMaxSizesResult NGGridLayoutAlgorithm::ComputeMinMaxSizes(
    const MinMaxSizesInput& input) const {
  // TODO(janewman): Handle the cases typically done via:
  // CalculateMinMaxSizesIgnoringChildren.

  // Measure Items
  Vector<GridItemData> grid_items;
  ConstructAndAppendGridItems(&grid_items);

  NGGridLayoutAlgorithmTrackCollection algorithm_column_track_collection;
  NGGridLayoutAlgorithmTrackCollection algorithm_row_track_collection;
  NGGridPlacement grid_placement(Style(),
                                 ComputeAutomaticRepetitions(kForColumns),
                                 ComputeAutomaticRepetitions(kForRows));

  BuildAlgorithmTrackCollections(
      &grid_items, &algorithm_column_track_collection,
      &algorithm_row_track_collection, &grid_placement);

  // Cache set indices.
  CacheItemSetIndices(algorithm_column_track_collection, &grid_items);

  // Create a vector of grid item indices using |NGGridChildIterator| order.
  Vector<wtf_size_t> reordered_item_indices(grid_items.size());
  for (wtf_size_t i = 0; i < grid_items.size(); ++i)
    reordered_item_indices[i] = i;

  // Cache track span properties for grid items.
  CacheGridItemsTrackSpanProperties(algorithm_column_track_collection,
                                    &grid_items, &reordered_item_indices);

  // Resolve inline size.
  ComputeUsedTrackSizes(&algorithm_column_track_collection, &grid_items,
                        &reordered_item_indices);

  const LayoutUnit grid_gap = GridGap(kForColumns);

  // Now the columns should have their used track size and growth limit, each
  // adding up to match the min and max size of the grid respectively.
  MinMaxSizes grid_min_max_sizes;

  // If the track collection does not have any tracks, then we do not want to
  // subtract the grid gap from the last track.
  bool has_tracks = false;
  for (auto column_set_iterator =
           algorithm_column_track_collection.GetSetIterator();
       !column_set_iterator.IsAtEnd(); column_set_iterator.MoveToNextSet()) {
    const auto& set = column_set_iterator.CurrentSet();
    has_tracks |= set.TrackCount();
    LayoutUnit gap = set.TrackCount() * grid_gap;

    // Aggregate min/max size contributions for this set of tracks.
    LayoutUnit min_size_contribution = set.BaseSize() + gap;
    grid_min_max_sizes.min_size += min_size_contribution;
    grid_min_max_sizes.max_size += set.GrowthLimit() == kIndefiniteSize
                                       ? min_size_contribution
                                       : set.GrowthLimit() + gap;
  }

  // Subtract the gap from the end of the last track. Only do this if there is
  // at least one track.
  if (has_tracks)
    grid_min_max_sizes -= grid_gap;

  grid_min_max_sizes += BorderScrollbarPadding().InlineSum();

  // TODO(janewman): determine what cases need depends_on_percentage_block_size
  // to be set.
  return {grid_min_max_sizes, /* depends_on_percentage_block_size */ true};
}

NGGridLayoutAlgorithm::AutoPlacementType
NGGridLayoutAlgorithm::GridItemData::AutoPlacement(
    GridTrackSizingDirection flow_direction) const {
  bool is_major_indefinite = Span(flow_direction).IsIndefinite();
  bool is_minor_indefinite =
      Span(flow_direction == kForColumns ? kForRows : kForColumns)
          .IsIndefinite();

  if (is_minor_indefinite && is_major_indefinite)
    return AutoPlacementType::kBoth;
  else if (is_minor_indefinite)
    return AutoPlacementType::kMinor;
  else if (is_major_indefinite)
    return AutoPlacementType::kMajor;

  return AutoPlacementType::kNotNeeded;
}

const GridSpan& NGGridLayoutAlgorithm::GridItemData::Span(
    GridTrackSizingDirection track_direction) const {
  return (track_direction == kForColumns) ? resolved_position.columns
                                          : resolved_position.rows;
}

void NGGridLayoutAlgorithm::GridItemData::SetSpan(
    const GridSpan& span,
    GridTrackSizingDirection track_direction) {
  if (track_direction == kForColumns)
    resolved_position.columns = span;
  else
    resolved_position.rows = span;
}

wtf_size_t NGGridLayoutAlgorithm::GridItemData::StartLine(
    GridTrackSizingDirection track_direction) const {
  const GridSpan& span = (track_direction == kForColumns)
                             ? resolved_position.columns
                             : resolved_position.rows;
  return span.StartLine();
}

wtf_size_t NGGridLayoutAlgorithm::GridItemData::EndLine(
    GridTrackSizingDirection track_direction) const {
  const GridSpan& span = (track_direction == kForColumns)
                             ? resolved_position.columns
                             : resolved_position.rows;
  return span.EndLine();
}

wtf_size_t NGGridLayoutAlgorithm::GridItemData::SpanSize(
    GridTrackSizingDirection track_direction) const {
  const GridSpan& span = (track_direction == kForColumns)
                             ? resolved_position.columns
                             : resolved_position.rows;
  return span.IntegerSpan();
}

const TrackSpanProperties&
NGGridLayoutAlgorithm::GridItemData::GetTrackSpanProperties(
    GridTrackSizingDirection track_direction) const {
  return track_direction == kForColumns ? column_span_properties
                                        : row_span_properties;
}

void NGGridLayoutAlgorithm::GridItemData::SetTrackSpanProperty(
    TrackSpanProperties::PropertyId property,
    GridTrackSizingDirection track_direction) {
  if (track_direction == kForColumns)
    column_span_properties.SetProperty(property);
  else
    row_span_properties.SetProperty(property);
}

bool NGGridLayoutAlgorithm::GridItemData::IsSpanningFlexibleTrack(
    GridTrackSizingDirection track_direction) const {
  return GetTrackSpanProperties(track_direction)
      .HasProperty(TrackSpanProperties::kHasFlexibleTrack);
}

bool NGGridLayoutAlgorithm::GridItemData::IsSpanningIntrinsicTrack(
    GridTrackSizingDirection track_direction) const {
  return GetTrackSpanProperties(track_direction)
      .HasProperty(TrackSpanProperties::kHasIntrinsicTrack);
}

NGGridLayoutAlgorithm::ReorderedGridItems::Iterator::Iterator(
    Vector<wtf_size_t>::const_iterator current_index,
    Vector<GridItemData>* grid_items)
    : current_index_(current_index), grid_items_(grid_items) {}

bool NGGridLayoutAlgorithm::ReorderedGridItems::Iterator::operator!=(
    const Iterator& other) const {
  return grid_items_ != other.grid_items_ ||
         current_index_ != other.current_index_;
}

NGGridLayoutAlgorithm::GridItemData*
NGGridLayoutAlgorithm::ReorderedGridItems::Iterator::operator->() {
  DCHECK_LT(*current_index_, grid_items_->size());
  return &(grid_items_->at(*current_index_));
}

NGGridLayoutAlgorithm::GridItemData&
NGGridLayoutAlgorithm::ReorderedGridItems::Iterator::operator*() {
  DCHECK_LT(*current_index_, grid_items_->size());
  return grid_items_->at(*current_index_);
}

NGGridLayoutAlgorithm::ReorderedGridItems::Iterator&
NGGridLayoutAlgorithm::ReorderedGridItems::Iterator::operator++() {
  ++current_index_;
  return *this;
}

NGGridLayoutAlgorithm::ReorderedGridItems::ReorderedGridItems(
    const Vector<wtf_size_t>& reordered_item_indices,
    Vector<GridItemData>& grid_items)
    : reordered_item_indices_(reordered_item_indices),
      grid_items_(grid_items) {}

NGGridLayoutAlgorithm::ReorderedGridItems::Iterator
NGGridLayoutAlgorithm::ReorderedGridItems::begin() {
  return Iterator(reordered_item_indices_.begin(), &grid_items_);
}

NGGridLayoutAlgorithm::ReorderedGridItems::Iterator
NGGridLayoutAlgorithm::ReorderedGridItems::end() {
  return Iterator(reordered_item_indices_.end(), &grid_items_);
}

NGGridLayoutAlgorithmTrackCollection::SetIterator
NGGridLayoutAlgorithm::GetSetIteratorForItem(
    const GridItemData& item,
    NGGridLayoutAlgorithmTrackCollection& track_collection) {
  return track_collection.GetSetIterator(
      track_collection.IsForColumns() ? item.columns_begin_set_index
                                      : item.rows_begin_set_index,
      track_collection.IsForColumns() ? item.columns_end_set_index
                                      : item.rows_end_set_index);
}

// TODO(ethavar): Current implementation of this method simply returns the
// preferred size of the grid item in the relevant direction. We should follow
// the definitions from https://drafts.csswg.org/css-grid-1/#algo-spanning-items
// (i.e. compute minimum, min-content, and max-content contributions).
LayoutUnit NGGridLayoutAlgorithm::ContributionSizeForGridItem(
    const GridItemData& grid_item,
    GridTrackSizingDirection track_direction,
    GridItemContributionType contribution_type) const {
  const ComputedStyle& grid_item_style = grid_item.node.Style();
  GridTrackSizingDirection grid_item_track_direction = track_direction;

  bool is_orthogonal_grid_item = Style().IsHorizontalWritingMode() ==
                                 grid_item_style.IsHorizontalWritingMode();
  if (is_orthogonal_grid_item) {
    grid_item_track_direction =
        (track_direction == kForColumns) ? kForRows : kForColumns;
  }

  Length length = (grid_item_track_direction == kForColumns)
                      ? grid_item_style.LogicalWidth()
                      : grid_item_style.LogicalHeight();
  return length.IsFixed() ? MinimumValueForLength(length, kIndefiniteSize)
                          : LayoutUnit();
}

void NGGridLayoutAlgorithm::ConstructAndAppendGridItems(
    Vector<GridItemData>* grid_items,
    Vector<GridItemData>* out_of_flow_items) const {
  DCHECK(grid_items);
  NGGridChildIterator iterator(Node());
  for (NGBlockNode child = iterator.NextChild(); child;
       child = iterator.NextChild()) {
    GridItemData grid_item = MeasureGridItem(child);
    // If |out_of_flow_items| is provided, store out-of-flow items separately,
    // as they do not contribute to track sizing or auto-placement.
    if (grid_item.item_type == ItemType::kInGridFlow)
      grid_items->emplace_back(grid_item);
    else if (out_of_flow_items)
      out_of_flow_items->emplace_back(grid_item);
  }
}

wtf_size_t NGGridLayoutAlgorithm::ComputeAutomaticRepetitions(
    GridTrackSizingDirection track_direction) const {
  // TODO(kschmi): Auto track repeat count should be based on the number of
  // children, rather than specified auto-column/track. Temporarily assign them
  // to zero here to avoid DCHECK's until we implement this logic.
  return 0;
}

namespace {

using AxisEdge = NGGridLayoutAlgorithm::AxisEdge;

// Given an |item_position| determines the correct |AxisEdge| alignment.
// Additionally will determine if the grid-item should be stretched with the
// |is_stretched| out-parameter.
AxisEdge AxisEdgeFromItemPosition(const ComputedStyle& container_style,
                                  const ComputedStyle& style,
                                  const ItemPosition item_position,
                                  bool is_inline_axis,
                                  bool* is_stretched) {
  DCHECK(is_stretched);
  *is_stretched = false;

  // Auto-margins take precedence over any alignment properties.
  if (style.MayHaveMargin()) {
    bool start_auto = is_inline_axis
                          ? style.MarginStartUsing(container_style).IsAuto()
                          : style.MarginBeforeUsing(container_style).IsAuto();
    bool end_auto = is_inline_axis
                        ? style.MarginEndUsing(container_style).IsAuto()
                        : style.MarginAfterUsing(container_style).IsAuto();

    if (start_auto && end_auto)
      return AxisEdge::kCenter;
    else if (start_auto)
      return AxisEdge::kEnd;
    else if (end_auto)
      return AxisEdge::kStart;
  }

  const auto container_writing_direction =
      container_style.GetWritingDirection();

  switch (item_position) {
    case ItemPosition::kSelfStart:
    case ItemPosition::kSelfEnd: {
      // In order to determine the correct "self" axis-edge without a
      // complicated set of if-branches we use two converters.

      // First use the grid-item's writing-direction to convert the logical
      // edge into the physical coordinate space.
      LogicalToPhysical<AxisEdge> physical(style.GetWritingDirection(),
                                           AxisEdge::kStart, AxisEdge::kEnd,
                                           AxisEdge::kStart, AxisEdge::kEnd);

      // Then use the container's writing-direction to convert the physical
      // edges, into our logical coordinate space.
      PhysicalToLogical<AxisEdge> logical(container_writing_direction,
                                          physical.Top(), physical.Right(),
                                          physical.Bottom(), physical.Left());

      if (is_inline_axis) {
        return item_position == ItemPosition::kSelfStart ? logical.InlineStart()
                                                         : logical.InlineEnd();
      }
      return item_position == ItemPosition::kSelfStart ? logical.BlockStart()
                                                       : logical.BlockEnd();
    }
    case ItemPosition::kCenter:
      return AxisEdge::kCenter;
    case ItemPosition::kFlexStart:
    case ItemPosition::kStart:
      return AxisEdge::kStart;
    case ItemPosition::kFlexEnd:
    case ItemPosition::kEnd:
      return AxisEdge::kEnd;
    case ItemPosition::kStretch:
      *is_stretched = true;
      return AxisEdge::kStart;
    case ItemPosition::kBaseline:
    case ItemPosition::kLastBaseline:
      return AxisEdge::kBaseline;
    case ItemPosition::kLeft:
      DCHECK(is_inline_axis);
      return container_writing_direction.IsLtr() ? AxisEdge::kStart
                                                 : AxisEdge::kEnd;
    case ItemPosition::kRight:
      DCHECK(is_inline_axis);
      return container_writing_direction.IsRtl() ? AxisEdge::kStart
                                                 : AxisEdge::kEnd;
    case ItemPosition::kLegacy:
    case ItemPosition::kAuto:
    case ItemPosition::kNormal:
      NOTREACHED();
      break;
  }

  NOTREACHED();
  return AxisEdge::kStart;
}

}  // namespace

NGGridLayoutAlgorithm::GridItemData NGGridLayoutAlgorithm::MeasureGridItem(
    const NGBlockNode node) const {
  const auto& container_style = Style();

  // Before we take track sizing into account for column width contributions,
  // have all child inline and min/max sizes measured for content-based width
  // resolution.
  GridItemData grid_item(node);
  const ComputedStyle& child_style = node.Style();
  bool is_orthogonal_flow_root = !IsParallelWritingMode(
      container_style.GetWritingMode(), child_style.GetWritingMode());
  NGConstraintSpace constraint_space = BuildSpaceForGridItem(node);

  // Children with orthogonal writing modes require a full layout pass to
  // determine inline size.
  if (is_orthogonal_flow_root) {
    scoped_refptr<const NGLayoutResult> result = node.Layout(constraint_space);
    grid_item.inline_size = NGFragment(ConstraintSpace().GetWritingDirection(),
                                       result->PhysicalFragment())
                                .InlineSize();
  } else {
    NGBoxStrut border_padding_in_child_writing_mode =
        ComputeBorders(constraint_space, node) +
        ComputePadding(constraint_space, child_style);
    grid_item.inline_size = ComputeInlineSizeForFragment(
        constraint_space, node, border_padding_in_child_writing_mode);
  }

  const ItemPosition normal_behaviour =
      node.IsReplaced() ? ItemPosition::kStart : ItemPosition::kStretch;

  // Determine the alignment for the grid-item ahead of time (we may need to
  // know if it stretches ahead of time to correctly determine any block-axis
  // contribution).
  grid_item.inline_axis_alignment = AxisEdgeFromItemPosition(
      container_style, child_style,
      child_style.ResolvedJustifySelf(normal_behaviour, &container_style)
          .GetPosition(),
      /* is_inline_axis */ true, &grid_item.is_inline_axis_stretched);
  grid_item.block_axis_alignment = AxisEdgeFromItemPosition(
      container_style, child_style,
      child_style.ResolvedAlignSelf(normal_behaviour, &container_style)
          .GetPosition(),
      /* is_inline_axis */ false, &grid_item.is_block_axis_stretched);

  grid_item.margins =
      ComputeMarginsFor(constraint_space, child_style, ConstraintSpace());
  grid_item.min_max_sizes =
      node.ComputeMinMaxSizes(
              container_style.GetWritingMode(),
              MinMaxSizesInput(child_percentage_size_.block_size,
                               MinMaxSizesType::kContent),
              &constraint_space)
          .sizes;
  grid_item.item_type = node.IsOutOfFlowPositioned() ? ItemType::kOutOfFlow
                                                     : ItemType::kInGridFlow;

  return grid_item;
}

NGConstraintSpace NGGridLayoutAlgorithm::BuildSpaceForGridItem(
    const NGBlockNode node) const {
  const auto& style = node.Style();
  NGConstraintSpaceBuilder builder(ConstraintSpace(),
                                   style.GetWritingDirection(),
                                   /* is_new_fc */ true);
  SetOrthogonalFallbackInlineSizeIfNeeded(Style(), node, &builder);
  builder.SetCacheSlot(NGCacheSlot::kMeasure);
  builder.SetIsPaintedAtomically(true);
  builder.SetAvailableSize(ChildAvailableSize());
  builder.SetPercentageResolutionSize(child_percentage_size_);
  return builder.ToConstraintSpace();
}

void NGGridLayoutAlgorithm::BuildBlockTrackCollections(
    Vector<GridItemData>* grid_items,
    NGGridBlockTrackCollection* column_track_collection,
    NGGridBlockTrackCollection* row_track_collection,
    NGGridPlacement* grid_placement) const {
  DCHECK(grid_items);
  DCHECK(column_track_collection);
  DCHECK(row_track_collection);
  DCHECK(grid_placement);

  SetSpecifiedTracks(grid_placement->AutoRepetitions(kForColumns),
                     column_track_collection);
  SetSpecifiedTracks(grid_placement->AutoRepetitions(kForRows),
                     row_track_collection);

  grid_placement->RunAutoPlacementAlgorithm(grid_items);
  EnsureTrackCoverageForGridItems(*grid_items, column_track_collection);
  EnsureTrackCoverageForGridItems(*grid_items, row_track_collection);

  column_track_collection->FinalizeRanges();
  row_track_collection->FinalizeRanges();
}

void NGGridLayoutAlgorithm::BuildAlgorithmTrackCollections(
    Vector<GridItemData>* grid_items,
    NGGridLayoutAlgorithmTrackCollection* column_track_collection,
    NGGridLayoutAlgorithmTrackCollection* row_track_collection,
    NGGridPlacement* grid_placement) const {
  DCHECK(grid_items);
  DCHECK(column_track_collection);
  DCHECK(row_track_collection);
  DCHECK(grid_placement);

  // Build block track collections.
  NGGridBlockTrackCollection column_block_track_collection(kForColumns);
  NGGridBlockTrackCollection row_block_track_collection(kForRows);
  BuildBlockTrackCollections(grid_items, &column_block_track_collection,
                             &row_block_track_collection, grid_placement);

  // Build algorithm track collections from the block track collections.
  DCHECK_NE(child_percentage_size_.inline_size, kIndefiniteSize);
  *column_track_collection = NGGridLayoutAlgorithmTrackCollection(
      column_block_track_collection,
      /* is_content_box_size_indefinite */ false);

  bool is_content_box_block_size_indefinite =
      child_percentage_size_.block_size == kIndefiniteSize;
  *row_track_collection = NGGridLayoutAlgorithmTrackCollection(
      row_block_track_collection, is_content_box_block_size_indefinite);
}

void NGGridLayoutAlgorithm::SetSpecifiedTracks(
    wtf_size_t auto_repetitions,
    NGGridBlockTrackCollection* track_collection) const {
  DCHECK(track_collection);
  const ComputedStyle& grid_style = Style();

  const NGGridTrackList& template_track_list =
      track_collection->IsForColumns()
          ? grid_style.GridTemplateColumns().NGTrackList()
          : grid_style.GridTemplateRows().NGTrackList();
  const NGGridTrackList& auto_track_list =
      track_collection->IsForColumns()
          ? grid_style.GridAutoColumns().NGTrackList()
          : grid_style.GridAutoRows().NGTrackList();
  track_collection->SetSpecifiedTracks(&template_track_list, &auto_track_list,
                                       auto_repetitions);
}

void NGGridLayoutAlgorithm::EnsureTrackCoverageForGridItems(
    const Vector<GridItemData>& grid_items,
    NGGridBlockTrackCollection* track_collection) const {
  DCHECK(track_collection);
  const GridTrackSizingDirection track_direction =
      track_collection->Direction();
  for (const auto& grid_item : grid_items) {
    track_collection->EnsureTrackCoverage(grid_item.StartLine(track_direction),
                                          grid_item.SpanSize(track_direction));
  }
}

void NGGridLayoutAlgorithm::CacheItemSetIndices(
    const NGGridLayoutAlgorithmTrackCollection& track_collection,
    Vector<GridItemData>* items) const {
  DCHECK(items);
  const GridTrackSizingDirection track_direction = track_collection.Direction();
  for (GridItemData& item : *items) {
    wtf_size_t start_line, end_line;
    if (item.item_type == ItemType::kInGridFlow) {
      start_line = item.StartLine(track_direction);
      end_line = item.EndLine(track_direction) - 1;
      DCHECK_NE(start_line, kNotFound);
      DCHECK_NE(end_line, kNotFound);
    } else {
      ResolveOutOfFlowItemGridLines(item, track_collection, &start_line,
                                    &end_line);
    }

    // We only calculate the indexes if:
    // 1. The item is in flow (it is a grid item) or
    // 2. The item is out of flow, but the line was not defined as 'auto' and
    // the line is within the bounds of the grid, since an out of flow item
    // cannot create grid lines.
    // TODO(ansollan): The start line of an out of flow item can be the last
    // line of the grid. If that is the case, begin_set_index has to be
    // computed as end_set_index. Similarly, if an end line is the first line
    // of the grid, end_set_index has to be computed as begin_set_index.
    wtf_size_t begin_set_index = kNotFound;
    wtf_size_t end_set_index = kNotFound;
    if (start_line != kNotFound) {
      wtf_size_t first_spanned_range =
          track_collection.RangeIndexFromTrackNumber(start_line);
      begin_set_index =
          track_collection.RangeStartingSetIndex(first_spanned_range);
    }
    if (end_line != kNotFound) {
      wtf_size_t last_spanned_range =
          track_collection.RangeIndexFromTrackNumber(end_line);
      end_set_index =
          track_collection.RangeStartingSetIndex(last_spanned_range) +
          track_collection.RangeSetCount(last_spanned_range);
    }

#if DCHECK_IS_ON()
    if (begin_set_index != kNotFound && end_set_index != kNotFound) {
      DCHECK_LE(end_set_index, track_collection.SetCount());
      DCHECK_LT(begin_set_index, end_set_index);
    } else if (begin_set_index != kNotFound) {
      DCHECK_LT(begin_set_index, track_collection.SetCount());
    } else if (end_set_index != kNotFound) {
      DCHECK_LE(end_set_index, track_collection.SetCount());
    }
#endif

    if (track_direction == kForColumns) {
      item.columns_begin_set_index = begin_set_index;
      item.columns_end_set_index = end_set_index;
    } else {
      item.rows_begin_set_index = begin_set_index;
      item.rows_end_set_index = end_set_index;
    }
  }
}

// TODO(ansollan): Move ResolveOutOfFlowItemGridLines to NGGridPlacement and
// pass |automatic_repetitions| and |explicit_start| variables.
void NGGridLayoutAlgorithm::ResolveOutOfFlowItemGridLines(
    const GridItemData& out_of_flow_item,
    const NGGridLayoutAlgorithmTrackCollection& track_collection,
    wtf_size_t* start_line,
    wtf_size_t* end_line) const {
  DCHECK(start_line);
  DCHECK(end_line);

  const ComputedStyle& out_of_flow_item_style = out_of_flow_item.node.Style();
  const GridTrackSizingDirection track_direction = track_collection.Direction();
  GridSpan span = GridPositionsResolver::ResolveGridPositionsFromStyle(
      Style(), out_of_flow_item_style, track_direction, 0);
  if (span.IsIndefinite()) {
    *start_line = kNotFound;
    *end_line = kNotFound;
    return;
  } else if (span.UntranslatedStartLine() > -1) {
    // TODO(ansollan): Handle out of flow positioned items with negative
    // indexes.
    span.Translate(0);
  }

  *start_line = span.StartLine();
  *end_line = span.EndLine() - 1;
  if (!track_collection.IsTrackWithinBounds(*start_line) ||
      (track_direction == kForColumns
           ? out_of_flow_item_style.GridColumnStart().IsAuto()
           : out_of_flow_item_style.GridRowStart().IsAuto()))
    *start_line = kNotFound;
  if (!track_collection.IsTrackWithinBounds(*end_line) ||
      (track_direction == kForColumns
           ? out_of_flow_item_style.GridColumnEnd().IsAuto()
           : out_of_flow_item_style.GridRowEnd().IsAuto()))
    *end_line = kNotFound;
}

void NGGridLayoutAlgorithm::CacheGridItemsTrackSpanProperties(
    const NGGridLayoutAlgorithmTrackCollection& track_collection,
    Vector<GridItemData>* grid_items,
    Vector<wtf_size_t>* reordered_item_indices) const {
  DCHECK(grid_items && reordered_item_indices);
  const GridTrackSizingDirection track_direction = track_collection.Direction();

  auto CompareGridItemsByStartLine = [grid_items, track_direction](
                                         wtf_size_t index_a,
                                         wtf_size_t index_b) -> bool {
    return grid_items->at(index_a).StartLine(track_direction) <
           grid_items->at(index_b).StartLine(track_direction);
  };
  std::sort(reordered_item_indices->begin(), reordered_item_indices->end(),
            CompareGridItemsByStartLine);

  auto CacheTrackSpanPropertyForAllGridItems =
      [&](TrackSpanProperties::PropertyId property) {
        // At this point we have the grid items sorted by their start line in
        // the respective direction; this is important since we'll process both,
        // the ranges in the track collection and the grid items, incrementally.
        auto range_iterator = track_collection.RangeIterator();

        for (GridItemData& grid_item :
             ReorderedGridItems(*reordered_item_indices, *grid_items)) {
          // We want to find the first range in the collection that:
          //   - Spans tracks located AFTER the start line of the current grid
          //   item; this can be done by checking that the last track number of
          //   the current range is NOT less than the current grid item's start
          //   line. Furthermore, since grid items are sorted by start line, if
          //   at any point a range is located BEFORE the current grid item's
          //   start line, the same range will also be located BEFORE any
          //   subsequent item's start line.
          //   - Contains a track that fulfills the specified property.
          while (!range_iterator.IsAtEnd() &&
                 (range_iterator.RangeTrackEnd() <
                      grid_item.StartLine(track_direction) ||
                  !track_collection.RangeHasTrackSpanProperty(
                      range_iterator.RangeIndex(), property))) {
            range_iterator.MoveToNextRange();
          }

          // Since we discarded every range in the track collection, any
          // following grid item cannot fulfill the property.
          if (range_iterator.IsAtEnd())
            break;

          // Notice that, from the way we build the ranges of a track collection
          // (see |NGGridBlockTrackCollection::EnsureTrackCoverage|), any given
          // range must either be completely contained or excluded from a grid
          // item's span. Thus, if the current range's last track is also
          // located BEFORE the item's end line, then this range, including a
          // track that fulfills the specified property, is completely contained
          // within this item's boundaries. Otherwise, this and every subsequent
          // range are excluded from the grid item's span, meaning that such
          // item cannot satisfy the property we are looking for.
          if (range_iterator.RangeTrackEnd() <
              grid_item.EndLine(track_direction)) {
            grid_item.SetTrackSpanProperty(property, track_direction);
          }
        }
      };

  CacheTrackSpanPropertyForAllGridItems(TrackSpanProperties::kHasFlexibleTrack);
  CacheTrackSpanPropertyForAllGridItems(
      TrackSpanProperties::kHasIntrinsicTrack);
}

// https://drafts.csswg.org/css-grid-1/#algo-track-sizing
void NGGridLayoutAlgorithm::ComputeUsedTrackSizes(
    NGGridLayoutAlgorithmTrackCollection* track_collection,
    Vector<GridItemData>* grid_items,
    Vector<wtf_size_t>* reordered_item_indices) const {
  DCHECK(track_collection && grid_items);
  LayoutUnit content_box_size = track_collection->IsForColumns()
                                    ? child_percentage_size_.inline_size
                                    : child_percentage_size_.block_size;

  // 1. Initialize track sizes (https://drafts.csswg.org/css-grid-1/#algo-init).
  for (auto set_iterator = track_collection->GetSetIterator();
       !set_iterator.IsAtEnd(); set_iterator.MoveToNextSet()) {
    NGGridSet& current_set = set_iterator.CurrentSet();
    const GridTrackSize& track_size = current_set.TrackSize();

    if (track_size.HasFixedMinTrackBreadth()) {
      // Indefinite lengths cannot occur, as they’re treated as 'auto'.
      DCHECK(!track_size.MinTrackBreadth().HasPercentage() ||
             content_box_size != kIndefiniteSize);

      // A fixed sizing function: Resolve to an absolute length and use that
      // size as the track’s initial base size.
      LayoutUnit fixed_min_breadth = MinimumValueForLength(
          track_size.MinTrackBreadth().length(), content_box_size);
      current_set.SetBaseSize(fixed_min_breadth * current_set.TrackCount());
    } else {
      // An intrinsic sizing function: Use an initial base size of zero.
      DCHECK(track_size.HasIntrinsicMinTrackBreadth());
      current_set.SetBaseSize(LayoutUnit());
    }

    // Note that, since |NGGridSet| initializes its growth limit as indefinite,
    // an intrinsic or flexible sizing function needs no further resolution.
    if (track_size.HasFixedMaxTrackBreadth()) {
      DCHECK(!track_size.MaxTrackBreadth().HasPercentage() ||
             content_box_size != kIndefiniteSize);

      // A fixed sizing function: Resolve to an absolute length and use that
      // size as the track’s initial growth limit; if the growth limit is less
      // than the base size, increase the growth limit to match the base size.
      LayoutUnit fixed_max_breadth = MinimumValueForLength(
          track_size.MaxTrackBreadth().length(), content_box_size);
      current_set.SetGrowthLimit(
          std::max(current_set.BaseSize(),
                   fixed_max_breadth * current_set.TrackCount()));
    }
  }

  // 2. Resolve intrinsic track sizing functions to absolute lengths.
  ResolveIntrinsicTrackSizes(track_collection, grid_items,
                             reordered_item_indices);
}

// Helpers for the track sizing algorithm.
namespace {

using GridItemContributionType =
    NGGridLayoutAlgorithm::GridItemContributionType;

// Returns the corresponding size to be increased by accommodating a grid item's
// contribution; for intrinsic min track sizing functions, return the base size.
// For intrinsic max track sizing functions, return the growth limit.
static LayoutUnit AffectedSizeForContribution(
    const NGGridSet& set,
    GridItemContributionType contribution_type) {
  switch (contribution_type) {
    case GridItemContributionType::kForIntrinsicMinimums:
    case GridItemContributionType::kForContentBasedMinimums:
    case GridItemContributionType::kForMaxContentMinimums:
      return set.BaseSize();
    case GridItemContributionType::kForIntrinsicMaximums:
    case GridItemContributionType::kForMaxContentMaximums:
      LayoutUnit growth_limit = set.GrowthLimit();
      // For infinite growth limits, substitute with the track's base size.
      if (growth_limit == kIndefiniteSize)
        return set.BaseSize();
      return growth_limit;
  }
}

static void GrowAffectedSizeByPlannedIncrease(
    NGGridSet& set,
    GridItemContributionType contribution_type) {
  switch (contribution_type) {
    case GridItemContributionType::kForIntrinsicMinimums:
    case GridItemContributionType::kForContentBasedMinimums:
    case GridItemContributionType::kForMaxContentMinimums:
      set.SetBaseSize(set.BaseSize() + set.PlannedIncrease());
      break;
    case GridItemContributionType::kForIntrinsicMaximums:
    case GridItemContributionType::kForMaxContentMaximums:
      LayoutUnit growth_limit = set.GrowthLimit();
      // If the affected size to grow is an infinite growth limit, set it to the
      // track's base size plus the planned increase.
      if (growth_limit == kIndefiniteSize)
        set.SetGrowthLimit(set.BaseSize() + set.PlannedIncrease());
      else
        set.SetGrowthLimit(growth_limit + set.PlannedIncrease());
      break;
  }
}

// Returns true if a set should increase its used size according to the steps in
// https://drafts.csswg.org/css-grid-1/#algo-spanning-items; false otherwise.
static bool IsContributionAppliedToSet(
    const NGGridSet& set,
    GridItemContributionType contribution_type) {
  switch (contribution_type) {
    case GridItemContributionType::kForIntrinsicMinimums:
      return set.TrackSize().HasIntrinsicMinTrackBreadth();
    case GridItemContributionType::kForContentBasedMinimums:
      return set.TrackSize().HasMinOrMaxContentMinTrackBreadth();
    case GridItemContributionType::kForMaxContentMinimums:
      // TODO(ethavar): Check if the grid container is being sized under a
      // 'max-content' constraint to consider 'auto' min track sizing functions,
      // see https://drafts.csswg.org/css-grid-1/#track-size-max-content-min.
      return set.TrackSize().HasMaxContentMinTrackBreadth();
    case GridItemContributionType::kForIntrinsicMaximums:
      return set.TrackSize().HasIntrinsicMaxTrackBreadth();
    case GridItemContributionType::kForMaxContentMaximums:
      return set.TrackSize().HasMaxContentOrAutoMaxTrackBreadth();
  }
}

// https://drafts.csswg.org/css-grid-1/#extra-space
// Returns true if a set's used size should be consider to grow beyond its limit
// (see the "Distribute space beyond limits" section); otherwise, false.
// Note that we will deliberately return false in cases where we don't have a
// collection of tracks different than "all affected tracks".
static bool ShouldUsedSizeGrowBeyondLimit(
    const NGGridSet& set,
    GridItemContributionType contribution_type) {
  // This function assumes that we already determined that extra space
  // distribution will be applied to the specified set.
  DCHECK(IsContributionAppliedToSet(set, contribution_type));

  switch (contribution_type) {
    case GridItemContributionType::kForIntrinsicMinimums:
    case GridItemContributionType::kForContentBasedMinimums:
      return set.TrackSize().HasIntrinsicMaxTrackBreadth();
    case GridItemContributionType::kForMaxContentMinimums:
      return set.TrackSize().HasMaxContentMaxTrackBreadth();
    case GridItemContributionType::kForIntrinsicMaximums:
    case GridItemContributionType::kForMaxContentMaximums:
      return false;
  }
}

static bool IsDistributionForGrowthLimits(
    GridItemContributionType contribution_type) {
  switch (contribution_type) {
    case GridItemContributionType::kForIntrinsicMinimums:
    case GridItemContributionType::kForContentBasedMinimums:
    case GridItemContributionType::kForMaxContentMinimums:
      return false;
    case GridItemContributionType::kForIntrinsicMaximums:
    case GridItemContributionType::kForMaxContentMaximums:
      return true;
  }
}

enum class InfinitelyGrowableBehavior { kEnforce, kIgnore };

// We define growth potential = limit - affected size; for base sizes, the limit
// is its growth limit. For growth limits, the limit is infinity if it is marked
// as "infinitely growable", and equal to the growth limit otherwise.
static LayoutUnit GrowthPotentialForSet(
    const NGGridSet& set,
    GridItemContributionType contribution_type,
    InfinitelyGrowableBehavior infinitely_growable_behavior =
        InfinitelyGrowableBehavior::kEnforce) {
  switch (contribution_type) {
    case GridItemContributionType::kForIntrinsicMinimums:
    case GridItemContributionType::kForContentBasedMinimums:
    case GridItemContributionType::kForMaxContentMinimums: {
      LayoutUnit growth_limit = set.GrowthLimit();
      return (growth_limit == kIndefiniteSize) ? kIndefiniteSize
                                               : growth_limit - set.BaseSize();
    }
    case GridItemContributionType::kForIntrinsicMaximums:
    case GridItemContributionType::kForMaxContentMaximums: {
      if (infinitely_growable_behavior ==
              InfinitelyGrowableBehavior::kEnforce &&
          !set.IsInfinitelyGrowable()) {
        // If the affected size was a growth limit and the track is not marked
        // infinitely growable, then the item-incurred increase will be zero.
        return LayoutUnit();
      }

      LayoutUnit growth_limit = set.GrowthLimit();
      LayoutUnit fit_content_limit = set.FitContentLimit();
      DCHECK(growth_limit >= 0 || growth_limit == kIndefiniteSize);
      DCHECK(fit_content_limit >= 0 || fit_content_limit == kIndefiniteSize);

      // The max track sizing function of a 'fit-content' track is treated as
      // 'max-content' until it reaches the limit specified as the 'fit-content'
      // argument, after which it is treated as having a fixed sizing function
      // of that argument (with a growth potential of zero).
      if (fit_content_limit != kIndefiniteSize) {
        LayoutUnit growth_potential = (growth_limit != kIndefiniteSize)
                                          ? fit_content_limit - growth_limit
                                          : fit_content_limit;
        return growth_potential.ClampNegativeToZero();
      }
      // Otherwise, this set has infinite growth potential.
      return kIndefiniteSize;
    }
  }
}

}  // namespace

// Follow the definitions from https://drafts.csswg.org/css-grid-1/#extra-space;
// notice that this method replaces the notion of "tracks" with "sets".
void NGGridLayoutAlgorithm::DistributeExtraSpaceToSets(
    LayoutUnit extra_space,
    GridItemContributionType contribution_type,
    NGGridSetVector* sets_to_grow,
    NGGridSetVector* sets_to_grow_beyond_limit) {
  DCHECK(sets_to_grow && extra_space >= 0);
  if (!extra_space)
    return;

#if DCHECK_IS_ON()
  if (IsDistributionForGrowthLimits(contribution_type))
    DCHECK_EQ(sets_to_grow, sets_to_grow_beyond_limit);
#endif

  wtf_size_t total_track_count = 0;
  for (NGGridSet* set : *sets_to_grow) {
    set->SetItemIncurredIncrease(LayoutUnit());

    // From the first note in https://drafts.csswg.org/css-grid-1/#extra-space:
    //   - If the affected size was a growth limit and the track is not marked
    //   "infinitely growable", then each item-incurred increase will be zero.
    //
    // When distributing space to growth limits, we need to increase each track
    // up to its 'fit-content' limit. However, because of the note above, first
    // we should only grow tracks marked as "infinitely growable" up to limits
    // and then grow all affected tracks beyond limits.
    //
    // We can correctly resolve every scenario by doing a single sort of
    // |sets_to_grow|, purposely ignoring the "infinitely growable" flag, then
    // filtering out which sets count toward the total track count at each step;
    // for base sizes this is not required, but if there are no tracks with
    // growth potential > 0, we can optimize by not sorting the sets.
    LayoutUnit growth_potential =
        GrowthPotentialForSet(*set, contribution_type);
    DCHECK(growth_potential >= 0 || growth_potential == kIndefiniteSize);
    if (growth_potential)
      total_track_count += set->TrackCount();
  }

  // We will sort the tracks by growth potential in non-decreasing order to
  // distribute space up to limits; notice that if we start distributing space
  // equally among all tracks we will eventually reach the limit of a track or
  // run out of space to distribute. If the former scenario happens, it should
  // be easy to see that the group of tracks that will reach its limit first
  // will be that with the least growth potential. Otherwise, if tracks in such
  // group does not reach their limit, every upcoming track with greater growth
  // potential must be able to increase its size by the same amount.
  if (total_track_count || IsDistributionForGrowthLimits(contribution_type)) {
    auto CompareSetsByGrowthPotential = [contribution_type](NGGridSet* set_a,
                                                            NGGridSet* set_b) {
      LayoutUnit growth_potential_a = GrowthPotentialForSet(
          *set_a, contribution_type, InfinitelyGrowableBehavior::kIgnore);
      LayoutUnit growth_potential_b = GrowthPotentialForSet(
          *set_b, contribution_type, InfinitelyGrowableBehavior::kIgnore);

      if (growth_potential_a == kIndefiniteSize ||
          growth_potential_b == kIndefiniteSize) {
        // At this point we know that there is at least one set with infinite
        // growth potential; if |set_a| has a definite value, then |set_b| must
        // have infinite growth potential, and thus, |set_a| < |set_b|.
        return growth_potential_a != kIndefiniteSize;
      }
      // Straightforward comparison of definite growth potentials.
      return growth_potential_a < growth_potential_b;
    };
    std::sort(sets_to_grow->begin(), sets_to_grow->end(),
              CompareSetsByGrowthPotential);
  }

  auto ClampSize = [](LayoutUnit& size, LayoutUnit limit) {
    size = (limit != kIndefiniteSize) ? std::min(size, limit) : size;
  };

  // Distribute space up to limits:
  //   - For base sizes, grow the base size up to the growth limit.
  //   - For growth limits, the only case where a growth limit should grow at
  //   this step is when the set has already been marked "infinitely growable".
  //   Increase the growth limit up to the 'fit-content' argument (if any); note
  //   that these arguments could prevent this step to fulfill the entirety of
  //   the extra space and further distribution would be needed.
  if (total_track_count) {
    for (NGGridSet* set : *sets_to_grow) {
      LayoutUnit growth_potential =
          GrowthPotentialForSet(*set, contribution_type);

      if (growth_potential) {
        wtf_size_t set_track_count = set->TrackCount();
        LayoutUnit extra_space_share =
            (extra_space * set_track_count) / total_track_count;
        DCHECK_GE(extra_space_share, 0);

        ClampSize(extra_space_share, growth_potential);
        set->SetItemIncurredIncrease(extra_space_share);

        total_track_count -= set_track_count;
        extra_space -= extra_space_share;
        DCHECK_GE(total_track_count, 0u);
        DCHECK_GE(extra_space, 0);
      }
    }
  }

  // Distribute space beyond limits:
  //   - For base sizes, every affected track can grow indefinitely.
  //   - For growth limits, grow tracks up to their 'fit-content' argument.
  if (sets_to_grow_beyond_limit && extra_space) {
    total_track_count = 0;
    for (NGGridSet* set : *sets_to_grow_beyond_limit)
      total_track_count += set->TrackCount();

    for (NGGridSet* set : *sets_to_grow_beyond_limit) {
      wtf_size_t set_track_count = set->TrackCount();
      LayoutUnit extra_space_share =
          (extra_space * set_track_count) / total_track_count;
      DCHECK_GE(extra_space_share, 0);

      // Ignore the "infinitely growable" flag and grow all affected tracks.
      if (IsDistributionForGrowthLimits(contribution_type)) {
        LayoutUnit growth_potential = GrowthPotentialForSet(
            *set, contribution_type, InfinitelyGrowableBehavior::kIgnore);
        ClampSize(extra_space_share, growth_potential);
      }
      set->SetItemIncurredIncrease(set->ItemIncurredIncrease() +
                                   extra_space_share);

      total_track_count -= set_track_count;
      extra_space -= extra_space_share;
      DCHECK_GE(total_track_count, 0u);
      DCHECK_GE(extra_space, 0);
    }
  }

  // For each affected track, if the track's item-incurred increase is larger
  // than its planned increase, set the planned increase to that value.
  for (NGGridSet* set : *sets_to_grow) {
    set->SetPlannedIncrease(
        std::max(set->ItemIncurredIncrease(), set->PlannedIncrease()));
  }
}

void NGGridLayoutAlgorithm::IncreaseTrackSizesToAccommodateGridItems(
    ReorderedGridItems::Iterator group_begin,
    ReorderedGridItems::Iterator group_end,
    GridItemContributionType contribution_type,
    NGGridLayoutAlgorithmTrackCollection* track_collection) const {
  DCHECK(track_collection);
  const GridTrackSizingDirection track_direction =
      track_collection->Direction();

  for (auto set_iterator = track_collection->GetSetIterator();
       !set_iterator.IsAtEnd(); set_iterator.MoveToNextSet()) {
    set_iterator.CurrentSet().SetPlannedIncrease(LayoutUnit());
  }

  NGGridSetVector sets_to_grow;
  NGGridSetVector sets_to_grow_beyond_limit;
  for (auto grid_item = group_begin; grid_item != group_end; ++grid_item) {
    // We can skip this item if it doesn't span intrinsic tracks.
    if (!grid_item->IsSpanningIntrinsicTrack(track_direction))
      continue;

    sets_to_grow.Shrink(0);
    sets_to_grow_beyond_limit.Shrink(0);

    // TODO(ansollan): If the grid is auto-sized and has a calc or percent row
    // gap, then the gap can't be calculated on the first pass as we wouldn't
    // know our block size.
    LayoutUnit spanned_tracks_size =
        GridGap(track_direction) * (grid_item->SpanSize(track_direction) - 1);
    for (auto set_iterator =
             GetSetIteratorForItem(*grid_item, *track_collection);
         !set_iterator.IsAtEnd(); set_iterator.MoveToNextSet()) {
      NGGridSet& current_set = set_iterator.CurrentSet();

      spanned_tracks_size +=
          AffectedSizeForContribution(current_set, contribution_type);
      if (IsContributionAppliedToSet(current_set, contribution_type)) {
        sets_to_grow.push_back(&current_set);
        if (ShouldUsedSizeGrowBeyondLimit(current_set, contribution_type))
          sets_to_grow_beyond_limit.push_back(&current_set);
      }
    }

    if (sets_to_grow.IsEmpty())
      continue;

    // Subtract the corresponding size (base size or growth limit) of every
    // spanned track from the grid item's size contribution to find the item's
    // remaining size contribution. For infinite growth limits, substitute with
    // the track's base size. This is the space to distribute, floor it at zero.
    LayoutUnit extra_space = ContributionSizeForGridItem(
        *grid_item, track_direction, contribution_type);
    extra_space -= spanned_tracks_size;

    DistributeExtraSpaceToSets(
        extra_space.ClampNegativeToZero(), contribution_type, &sets_to_grow,
        sets_to_grow_beyond_limit.IsEmpty() ? &sets_to_grow
                                            : &sets_to_grow_beyond_limit);
  }

  for (auto set_iterator = track_collection->GetSetIterator();
       !set_iterator.IsAtEnd(); set_iterator.MoveToNextSet()) {
    GrowAffectedSizeByPlannedIncrease(set_iterator.CurrentSet(),
                                      contribution_type);
  }
}

// https://drafts.csswg.org/css-grid-1/#algo-content
void NGGridLayoutAlgorithm::ResolveIntrinsicTrackSizes(
    NGGridLayoutAlgorithmTrackCollection* track_collection,
    Vector<GridItemData>* grid_items,
    Vector<wtf_size_t>* reordered_item_indices) const {
  DCHECK(grid_items && track_collection && reordered_item_indices);
  const GridTrackSizingDirection track_direction =
      track_collection->Direction();

  // Reorder grid items to process them as follows:
  //   - First, consider items spanning a single non-flexible track.
  //   - Next, consider items with span size of 2 not spanning a flexible track.
  //   - Repeat incrementally for items with greater span sizes until all items
  //   not spanning a flexible track have been considered.
  //   - Finally, consider all items spanning a flexible track.
  auto CompareGridItemsForIntrinsicTrackResolution =
      [grid_items, track_direction](wtf_size_t index_a,
                                    wtf_size_t index_b) -> bool {
    if (grid_items->at(index_a).IsSpanningFlexibleTrack(track_direction) ||
        grid_items->at(index_b).IsSpanningFlexibleTrack(track_direction)) {
      // Ignore span sizes if one of the items spans a track with a flexible
      // sizing function; items not spanning such tracks should come first.
      return !grid_items->at(index_a).IsSpanningFlexibleTrack(track_direction);
    }
    return grid_items->at(index_a).SpanSize(track_direction) <
           grid_items->at(index_b).SpanSize(track_direction);
  };
  std::sort(reordered_item_indices->begin(), reordered_item_indices->end(),
            CompareGridItemsForIntrinsicTrackResolution);

  // First, process the items that don't span a flexible track.
  ReorderedGridItems reordered_items =
      ReorderedGridItems(*reordered_item_indices, *grid_items);
  ReorderedGridItems::Iterator current_group_begin = reordered_items.begin();

  while (current_group_begin != reordered_items.end() &&
         !current_group_begin->IsSpanningFlexibleTrack(track_direction)) {
    // Each iteration considers all items with the same span size.
    wtf_size_t current_group_span_size =
        current_group_begin->SpanSize(track_direction);
    ReorderedGridItems::Iterator current_group_end = current_group_begin;
    do {
      DCHECK(!current_group_end->IsSpanningFlexibleTrack(track_direction));
      ++current_group_end;
    } while (current_group_end != reordered_items.end() &&
             !current_group_end->IsSpanningFlexibleTrack(track_direction) &&
             current_group_end->SpanSize(track_direction) ==
                 current_group_span_size);

    IncreaseTrackSizesToAccommodateGridItems(
        current_group_begin, current_group_end,
        GridItemContributionType::kForIntrinsicMinimums, track_collection);

    // TODO(ethavar): Add remaining stages, mark infinitely growable sets...
    current_group_begin = current_group_end;
  }

  // TODO(ethavar): drafts.csswg.org/css-grid-1/#algo-spanning-flex-items
  // Repeat the previous step instead considering (together, rather than grouped
  // by span) all items that do span a track with a flexible sizing function.
}

void NGGridLayoutAlgorithm::PlaceItems(
    const Vector<GridItemData>& grid_items,
    const NGGridLayoutAlgorithmTrackCollection& column_track_collection,
    const NGGridLayoutAlgorithmTrackCollection& row_track_collection,
    Vector<GridItemData>* out_of_flow_items,
    LayoutUnit* intrinsic_block_size,
    LayoutUnit* block_size) {
  DCHECK(intrinsic_block_size);
  DCHECK(block_size);
  LayoutUnit column_grid_gap =
      GridGap(kForColumns, ChildAvailableSize().inline_size);
  LayoutUnit row_grid_gap = GridGap(kForRows, ChildAvailableSize().block_size);
  Vector<LayoutUnit> column_set_offsets =
      ComputeSetOffsets(column_track_collection, column_grid_gap);
  Vector<LayoutUnit> row_set_offsets =
      ComputeSetOffsets(row_track_collection, row_grid_gap);

  // Intrinsic block size is based on the final row offset.
  // Because gaps are included in row offsets, subtract out the final gap.
  *intrinsic_block_size =
      row_set_offsets.back() -
      (row_set_offsets.size() == 1 ? LayoutUnit() : row_grid_gap) +
      BorderScrollbarPadding().block_end;

  *intrinsic_block_size =
      ClampIntrinsicBlockSize(ConstraintSpace(), Node(),
                              BorderScrollbarPadding(), *intrinsic_block_size);

  *block_size = ComputeBlockSizeForFragment(
      ConstraintSpace(), Style(), BorderPadding(), *intrinsic_block_size,
      border_box_size_.inline_size);

  // If the row gap is percent or calc, it should be computed now that the
  // intrinsic size is known. However, the gap should not be added to the
  // intrinsic block size.
  const bool is_row_gap_unresolvable =
      Style().RowGap() && Style().RowGap()->IsPercentOrCalc() &&
      ChildAvailableSize().block_size == kIndefiniteSize;
  if (is_row_gap_unresolvable) {
    const LayoutUnit resolved_available_block_size =
        (*block_size - BorderScrollbarPadding().BlockSum())
            .ClampNegativeToZero();

    row_grid_gap = GridGap(kForRows, resolved_available_block_size);
    row_set_offsets = ComputeSetOffsets(row_track_collection, row_grid_gap);
  }

  PlaceGridItems(grid_items, column_set_offsets, row_set_offsets, *block_size,
                 column_grid_gap, row_grid_gap);

  PlaceOutOfFlowItems(column_set_offsets, row_set_offsets,
                      column_track_collection, row_track_collection,
                      *block_size, column_grid_gap, row_grid_gap,
                      out_of_flow_items);
}

LayoutUnit NGGridLayoutAlgorithm::GridGap(
    GridTrackSizingDirection track_direction,
    LayoutUnit available_size) const {
  const base::Optional<Length>& gap =
      track_direction == kForColumns ? Style().ColumnGap() : Style().RowGap();

  if (!gap)
    return LayoutUnit();

  // TODO(ansollan): Update behavior based on outcome of working group
  // discussions. See https://github.com/w3c/csswg-drafts/issues/5566.
  if (available_size == kIndefiniteSize)
    available_size = LayoutUnit();

  return MinimumValueForLength(*gap, available_size);
}

Vector<LayoutUnit> NGGridLayoutAlgorithm::ComputeSetOffsets(
    const NGGridLayoutAlgorithmTrackCollection& track_collection,
    LayoutUnit grid_gap) const {
  LayoutUnit set_offset = track_collection.IsForColumns()
                              ? BorderScrollbarPadding().inline_start
                              : BorderScrollbarPadding().block_start;
  Vector<LayoutUnit> set_offsets = {set_offset};
  set_offsets.ReserveCapacity(track_collection.SetCount() + 1);
  for (auto set_iterator = track_collection.GetSetIterator();
       !set_iterator.IsAtEnd(); set_iterator.MoveToNextSet()) {
    const auto& set = set_iterator.CurrentSet();
    set_offset += set.BaseSize() + set.TrackCount() * grid_gap;
    set_offsets.push_back(set_offset);
  }
  return set_offsets;
}

namespace {

// Returns the alignment offset for either the inline or block direction.
LayoutUnit AlignmentOffset(LayoutUnit container_size,
                           LayoutUnit size,
                           LayoutUnit margin_start,
                           LayoutUnit margin_end,
                           AxisEdge axis_edge) {
  switch (axis_edge) {
    case AxisEdge::kStart:
      return margin_start;
    case AxisEdge::kCenter:
      return (container_size - size + margin_start - margin_end) / 2;
    case AxisEdge::kEnd:
      return container_size - margin_end - size;
    case AxisEdge::kBaseline:
      // TODO(ikilpatrick): Implement baseline alignment.
      return margin_start;
  }
  NOTREACHED();
  return LayoutUnit();
}

void AlignmentOffsetForOutOfFlow(
    const AxisEdge inline_axis_edge,
    const AxisEdge block_axis_edge,
    const LogicalSize container_size,
    NGLogicalStaticPosition::InlineEdge* inline_edge,
    NGLogicalStaticPosition::BlockEdge* block_edge,
    LogicalOffset* offset) {
  using InlineEdge = NGLogicalStaticPosition::InlineEdge;
  using BlockEdge = NGLogicalStaticPosition::BlockEdge;

  switch (inline_axis_edge) {
    case AxisEdge::kStart:
      *inline_edge = InlineEdge::kInlineStart;
      break;
    case AxisEdge::kCenter:
      *inline_edge = InlineEdge::kInlineCenter;
      offset->inline_offset += container_size.inline_size / 2;
      break;
    default:
      *inline_edge = InlineEdge::kInlineEnd;
      offset->inline_offset += container_size.inline_size;
      break;
  }

  switch (block_axis_edge) {
    case AxisEdge::kStart:
      *block_edge = BlockEdge::kBlockStart;
      break;
    case AxisEdge::kCenter:
      *block_edge = BlockEdge::kBlockCenter;
      offset->block_offset += container_size.block_size / 2;
      break;
    default:
      *block_edge = BlockEdge::kBlockEnd;
      offset->block_offset += container_size.block_size;
      break;
  }
}
}  // namespace

void NGGridLayoutAlgorithm::PlaceGridItems(
    const Vector<GridItemData>& grid_items,
    const Vector<LayoutUnit>& column_set_offsets,
    const Vector<LayoutUnit>& row_set_offsets,
    LayoutUnit block_size,
    LayoutUnit column_grid_gap,
    LayoutUnit row_grid_gap) {
  for (const GridItemData& grid_item : grid_items) {
    LogicalOffset offset;
    LogicalSize size;
    ComputeOffsetAndSize(grid_item, column_set_offsets, column_grid_gap,
                         &offset.inline_offset, &size.inline_size);
    ComputeOffsetAndSize(grid_item, row_set_offsets, row_grid_gap,
                         &offset.block_offset, &size.block_size, kForRows,
                         block_size);
    const auto& item_style = grid_item.node.Style();
    NGConstraintSpaceBuilder builder(ConstraintSpace(),
                                     item_style.GetWritingDirection(),
                                     /* is_new_fc */ true);
    SetOrthogonalFallbackInlineSizeIfNeeded(Style(), grid_item.node, &builder);
    builder.SetIsPaintedAtomically(true);
    builder.SetAvailableSize(size);
    builder.SetPercentageResolutionSize(size);

    builder.SetStretchInlineSizeIfAuto(grid_item.is_inline_axis_stretched);
    builder.SetStretchBlockSizeIfAuto(grid_item.is_block_axis_stretched);

    scoped_refptr<const NGLayoutResult> result =
        grid_item.node.Layout(builder.ToConstraintSpace());
    const auto& physical_fragment = result->PhysicalFragment();

    // Apply the grid-item's alignment (if any).
    NGFragment fragment(ConstraintSpace().GetWritingDirection(),
                        physical_fragment);
    offset +=
        LogicalOffset(AlignmentOffset(size.inline_size, fragment.InlineSize(),
                                      grid_item.margins.inline_start,
                                      grid_item.margins.inline_end,
                                      grid_item.inline_axis_alignment),
                      AlignmentOffset(size.block_size, fragment.BlockSize(),
                                      grid_item.margins.block_start,
                                      grid_item.margins.block_end,
                                      grid_item.block_axis_alignment));

    container_builder_.AddChild(physical_fragment, offset);
  }
}

void NGGridLayoutAlgorithm::PlaceOutOfFlowItems(
    const Vector<LayoutUnit>& column_set_offsets,
    const Vector<LayoutUnit>& row_set_offsets,
    const NGGridLayoutAlgorithmTrackCollection& column_track_collection,
    const NGGridLayoutAlgorithmTrackCollection& row_track_collection,
    LayoutUnit block_size,
    LayoutUnit column_grid_gap,
    LayoutUnit row_grid_gap,
    Vector<GridItemData>* out_of_flow_items) {
  // Cache set indices for out of flow items.
  CacheItemSetIndices(column_track_collection, out_of_flow_items);
  CacheItemSetIndices(row_track_collection, out_of_flow_items);

  for (const GridItemData& out_of_flow_item : *out_of_flow_items) {
    LogicalRect containing_block_rect;
    ComputeOffsetAndSize(out_of_flow_item, column_set_offsets, column_grid_gap,
                         &containing_block_rect.offset.inline_offset,
                         &containing_block_rect.size.inline_size);
    ComputeOffsetAndSize(out_of_flow_item, row_set_offsets, row_grid_gap,
                         &containing_block_rect.offset.block_offset,
                         &containing_block_rect.size.block_size, kForRows,
                         block_size);
    NGLogicalStaticPosition::InlineEdge inline_edge;
    NGLogicalStaticPosition::BlockEdge block_edge;
    LogicalOffset child_offset = containing_block_rect.offset;
    AlignmentOffsetForOutOfFlow(out_of_flow_item.inline_axis_alignment,
                                out_of_flow_item.block_axis_alignment,
                                containing_block_rect.size, &inline_edge,
                                &block_edge, &child_offset);

    container_builder_.AddOutOfFlowChildCandidate(
        out_of_flow_item.node, child_offset, inline_edge, block_edge,
        /* needs_block_offset_adjustment */ false, containing_block_rect);
  }
}

void NGGridLayoutAlgorithm::ComputeOffsetAndSize(
    const GridItemData& item,
    const Vector<LayoutUnit>& set_offsets,
    LayoutUnit grid_gap,
    LayoutUnit* start_offset,
    LayoutUnit* size,
    GridTrackSizingDirection track_direction,
    LayoutUnit block_size) const {
  wtf_size_t start_index, end_index;
  LayoutUnit border;
  // The default padding box value of the |size| will only be used in out of
  // flow items in which both the start line and end line are defined as 'auto'.
  if (track_direction == kForColumns) {
    start_index = item.columns_begin_set_index;
    end_index = item.columns_end_set_index;
    border = container_builder_.Borders().inline_start;
    *size =
        border_box_size_.inline_size - container_builder_.Borders().InlineSum();
  } else {
    start_index = item.rows_begin_set_index;
    end_index = item.rows_end_set_index;
    border = container_builder_.Borders().block_start;
    *size = border_box_size_.block_size == kIndefiniteSize
                ? block_size
                : border_box_size_.block_size;
    *size -= container_builder_.Borders().BlockSum();
  }
  *start_offset = border;
  LayoutUnit end_offset = border;
  // If the start line is defined, the size is calculated by subtracting the
  // offset at start index. Additionally, the start border is removed from the
  // cumulated offset because it was already accounted for in the previous value
  // of the size.
  if (start_index != kNotFound) {
    *start_offset = set_offsets[start_index];
    *size -= (*start_offset - end_offset);
  }
  // If the end line is defined, the offset (which can be the offset at the
  // start index or the start border) and the added grid gap after the spanned
  // tracks are subtracted from the offset at the end index.
  if (end_index != kNotFound) {
    end_offset = set_offsets[end_index];
    *size = end_offset - *start_offset - grid_gap;
  }
  if (start_index != kNotFound && end_index != kNotFound) {
    DCHECK_LT(start_index, end_index);
    DCHECK_LT(end_index, set_offsets.size());
    DCHECK_GE(*size, 0);
  } else {
    // Only out of flow items can have an undefined ('auto') value for the start
    // and/or end indices.
    DCHECK_EQ(item.item_type, ItemType::kOutOfFlow);
  }
}
}  // namespace blink
