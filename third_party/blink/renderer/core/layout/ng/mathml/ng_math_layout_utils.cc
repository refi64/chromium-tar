// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/ng/mathml/ng_math_layout_utils.h"

#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/ng/ng_block_node.h"
#include "third_party/blink/renderer/core/layout/ng/ng_constraint_space_builder.h"
#include "third_party/blink/renderer/core/layout/ng/ng_length_utils.h"
#include "third_party/blink/renderer/core/layout/ng/ng_space_utils.h"
#include "third_party/blink/renderer/core/mathml/mathml_fraction_element.h"
#include "third_party/blink/renderer/core/mathml/mathml_radical_element.h"
#include "third_party/blink/renderer/core/mathml/mathml_scripts_element.h"

namespace blink {

NGConstraintSpace CreateConstraintSpaceForMathChild(
    const NGBlockNode& parent_node,
    const LogicalSize& child_available_size,
    const NGConstraintSpace& parent_constraint_space,
    const NGLayoutInputNode& child) {
  const ComputedStyle& parent_style = parent_node.Style();
  const ComputedStyle& child_style = child.Style();
  DCHECK(child.CreatesNewFormattingContext());
  NGConstraintSpaceBuilder space_builder(parent_constraint_space,
                                         child_style.GetWritingMode(),
                                         true /* is_new_fc */);
  SetOrthogonalFallbackInlineSizeIfNeeded(parent_style, child, &space_builder);

  space_builder.SetAvailableSize(child_available_size);
  space_builder.SetPercentageResolutionSize(child_available_size);
  space_builder.SetReplacedPercentageResolutionSize(child_available_size);

  space_builder.SetIsShrinkToFit(child_style.LogicalWidth().IsAuto());

  // TODO(rbuis): add target stretch sizes.

  space_builder.SetTextDirection(child_style.Direction());

  // TODO(rbuis): add ink baselines?
  space_builder.SetNeedsBaseline(true);
  return space_builder.ToConstraintSpace();
}

NGLayoutInputNode FirstChildInFlow(const NGBlockNode& node) {
  NGLayoutInputNode child = node.FirstChild();
  while (child && child.IsOutOfFlowPositioned())
    child = child.NextSibling();
  return child;
}

NGLayoutInputNode NextSiblingInFlow(const NGBlockNode& node) {
  NGLayoutInputNode sibling = node.NextSibling();
  while (sibling && sibling.IsOutOfFlowPositioned())
    sibling = sibling.NextSibling();
  return sibling;
}

inline bool InFlowChildCountIs(const NGBlockNode& node, unsigned count) {
  DCHECK(count == 2 || count == 3);
  auto child = To<NGBlockNode>(FirstChildInFlow(node));
  while (count && child) {
    child = To<NGBlockNode>(NextSiblingInFlow(child));
    count--;
  }
  return !count && !child;
}

bool IsValidMathMLFraction(const NGBlockNode& node) {
  return InFlowChildCountIs(node, 2);
}

static bool IsPrescriptDelimiter(const NGBlockNode& block_node) {
  auto* node = block_node.GetLayoutBox()->GetNode();
  return node && IsA<MathMLElement>(node) &&
         node->HasTagName(mathml_names::kMprescriptsTag);
}

// Valid according to:
// https://mathml-refresh.github.io/mathml-core/#prescripts-and-tensor-indices-mmultiscripts
inline bool IsValidMultiscript(const NGBlockNode& node) {
  auto child = To<NGBlockNode>(FirstChildInFlow(node));
  if (!child || IsPrescriptDelimiter(child))
    return false;
  bool number_of_scripts_is_even = true;
  while (child) {
    child = To<NGBlockNode>(NextSiblingInFlow(child));
    if (!child)
      continue;
    if (IsPrescriptDelimiter(child)) {
      if (!number_of_scripts_is_even)
        return false;
      continue;
    }
    number_of_scripts_is_even = !number_of_scripts_is_even;
  }
  return number_of_scripts_is_even;
}

bool IsValidMathMLScript(const NGBlockNode& node) {
  switch (node.ScriptType()) {
    case MathScriptType::kUnder:
    case MathScriptType::kOver:
    case MathScriptType::kSub:
    case MathScriptType::kSuper:
      return InFlowChildCountIs(node, 2);
    case MathScriptType::kSubSup:
    case MathScriptType::kUnderOver:
      return InFlowChildCountIs(node, 3);
    case MathScriptType::kMultiscripts:
      return IsValidMultiscript(node);
    default:
      return false;
  }
}

bool IsValidMathMLRadical(const NGBlockNode& node) {
  auto* radical =
      DynamicTo<MathMLRadicalElement>(node.GetLayoutBox()->GetNode());
  return !radical->HasIndex() || InFlowChildCountIs(node, 2);
}

RadicalHorizontalParameters GetRadicalHorizontalParameters(
    const ComputedStyle& style) {
  RadicalHorizontalParameters parameters;
  parameters.kern_before_degree = LayoutUnit(
      MathConstant(style,
                   OpenTypeMathSupport::MathConstants::kRadicalKernBeforeDegree)
          .value_or(5 * style.FontSize() / 18));
  parameters.kern_after_degree = LayoutUnit(
      MathConstant(style,
                   OpenTypeMathSupport::MathConstants::kRadicalKernAfterDegree)
          .value_or(-10 * style.FontSize() / 18));
  return parameters;
}

RadicalVerticalParameters GetRadicalVerticalParameters(
    const ComputedStyle& style,
    bool has_index) {
  RadicalVerticalParameters parameters;
  bool has_display = HasDisplayStyle(style);
  float rule_thickness = RuleThicknessFallback(style);
  float x_height = style.GetFont().PrimaryFont()->GetFontMetrics().XHeight();
  parameters.rule_thickness = LayoutUnit(
      MathConstant(style,
                   OpenTypeMathSupport::MathConstants::kRadicalRuleThickness)
          .value_or(rule_thickness));
  parameters.vertical_gap = LayoutUnit(
      MathConstant(
          style, has_display
                     ? OpenTypeMathSupport::MathConstants::
                           kRadicalDisplayStyleVerticalGap
                     : OpenTypeMathSupport::MathConstants::kRadicalVerticalGap)
          .value_or(has_display ? rule_thickness + x_height / 4
                                : 5 * rule_thickness / 4));
  parameters.extra_ascender = LayoutUnit(
      MathConstant(style,
                   OpenTypeMathSupport::MathConstants::kRadicalExtraAscender)
          .value_or(parameters.rule_thickness));
  if (has_index) {
    parameters.degree_bottom_raise_percent =
        MathConstant(style, OpenTypeMathSupport::MathConstants::
                                kRadicalDegreeBottomRaisePercent)
            .value_or(.6);
  }
  return parameters;
}

MinMaxSizes GetMinMaxSizesForVerticalStretchyOperator(
    const ComputedStyle& style,
    UChar character) {
  // https://mathml-refresh.github.io/mathml-core/#dfn-preferred-inline-size-of-a-glyph-stretched-along-the-block-axis
  const SimpleFontData* primary_font = style.GetFont().PrimaryFont();
  const HarfBuzzFace* harfbuzz_face =
      primary_font->PlatformData().GetHarfBuzzFace();

  MinMaxSizes sizes;

  if (auto base_glyph = primary_font->GlyphForCharacter(character)) {
    sizes.Encompass(LayoutUnit(primary_font->WidthForGlyph(base_glyph)));

    for (auto& variant : OpenTypeMathSupport::GetGlyphVariantRecords(
             harfbuzz_face, base_glyph, OpenTypeMathStretchData::Vertical)) {
      sizes.Encompass(LayoutUnit(primary_font->WidthForGlyph(variant)));
    }

    for (auto& part : OpenTypeMathSupport::GetGlyphPartRecords(
             harfbuzz_face, base_glyph,
             OpenTypeMathStretchData::StretchAxis::Vertical)) {
      sizes.Encompass(LayoutUnit(primary_font->WidthForGlyph(part.glyph)));
    }
  }

  return sizes;
}

namespace {

inline LayoutUnit DefaultFractionLineThickness(const ComputedStyle& style) {
  return LayoutUnit(
      MathConstant(style,
                   OpenTypeMathSupport::MathConstants::kFractionRuleThickness)
          .value_or(RuleThicknessFallback(style)));
}

}  // namespace

LayoutUnit MathAxisHeight(const ComputedStyle& style) {
  return LayoutUnit(
      MathConstant(style, OpenTypeMathSupport::MathConstants::kAxisHeight)
          .value_or(style.GetFont().PrimaryFont()->GetFontMetrics().XHeight() /
                    2));
}

LayoutUnit FractionLineThickness(const ComputedStyle& style) {
  return std::max<LayoutUnit>(
      ValueForLength(style.GetMathFractionBarThickness(),
                     DefaultFractionLineThickness(style)),
      LayoutUnit());
}

}  // namespace blink
