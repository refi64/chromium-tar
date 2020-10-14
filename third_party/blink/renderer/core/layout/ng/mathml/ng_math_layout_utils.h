// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_MATHML_NG_MATH_LAYOUT_UTILS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_MATHML_NG_MATH_LAYOUT_UTILS_H_

#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/platform/fonts/opentype/open_type_math_support.h"

namespace blink {

struct LogicalSize;
struct MinMaxSizes;
class NGBlockNode;
class NGConstraintSpace;
class NGLayoutInputNode;

// Creates a new constraint space for the current child.
NGConstraintSpace CreateConstraintSpaceForMathChild(
    const NGBlockNode& parent_node,
    const LogicalSize& child_available_size,
    const NGConstraintSpace& parent_constraint_space,
    const NGLayoutInputNode&);

NGLayoutInputNode FirstChildInFlow(const NGBlockNode&);
NGLayoutInputNode NextSiblingInFlow(const NGBlockNode&);

bool IsValidMathMLFraction(const NGBlockNode&);
bool IsValidMathMLScript(const NGBlockNode&);
bool IsValidMathMLRadical(const NGBlockNode&);

inline float RuleThicknessFallback(const ComputedStyle& style) {
  // This function returns a value for the default rule thickness (TeX's
  // \xi_8) to be used as a fallback when we lack a MATH table.
  return 0.05f * style.FontSize();
}

LayoutUnit MathAxisHeight(const ComputedStyle& style);

inline base::Optional<float> MathConstant(
    const ComputedStyle& style,
    OpenTypeMathSupport::MathConstants constant) {
  return OpenTypeMathSupport::MathConstant(
      style.GetFont().PrimaryFont()->PlatformData().GetHarfBuzzFace(),
      constant);
}

LayoutUnit FractionLineThickness(const ComputedStyle&);

inline bool HasDisplayStyle(const ComputedStyle& style) {
  return style.MathStyle() == EMathStyle::kDisplay;
}

// Get parameters for horizontal positioning of mroot.
// The parameters are defined here:
// https://mathml-refresh.github.io/mathml-core/#layout-constants-mathconstants
struct RadicalHorizontalParameters {
  LayoutUnit kern_before_degree;
  LayoutUnit kern_after_degree;
};
RadicalHorizontalParameters GetRadicalHorizontalParameters(
    const ComputedStyle&);

// Get parameters for vertical positioning of msqrt/mroot.
// The parameters are defined here:
// https://mathml-refresh.github.io/mathml-core/#layout-constants-mathconstants
struct RadicalVerticalParameters {
  LayoutUnit vertical_gap;
  LayoutUnit rule_thickness;
  LayoutUnit extra_ascender;
  float degree_bottom_raise_percent;
};
RadicalVerticalParameters GetRadicalVerticalParameters(const ComputedStyle&,
                                                       bool has_index);

// https://mathml-refresh.github.io/mathml-core/#dfn-preferred-inline-size-of-a-glyph-stretched-along-the-block-axis
MinMaxSizes GetMinMaxSizesForVerticalStretchyOperator(const ComputedStyle&,
                                                      UChar character);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_MATHML_NG_MATH_LAYOUT_UTILS_H_
