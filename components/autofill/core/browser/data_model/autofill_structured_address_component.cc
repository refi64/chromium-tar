// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/data_model/autofill_structured_address_component.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string16.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/data_model/autofill_structured_address_constants.h"
#include "components/autofill/core/browser/data_model/autofill_structured_address_utils.h"
#include "components/autofill/core/browser/field_types.h"

namespace autofill {

namespace structured_address {
AddressComponent::AddressComponent(ServerFieldType storage_type)
    : AddressComponent(storage_type, nullptr, {}) {}

AddressComponent::AddressComponent(ServerFieldType storage_type,
                                   AddressComponent* parent)
    : AddressComponent(storage_type, parent, {}) {}

AddressComponent::AddressComponent(ServerFieldType storage_type,
                                   AddressComponent* parent,
                                   std::vector<AddressComponent*> subcomponents)
    : value_verification_status_(VerificationStatus::kNoStatus),
      storage_type_(storage_type),
      subcomponents_(subcomponents),
      parent_(parent) {}

AddressComponent::~AddressComponent() = default;

ServerFieldType AddressComponent::GetStorageType() const {
  return storage_type_;
}

std::string AddressComponent::GetStorageTypeName() const {
  return AutofillType(storage_type_).ToString();
}

AddressComponent& AddressComponent::operator=(const AddressComponent& right) {
  DCHECK(GetStorageType() == right.GetStorageType());
  if (this == &right)
    return *this;

  if (right.IsValueAssigned()) {
    value_ = right.value_;
    value_verification_status_ = right.value_verification_status_;
    sorted_normalized_tokens_ = right.sorted_normalized_tokens_;
  } else {
    UnsetValue();
  }

  DCHECK(right.subcomponents_.size() == subcomponents_.size());

  for (size_t i = 0; i < right.subcomponents_.size(); i++)
    *subcomponents_[i] = *right.subcomponents_[i];

  return *this;
}

bool AddressComponent::operator==(const AddressComponent& right) const {
  if (this == &right)
    return true;

  if (GetStorageType() != right.GetStorageType())
    return false;

  if (value_ != right.value_ ||
      value_verification_status_ != right.value_verification_status_)
    return false;

  DCHECK(right.subcomponents_.size() == subcomponents_.size());
  for (size_t i = 0; i < right.subcomponents_.size(); i++)
    if (!(*subcomponents_[i] == *right.subcomponents_[i]))
      return false;
  return true;
}

bool AddressComponent::operator!=(const AddressComponent& right) const {
  return !(*this == right);
}

bool AddressComponent::IsAtomic() const {
  return subcomponents_.empty();
}

VerificationStatus AddressComponent::GetVerificationStatus() const {
  return value_verification_status_;
}

const base::string16& AddressComponent::GetValue() const {
  if (value_.has_value())
    return value_.value();
  return base::EmptyString16();
}

bool AddressComponent::IsValueAssigned() const {
  return value_.has_value();
}

void AddressComponent::SetValue(base::string16 value,
                                VerificationStatus status) {
  value_ = std::move(value);
  value_verification_status_ = status;
  sorted_normalized_tokens_ = TokenizeValue(value_.value());
}

void AddressComponent::UnsetValue() {
  value_.reset();
  value_verification_status_ = VerificationStatus::kNoStatus;
  sorted_normalized_tokens_.clear();
}

void AddressComponent::GetSupportedTypes(
    ServerFieldTypeSet* supported_types) const {
  // A proper AddressComponent tree contains every type only once.
  DCHECK(supported_types->find(storage_type_) == supported_types->end())
      << "The AddressComponent already contains a node that supports this "
         "type: "
      << storage_type_;
  supported_types->insert(storage_type_);
  GetAdditionalSupportedFieldTypes(supported_types);
  for (auto* subcomponent : subcomponents_)
    subcomponent->GetSupportedTypes(supported_types);
}

bool AddressComponent::ConvertAndSetValueForAdditionalFieldTypeName(
    const std::string& field_type_name,
    const base::string16& value,
    const VerificationStatus& status) {
  return false;
}

bool AddressComponent::ConvertAndGetTheValueForAdditionalFieldTypeName(
    const std::string& field_type_name,
    base::string16* value) const {
  return false;
}

base::string16 AddressComponent::GetBestFormatString() const {
  // If the component is atomic, the format string is just the value.
  if (IsAtomic())
    return base::ASCIIToUTF16(GetPlaceholderToken(GetStorageTypeName()));

  // Otherwise, the canonical format string is the concatenation of all
  // subcomponents by their natural order.
  std::vector<std::string> format_pieces;
  for (const auto* subcomponent : subcomponents_) {
    std::string format_piece = GetPlaceholderToken(
        AutofillType(subcomponent->GetStorageType()).ToString());
    format_pieces.emplace_back(std::move(format_piece));
  }
  return base::ASCIIToUTF16(base::JoinString(format_pieces, " "));
}

std::vector<ServerFieldType> AddressComponent::GetSubcomponentTypes() const {
  std::vector<ServerFieldType> subcomponent_types;
  subcomponent_types.reserve(subcomponents_.size());
  for (const auto* subcomponent : subcomponents_) {
    subcomponent_types.emplace_back(subcomponent->GetStorageType());
  }
  return subcomponent_types;
}

bool AddressComponent::SetValueForTypeIfPossible(
    const ServerFieldType& type,
    const base::string16& value,
    const VerificationStatus& verification_status,
    bool invalidate_child_nodes,
    bool invalidate_parent_nodes) {
  return SetValueForTypeIfPossible(AutofillType(type).ToString(), value,
                                   verification_status, invalidate_child_nodes,
                                   invalidate_parent_nodes);
}

bool AddressComponent::SetValueForTypeIfPossible(
    const std::string& type_name,
    const base::string16& value,
    const VerificationStatus& verification_status,
    bool invalidate_child_nodes,
    bool invalidate_parent_nodes) {
  bool value_set = false;
  // If the type is the storage type of the component, it can directly be
  // returned.
  if (type_name == GetStorageTypeName()) {
    SetValue(value, verification_status);
    value_set = true;
  } else if (ConvertAndSetValueForAdditionalFieldTypeName(
                 type_name, value, verification_status)) {
    // The conversion using a field type was successful.
    value_set = true;
  }

  if (value_set) {
    if (invalidate_child_nodes)
      UnsetSubcomponents();
    return true;
  }

  // Finally, probe if the type is supported by one of the subcomponents.
  for (auto* subcomponent : subcomponents_) {
    if (subcomponent->SetValueForTypeIfPossible(
            type_name, value, verification_status, invalidate_child_nodes,
            invalidate_parent_nodes)) {
      if (invalidate_parent_nodes)
        UnsetValue();
      return true;
    }
  }

  return false;
}

void AddressComponent::UnsetAddressComponentAndItsSubcomponents() {
  UnsetValue();
  UnsetSubcomponents();
}

void AddressComponent::UnsetSubcomponents() {
  for (auto* component : subcomponents_)
    component->UnsetAddressComponentAndItsSubcomponents();
}

bool AddressComponent::GetValueAndStatusForTypeIfPossible(
    const ServerFieldType& type,
    base::string16* value,
    VerificationStatus* status) const {
  return GetValueAndStatusForTypeIfPossible(AutofillType(type).ToString(),
                                            value, status);
}

bool AddressComponent::GetValueAndStatusForTypeIfPossible(
    const std::string& type_name,
    base::string16* value,
    VerificationStatus* status) const {
  // If the value is the storage type, it can be simply returned.
  if (type_name == GetStorageTypeName()) {
    if (value)
      *value = value_.value_or(base::string16());
    if (status)
      *status = GetVerificationStatus();
    return true;
  }

  // Otherwise, probe if it is a supported field type that can be converted.
  if (this->ConvertAndGetTheValueForAdditionalFieldTypeName(type_name, value)) {
    if (status)
      *status = GetVerificationStatus();
    return true;
  }

  // Finally, try to retrieve the value from one of the subcomponents.
  for (const auto* subcomponent : subcomponents_) {
    if (subcomponent->GetValueAndStatusForTypeIfPossible(type_name, value,
                                                         status))
      return true;
  }
  return false;
}

base::string16 AddressComponent::GetValueForType(
    const ServerFieldType& type) const {
  return GetValueForType(AutofillType(type).ToString());
}

base::string16 AddressComponent::GetValueForType(
    const std::string& type_name) const {
  base::string16 value;
  bool success = GetValueAndStatusForTypeIfPossible(type_name, &value, nullptr);
  // TODO(crbug.com/1113617): Honorifics are temporally disabled.
  DCHECK(success ||
         type_name == AutofillType(NAME_HONORIFIC_PREFIX).ToString());
  return value;
}

VerificationStatus AddressComponent::GetVerificationStatusForType(
    const ServerFieldType& type) const {
  return GetVerificationStatusForType(AutofillType(type).ToString());
}

VerificationStatus AddressComponent::GetVerificationStatusForType(
    const std::string& type_name) const {
  VerificationStatus status = VerificationStatus::kNoStatus;
  bool success =
      GetValueAndStatusForTypeIfPossible(type_name, nullptr, &status);
  // TODO(crbug.com/1113617): Honorifics are temporally disabled.
  DCHECK(success ||
         type_name == AutofillType(NAME_HONORIFIC_PREFIX).ToString());
  return status;
}

bool AddressComponent::UnsetValueForTypeIfSupported(
    const ServerFieldType& type) {
  if (type == storage_type_) {
    UnsetAddressComponentAndItsSubcomponents();
    return true;
  }

  for (auto* subcomponent : subcomponents_) {
    if (subcomponent->UnsetValueForTypeIfSupported(type))
      return true;
  }

  return false;
}

bool AddressComponent::ParseValueAndAssignSubcomponentsByMethod() {
  return false;
}

std::vector<const re2::RE2*>
AddressComponent::GetParseRegularExpressionsByRelevance() const {
  return {};
}

void AddressComponent::ParseValueAndAssignSubcomponents() {
  // Set the values of all subcomponents to the empty string and set the
  // verification status to kParsed.
  for (auto* subcomponent : subcomponents_)
    subcomponent->SetValue(base::string16(), VerificationStatus::kParsed);

  // First attempt, try to parse by method.
  if (ParseValueAndAssignSubcomponentsByMethod())
    return;

  // Second attempt, try to parse by expressions.
  if (ParseValueAndAssignSubcomponentsByRegularExpressions())
    return;

  // As a final fallback, parse using the fallback method.
  ParseValueAndAssignSubcomponentsByFallbackMethod();
}

bool AddressComponent::ParseValueAndAssignSubcomponentsByRegularExpressions() {
  for (const auto* parse_expression : GetParseRegularExpressionsByRelevance()) {
    if (!parse_expression)
      continue;
    std::map<std::string, std::string> result_map;
    if (ParseValueByRegularExpression(base::UTF16ToUTF8(GetValue()),
                                      parse_expression, &result_map)) {
      // Parsing was successful and results from the result map can be written
      // to the structure.
      for (const auto& result_entry : result_map) {
        std::string field_type = result_entry.first;
        base::string16 field_value = base::UTF8ToUTF16(result_entry.second);
        // Do not reassign the value of this node.
        if (field_type == GetStorageTypeName())
          continue;
        // crbug.com(1113617): Honorifics are temporally disabled.
        if (field_type == AutofillType(NAME_HONORIFIC_PREFIX).ToString())
          continue;
        bool success = SetValueForTypeIfPossible(field_type, field_value,
                                                 VerificationStatus::kParsed);
        // Setting the value should always work unless the regular expression is
        // invalid.
        DCHECK(success);
      }
      return true;
    }
  }
  return false;
}

void AddressComponent::ParseValueAndAssignSubcomponentsByFallbackMethod() {
  // There is nothing to do for an atomic component.
  if (IsAtomic())
    return;

  // An empty string is trivially parsable.
  if (GetValue().empty())
    return;

  // Split the string by spaces.
  std::vector<base::string16> space_separated_tokens =
      base::SplitString(GetValue(), base::UTF8ToUTF16(" "),
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  auto token_iterator = space_separated_tokens.begin();
  auto subcomponent_types = GetSubcomponentTypes();

  // Assign one space-separated token each to all but the last subcomponent.
  for (size_t i = 0; (i + 1) < subcomponent_types.size(); i++) {
    // If there are no tokens left, parsing is done.
    if (token_iterator == space_separated_tokens.end())
      return;
    // Set the current token to the type and advance the token iterator.
    bool success = SetValueForTypeIfPossible(
        subcomponent_types[i], *token_iterator, VerificationStatus::kParsed);
    // By design, setting the value should never fail.
    DCHECK(success);
    token_iterator++;
  }

  // Collect all remaining tokens in the last subcomponent.
  base::string16 remaining_tokens = base::JoinString(
      std::vector<base::string16>(token_iterator, space_separated_tokens.end()),
      base::ASCIIToUTF16(" "));
  // By design, it should be possible to assign the value unless the regular
  // expression is wrong.
  bool success = SetValueForTypeIfPossible(
      subcomponent_types.back(), remaining_tokens, VerificationStatus::kParsed);
  DCHECK(success);
}

void AddressComponent::FormatValueFromSubcomponents() {
  // Get the most suited format string.
  base::string16 format_string = GetBestFormatString();

  // Perform the following steps on a copy of the format string.
  // * Replace all the placeholders of the form ${TYPE_NAME} with the
  // corresponding value.
  // * Strip away double spaces as they may occur after replacing a placeholder
  // with an empty value.

  base::string16 result = ReplacePlaceholderTypesWithValues(format_string);
  result = base::CollapseWhitespace(result, /*trim_line_breaks=*/false);
  SetValue(result, VerificationStatus::kFormatted);
}

base::string16 AddressComponent::ReplacePlaceholderTypesWithValues(
    const base::string16& format) const {
  // Replaces placeholders using the following rules.
  // Assumptions: Placeholder values are not nested.
  //
  // * Search for a substring of the form "{$[^}]*}".
  //
  // * Check if this substring is a supported type of this component.
  //
  // * If yes, replace the substring with the corresponding value.
  //
  // * If the corresponding value is empty, return false.

  auto control_parmater = base::ASCIIToUTF16("$").at(0);
  auto control_parmater_open_delimitor = base::ASCIIToUTF16("{").at(0);
  auto control_parmater_close_delimitor = base::ASCIIToUTF16("}").at(0);

  // Create a result vector for the tokens that are joined in the end.
  std::vector<base::StringPiece16> result_pieces;
  // Reserve space for 10 tokens. This should be sufficient for most cases.
  result_pieces.reserve(10);

  // Store the inserted values to allow the used StringPieces to stay valid.
  std::vector<base::string16> inserted_values;
  inserted_values.reserve(4);

  // Use a StringPiece rather than the string since this allows for getting
  // cheap views onto substrings.
  const base::StringPiece16 format_piece = format;

  bool started_control_sequence = false;
  // Track until which index the format string was fully processed.
  size_t processed_until_index = 0;

  for (size_t i = 0; i < format_piece.size(); ++i) {
    // Check if a control sequence is started by '${'
    if (format_piece[i] == control_parmater && i < format_piece.size() - 1 &&
        format_piece[i + 1] == control_parmater_open_delimitor) {
      // A control sequence is started.
      started_control_sequence = true;
      // Append the preceding string since it can't be a valid placeholder.
      if (i > 0) {
        result_pieces.emplace_back(format_piece.substr(
            processed_until_index, i - processed_until_index));
      }
      processed_until_index = i;
      ++i;
    } else if (started_control_sequence &&
               format_piece[i] == control_parmater_close_delimitor) {
      // The control sequence came to an end.
      started_control_sequence = false;
      size_t placeholder_start = processed_until_index + 2;
      base::string16 type_name =
          format_piece.substr(placeholder_start, i - placeholder_start)
              .as_string();
      base::string16 value;
      if (GetValueAndStatusForTypeIfPossible(base::UTF16ToASCII(type_name),
                                             &value, nullptr)) {
        // The type is valid and should be substituted.
        inserted_values.emplace_back(std::move(value));
        result_pieces.emplace_back(base::StringPiece16(inserted_values.back()));
      } else {
        // Append the control sequence as it is, because the type is not
        // supported by the component tree.
        result_pieces.emplace_back(format_piece.substr(
            processed_until_index, i - processed_until_index + 1));
      }
      processed_until_index = i + 1;
    }
  }
  // Append the rest of the string.
  result_pieces.emplace_back(
      format_piece.substr(processed_until_index, base::string16::npos));

  // Build the final result.
  return base::JoinString(result_pieces, base::ASCIIToUTF16(""));
}

bool AddressComponent::CompleteFullTree() {
  int max_nodes_on_root_to_leaf_path =
      GetRootNode().MaximumNumberOfAssignedAddressComponentsOnNodeToLeafPaths();
  // With more than one node the tree cannot be completed.
  switch (max_nodes_on_root_to_leaf_path) {
    // An empty tree is already complete.
    case 0:
      return true;
    // With a single node, the tree is completable.
    case 1:
      GetRootNode().RecursivelyCompleteTree();
      return true;
    // In any other case, the tree is not completable.
    default:
      return false;
  }
}

void AddressComponent::RecursivelyCompleteTree() {
  if (IsAtomic())
    return;

  // If the value is assigned, parse the subcomponents from the value.
  if (!GetValue().empty())
    ParseValueAndAssignSubcomponents();

  // First call completion on all subcomponents.
  for (auto* subcomponent : subcomponents_)
    subcomponent->RecursivelyCompleteTree();

  // Finally format the value from the sucomponents if it is not already
  // assigned.
  if (GetValue().empty())
    FormatValueFromSubcomponents();
}

int AddressComponent::
    MaximumNumberOfAssignedAddressComponentsOnNodeToLeafPaths() const {
  int result = 0;

  for (auto* subcomponent : subcomponents_) {
    result = std::max(
        result,
        subcomponent
            ->MaximumNumberOfAssignedAddressComponentsOnNodeToLeafPaths());
  }

  // Only count non-empty nodes.
  if (!GetValue().empty())
    ++result;

  return result;
}

bool AddressComponent::IsTreeCompletable() {
  // An empty tree is also a completable tree.
  return MaximumNumberOfAssignedAddressComponentsOnNodeToLeafPaths() <= 1;
}

const AddressComponent& AddressComponent::GetRootNode() const {
  if (!parent_)
    return *this;
  return parent_->GetRootNode();
}

AddressComponent& AddressComponent::GetRootNode() {
  return const_cast<AddressComponent&>(
      const_cast<const AddressComponent*>(this)->GetRootNode());
}

void AddressComponent::RecursivelyUnsetParsedAndFormattedValues() {
  if (IsValueAssigned() &&
      (GetVerificationStatus() == VerificationStatus::kFormatted ||
       GetVerificationStatus() == VerificationStatus::kParsed))
    UnsetValue();

  for (auto* component : subcomponents_)
    component->RecursivelyUnsetParsedAndFormattedValues();
}

void AddressComponent::RecursivelyUnsetSubcomponents() {
  for (auto* subcomponent : subcomponents_) {
    subcomponent->UnsetValue();
    subcomponent->RecursivelyUnsetSubcomponents();
  }
}

void AddressComponent::UnsetParsedAndFormattedValuesInEntireTree() {
  GetRootNode().RecursivelyUnsetParsedAndFormattedValues();
}

void AddressComponent::MergeVerificationStatuses(
    const AddressComponent& newer_component) {
  if (IsValueAssigned() && (GetValue() == newer_component.GetValue()) &&
      (GetVerificationStatus() < newer_component.GetVerificationStatus())) {
    value_verification_status_ = newer_component.GetVerificationStatus();
  }

  DCHECK(newer_component.subcomponents_.size() == subcomponents_.size());
  for (size_t i = 0; i < newer_component.subcomponents_.size(); i++) {
    subcomponents_[i]->MergeVerificationStatuses(
        *newer_component.subcomponents_.at(i));
  }
}

bool AddressComponent::IsMergeableWithComponent(
    const AddressComponent& newer_component) const {
  // If both components are the same, there is nothing to do.
  if (*this == newer_component)
    return true;

  return AreSortedTokensEqual(GetSortedTokens(),
                              newer_component.GetSortedTokens());
}

bool AddressComponent::MergeWithComponent(
    const AddressComponent& newer_component) {
  // If both components are the same, there is nothing to do.
  if (*this == newer_component)
    return true;

  if (!IsMergeableWithComponent(newer_component))
    return false;

  // Applies the merging strategy for two token-equivalent components.
  if (AreSortedTokensEqual(GetSortedTokens(),
                           newer_component.GetSortedTokens())) {
    return MergeTokenEquivalentComponent(newer_component);
  }
  return false;
}

bool AddressComponent::MergeTokenEquivalentComponent(
    const AddressComponent& newer_component) {
  // Assumption:
  // The values of both components are a permutation of the same tokens.
  // The componentization of the components can be different in terms of
  // how the tokens are divided between the subomponents. The valdiation
  // status of the component and its subcomponent can be different.
  //
  // Merge Strategy:
  // * Adopt the exact value (and validation status) of the node with the higher
  // validation status.
  //
  // * For all subcomponents that have the same value, make a recursive call and
  // use the result.
  //
  // * For the set of all non-matching subcomponents. Either use the ones from
  // this component or the other depending on which substructure is better in
  // terms of the number of validated tokens.

  if (newer_component.GetVerificationStatus() >= GetVerificationStatus()) {
    SetValue(newer_component.GetValue(),
             newer_component.GetVerificationStatus());
  }

  // Now, the substructure of the node must be merged. There are three cases:
  //
  // * All nodes of the substructure are pairwise mergeable. In this case it
  // is sufficient to apply a recursive merging strategy.
  //
  // * None of the nodes of the substructure are pairwise mergeable. In this
  // case, either the complete substructure of |this| or |newer_component|
  // must be used. Which one to use can be decided by the higher validation
  // score.
  //
  // * In a mixed scenario, there is at least one pair of mergeable nodes
  // in the substructure and at least on pair of non-mergeable nodes. Here,
  // the mergeable nodes are merged while all other nodes are taken either
  // from |this| or the |newer_component| decided by the higher validation
  // score of the unmerged nodes.
  //
  // The following algorithm combines the three cases by first trying to merge
  // all components pair-wise. For all components that couldn't be merged, the
  // verification score is summed for this and the other component. If the other
  // component has an equal or larger score, finalize the merge by using its
  // components. It is assumed that the other component is the newer of the two
  // components. By favoring the other component in a tie, the most recently
  // used structure wins.

  const std::vector<AddressComponent*> other_subcomponents =
      newer_component.Subcomponents();

  DCHECK(subcomponents_.size() == newer_component.Subcomponents().size());

  int this_component_verification_score = 0;
  int newer_component_verification_score = 0;

  std::vector<int> unmerged_indices;
  unmerged_indices.reserve(subcomponents_.size());

  for (size_t i = 0; i < subcomponents_.size(); i++) {
    DCHECK(subcomponents_[i]->GetStorageType() ==
           other_subcomponents.at(i)->GetStorageType());

    // If the components can't be merged directly, store the ungermed index and
    // sum the verification scores to decide which component's substructure to
    // use.
    if (!subcomponents_[i]->MergeWithComponent(*other_subcomponents.at(i))) {
      this_component_verification_score +=
          subcomponents_[i]->GetStructureVerificationScore();
      newer_component_verification_score +=
          other_subcomponents.at(i)->GetStructureVerificationScore();
      unmerged_indices.emplace_back(i);
    }
  }

  // If the total verification score of all unmerged components of the other
  // component is equal or larger than the score of this component, use its
  // subcomponents including their substructure for all unmerged components.
  if (newer_component_verification_score >= this_component_verification_score) {
    for (size_t i : unmerged_indices)
      *subcomponents_[i] = *other_subcomponents[i];
  }

  return true;
}

int AddressComponent::GetStructureVerificationScore() const {
  int result = 0;
  switch (GetVerificationStatus()) {
    case VerificationStatus::kNoStatus:
    case VerificationStatus::kParsed:
    case VerificationStatus::kFormatted:
      break;
    case VerificationStatus::kObserved:
      result += 1;
      break;
    case VerificationStatus::kUserVerified:
      // In the current implementation, only the root not can be verified by
      // the user.
      NOTREACHED();
      break;
  }
  for (const AddressComponent* component : subcomponents_)
    result += component->GetStructureVerificationScore();

  return result;
}

}  // namespace structured_address

}  // namespace autofill
