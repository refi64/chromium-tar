// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/accessibility/accessibility_tree_formatter_utils_mac.h"

#include "base/strings/sys_string_conversions.h"

// error: 'accessibilityAttributeNames' is deprecated: first deprecated in
// macOS 10.10 - Use the NSAccessibility protocol methods instead (see
// NSAccessibilityProtocols.h
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

using base::SysNSStringToUTF8;

namespace content {
namespace a11y {

namespace {

#define INT_FAIL(property_node, msg)                              \
  LOG(ERROR) << "Failed to parse " << property_node.name_or_value \
             << " to Int: " << msg;                               \
  return nil;

#define INTARRAY_FAIL(property_node, msg)                         \
  LOG(ERROR) << "Failed to parse " << property_node.name_or_value \
             << " to IntArray: " << msg;                          \
  return nil;

#define NSRANGE_FAIL(property_node, msg)                          \
  LOG(ERROR) << "Failed to parse " << property_node.name_or_value \
             << " to NSRange: " << msg;                           \
  return nil;

#define UIELEMENT_FAIL(property_node, msg)                        \
  LOG(ERROR) << "Failed to parse " << property_node.name_or_value \
             << " to UIElement: " << msg;                         \
  return nil;

#define TEXTMARKER_FAIL(property_node, msg)                                    \
  LOG(ERROR) << "Failed to parse " << property_node.name_or_value              \
             << " to AXTextMarker: " << msg                                    \
             << ". Expected format: {anchor, offset, affinity}, where anchor " \
                "is :line_num, offset is integer, affinity is either down, "   \
                "up or none";                                                  \
  return nil;

}  // namespace

LineIndexesMap::LineIndexesMap(const BrowserAccessibilityCocoa* cocoa_node) {
  int counter = 0;
  Build(cocoa_node, &counter);
}

LineIndexesMap::~LineIndexesMap() {}

std::string LineIndexesMap::IndexBy(
    const BrowserAccessibilityCocoa* cocoa_node) const {
  std::string line_index = ":unknown";
  if (map.find(cocoa_node) != map.end()) {
    line_index = map.at(cocoa_node);
  }
  return line_index;
}

gfx::NativeViewAccessible LineIndexesMap::NodeBy(
    const std::string& line_index) const {
  for (std::pair<const gfx::NativeViewAccessible, std::string> item : map) {
    if (item.second == line_index) {
      return item.first;
    }
  }
  return nil;
}

void LineIndexesMap::Build(const BrowserAccessibilityCocoa* cocoa_node,
                           int* counter) {
  const std::string line_index =
      std::string(1, ':') + base::NumberToString(++(*counter));
  map.insert({cocoa_node, line_index});
  for (BrowserAccessibilityCocoa* cocoa_child in [cocoa_node children]) {
    Build(cocoa_child, counter);
  }
}

// AttributeInvoker

AttributeInvoker::AttributeInvoker(const BrowserAccessibilityCocoa* cocoa_node,
                                   const LineIndexesMap& line_indexes_map)
    : cocoa_node(cocoa_node), line_indexes_map(line_indexes_map) {
  attributes = [cocoa_node accessibilityAttributeNames];
  parameterized_attributes =
      [cocoa_node accessibilityParameterizedAttributeNames];
}

OptionalNSObject AttributeInvoker::Invoke(
    const PropertyNode& property_node) const {
  // Attributes
  for (NSString* attribute : attributes) {
    if (property_node.IsMatching(SysNSStringToUTF8(attribute))) {
      return OptionalNSObject::NotNullOrNotApplicable(
          [cocoa_node accessibilityAttributeValue:attribute]);
    }
  }

  // Parameterized attributes
  for (NSString* attribute : parameterized_attributes) {
    if (property_node.IsMatching(SysNSStringToUTF8(attribute))) {
      OptionalNSObject param = ParamByPropertyNode(property_node);
      if (param.IsNotNil()) {
        return OptionalNSObject([cocoa_node
            accessibilityAttributeValue:attribute
                           forParameter:*param]);
      }
      return param;
    }
  }

  return OptionalNSObject::NotApplicable();
}

OptionalNSObject AttributeInvoker::ParamByPropertyNode(
    const PropertyNode& property_node) const {
  // NSAccessibility attributes always take a single parameter.
  if (property_node.parameters.size() != 1) {
    LOG(ERROR) << "Failed to parse " << property_node.original_property
               << ": single parameter is expected";
    return OptionalNSObject::Error();
  }

  // Nested attribute case: attempt to invoke an attribute for an argument node.
  const PropertyNode& arg_node = property_node.parameters[0];
  OptionalNSObject subvalue = Invoke(arg_node);
  if (!subvalue.IsNotApplicable()) {
    return subvalue;
  }

  // Otherwise parse argument node value.
  const std::string& property_name = property_node.name_or_value;
  if (property_name == "AXLineForIndex" ||
      property_name == "AXTextMarkerForIndex") {  // Int
    return OptionalNSObject::NotNilOrError(PropertyNodeToInt(arg_node));
  }
  if (property_name == "AXCellForColumnAndRow") {  // IntArray
    return OptionalNSObject::NotNilOrError(PropertyNodeToIntArray(arg_node));
  }
  if (property_name == "AXStringForRange") {  // NSRange
    return OptionalNSObject::NotNilOrError(PropertyNodeToRange(arg_node));
  }
  if (property_name == "AXIndexForChildUIElement") {  // UIElement
    return OptionalNSObject::NotNilOrError(PropertyNodeToUIElement(arg_node));
  }
  if (property_name == "AXIndexForTextMarker") {  // TextMarker
    return OptionalNSObject::NotNilOrError(PropertyNodeToTextMarker(arg_node));
  }
  if (property_name == "AXStringForTextMarkerRange") {  // TextMarkerRange
    return OptionalNSObject::NotNilOrError(
        PropertyNodeToTextMarkerRange(arg_node));
  }

  return OptionalNSObject::NotApplicable();
}

// NSNumber. Format: integer.
NSNumber* AttributeInvoker::PropertyNodeToInt(
    const PropertyNode& intnode) const {
  base::Optional<int> param = intnode.AsInt();
  if (!param) {
    INT_FAIL(intnode, "not a number")
  }
  return [NSNumber numberWithInt:*param];
}

// NSArray of two NSNumber. Format: [integer, integer].
NSArray* AttributeInvoker::PropertyNodeToIntArray(
    const PropertyNode& arraynode) const {
  if (arraynode.name_or_value != "[]") {
    INTARRAY_FAIL(arraynode, "not array")
  }

  NSMutableArray* array =
      [[NSMutableArray alloc] initWithCapacity:arraynode.parameters.size()];
  for (const auto& paramnode : arraynode.parameters) {
    base::Optional<int> param = paramnode.AsInt();
    if (!param) {
      INTARRAY_FAIL(arraynode, paramnode.name_or_value + " is not a number")
    }
    [array addObject:@(*param)];
  }
  return array;
}

// NSRange. Format: {loc: integer, len: integer}.
NSValue* AttributeInvoker::PropertyNodeToRange(
    const PropertyNode& dictnode) const {
  if (!dictnode.IsDict()) {
    NSRANGE_FAIL(dictnode, "dictionary is expected")
  }

  base::Optional<int> loc = dictnode.FindIntKey("loc");
  if (!loc) {
    NSRANGE_FAIL(dictnode, "no loc or loc is not a number")
  }

  base::Optional<int> len = dictnode.FindIntKey("len");
  if (!len) {
    NSRANGE_FAIL(dictnode, "no len or len is not a number")
  }

  return [NSValue valueWithRange:NSMakeRange(*loc, *len)];
}

// UIElement. Format: :line_num.
gfx::NativeViewAccessible AttributeInvoker::PropertyNodeToUIElement(
    const PropertyNode& uielement_node) const {
  gfx::NativeViewAccessible uielement =
      line_indexes_map.NodeBy(uielement_node.name_or_value);
  if (!uielement) {
    UIELEMENT_FAIL(uielement_node,
                   "no corresponding UIElement was found in the tree")
  }
  return uielement;
}

id AttributeInvoker::DictNodeToTextMarker(const PropertyNode& dictnode) const {
  if (!dictnode.IsDict()) {
    TEXTMARKER_FAIL(dictnode, "dictionary is expected")
  }
  if (dictnode.parameters.size() != 3) {
    TEXTMARKER_FAIL(dictnode, "wrong number of dictionary elements")
  }

  BrowserAccessibilityCocoa* anchor_cocoa =
      line_indexes_map.NodeBy(dictnode.parameters[0].name_or_value);
  if (!anchor_cocoa) {
    TEXTMARKER_FAIL(dictnode, "1st argument: wrong anchor")
  }

  base::Optional<int> offset = dictnode.parameters[1].AsInt();
  if (!offset) {
    TEXTMARKER_FAIL(dictnode, "2nd argument: wrong offset")
  }

  ax::mojom::TextAffinity affinity;
  const std::string& affinity_str = dictnode.parameters[2].name_or_value;
  if (affinity_str == "none") {
    affinity = ax::mojom::TextAffinity::kNone;
  } else if (affinity_str == "down") {
    affinity = ax::mojom::TextAffinity::kDownstream;
  } else if (affinity_str == "up") {
    affinity = ax::mojom::TextAffinity::kUpstream;
  } else {
    TEXTMARKER_FAIL(dictnode, "3rd argument: wrong affinity")
  }

  return content::AXTextMarkerFrom(anchor_cocoa, *offset, affinity);
}

id AttributeInvoker::PropertyNodeToTextMarker(
    const PropertyNode& dictnode) const {
  return DictNodeToTextMarker(dictnode);
}

id AttributeInvoker::PropertyNodeToTextMarkerRange(
    const PropertyNode& rangenode) const {
  if (!rangenode.IsDict()) {
    TEXTMARKER_FAIL(rangenode, "dictionary is expected")
  }

  const PropertyNode* anchornode = rangenode.FindKey("anchor");
  if (!anchornode) {
    TEXTMARKER_FAIL(rangenode, "no anchor")
  }

  id anchor_textmarker = DictNodeToTextMarker(*anchornode);
  if (!anchor_textmarker) {
    TEXTMARKER_FAIL(rangenode, "failed to parse anchor")
  }

  const PropertyNode* focusnode = rangenode.FindKey("focus");
  if (!focusnode) {
    TEXTMARKER_FAIL(rangenode, "no focus")
  }

  id focus_textmarker = DictNodeToTextMarker(*focusnode);
  if (!focus_textmarker) {
    TEXTMARKER_FAIL(rangenode, "failed to parse focus")
  }

  return content::AXTextMarkerRangeFrom(anchor_textmarker, focus_textmarker);
}

}  // namespace a11y
}  // namespace content

#pragma clang diagnostic pop
