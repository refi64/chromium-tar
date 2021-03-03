// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_METADATA_TYPE_CONVERSION_H_
#define UI_VIEWS_METADATA_TYPE_CONVERSION_H_

#include <stdint.h>

#include <memory>
#include <utility>
#include <vector>

#include "base/no_destructor.h"
#include "base/optional.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/range/range.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/views_export.h"
#include "url/gurl.h"
#include "url/third_party/mozilla/url_parse.h"

namespace views {
namespace metadata {

using ValidStrings = std::vector<base::string16>;

// Various metadata methods pass types either by value or const ref depending on
// whether the types are "small" (defined as "fundamental, enum, or pointer").
// ArgType<T> gives the appropriate type to use as an argument in such cases.
template <typename T>
using ArgType =
    typename std::conditional<std::is_fundamental<T>::value ||
                                  std::is_enum<T>::value ||
                                  std::is_pointer<T>::value ||
                                  (std::is_move_assignable<T>::value &&
                                   std::is_move_constructible<T>::value &&
                                   !std::is_copy_assignable<T>::value &&
                                   !std::is_copy_constructible<T>::value),
                              T,
                              const T&>::type;

// General Type Conversion Template Functions ---------------------------------
template <bool serializable>
struct BaseTypeConverter {
  static constexpr bool is_serializable = serializable;
  static bool IsSerializable() { return is_serializable; }
};

template <typename T>
struct TypeConverter : BaseTypeConverter<std::is_enum<T>::value> {
  static base::string16 ToString(ArgType<T> source_value);
  static base::Optional<T> FromString(const base::string16& source_value);
  static ValidStrings GetValidStrings();
};

// Types and macros for generating enum converters ----------------------------
template <typename T>
struct EnumStrings {
  struct EnumString {
    T enum_value;
    base::string16 str_value;
  };

  explicit EnumStrings(std::vector<EnumString> init_val)
      : pairs(std::move(init_val)) {}

  ValidStrings GetStringValues() const {
    ValidStrings string_values;
    for (const auto& pair : pairs)
      string_values.push_back(pair.str_value);
    return string_values;
  }

  const std::vector<EnumString> pairs;
};

template <typename T>
static const EnumStrings<T>& GetEnumStringsInstance();

// Generate the code to define a enum type to and from base::string16
// conversions. The first argument is the type T, and the rest of the argument
// should have the enum value and string pairs defined in a format like
// "{enum_value0, string16_value0}, {enum_value1, string16_value1} ...".
#define DEFINE_ENUM_CONVERTERS(T, ...)                             \
  template <>                                                      \
  const views::metadata::EnumStrings<T>&                           \
  views::metadata::GetEnumStringsInstance<T>() {                   \
    static const base::NoDestructor<EnumStrings<T>> instance(      \
        std::vector<views::metadata::EnumStrings<T>::EnumString>(  \
            {__VA_ARGS__}));                                       \
    return *instance;                                              \
  }                                                                \
                                                                   \
  template <>                                                      \
  base::string16 views::metadata::TypeConverter<T>::ToString(      \
      ArgType<T> source_value) {                                   \
    for (const auto& pair : GetEnumStringsInstance<T>().pairs) {   \
      if (source_value == pair.enum_value)                         \
        return pair.str_value;                                     \
    }                                                              \
    return base::string16();                                       \
  }                                                                \
                                                                   \
  template <>                                                      \
  base::Optional<T> views::metadata::TypeConverter<T>::FromString( \
      const base::string16& source_value) {                        \
    for (const auto& pair : GetEnumStringsInstance<T>().pairs) {   \
      if (source_value == pair.str_value) {                        \
        return pair.enum_value;                                    \
      }                                                            \
    }                                                              \
    return base::nullopt;                                          \
  }                                                                \
                                                                   \
  template <>                                                      \
  views::metadata::ValidStrings                                    \
  views::metadata::TypeConverter<T>::GetValidStrings() {           \
    return GetEnumStringsInstance<T>().GetStringValues();          \
  }

// String Conversions ---------------------------------------------------------

// Converts the four elements of |pieces| beginning at |start_piece| to an
// SkColor by assuming the pieces are split from a string like "rgba(r,g,b,a)".
// Returns nullopt if conversion was unsuccessful.
VIEWS_EXPORT base::Optional<SkColor> RgbaPiecesToSkColor(
    const std::vector<base::StringPiece16>& pieces,
    size_t start_piece);

#define DECLARE_CONVERSIONS(T)                                               \
  template <>                                                                \
  struct VIEWS_EXPORT TypeConverter<T> : BaseTypeConverter<true> {           \
    static base::string16 ToString(ArgType<T> source_value);                 \
    static base::Optional<T> FromString(const base::string16& source_value); \
    static ValidStrings GetValidStrings() { return {}; }                     \
  };

DECLARE_CONVERSIONS(int8_t)
DECLARE_CONVERSIONS(int16_t)
DECLARE_CONVERSIONS(int32_t)
DECLARE_CONVERSIONS(int64_t)
DECLARE_CONVERSIONS(uint8_t)
DECLARE_CONVERSIONS(uint16_t)
DECLARE_CONVERSIONS(uint32_t)
DECLARE_CONVERSIONS(uint64_t)
DECLARE_CONVERSIONS(float)
DECLARE_CONVERSIONS(double)
DECLARE_CONVERSIONS(const char*)
DECLARE_CONVERSIONS(base::string16)
DECLARE_CONVERSIONS(base::TimeDelta)
DECLARE_CONVERSIONS(gfx::ShadowValues)
DECLARE_CONVERSIONS(gfx::Size)
DECLARE_CONVERSIONS(gfx::Range)
DECLARE_CONVERSIONS(gfx::Insets)
DECLARE_CONVERSIONS(GURL)
DECLARE_CONVERSIONS(url::Component)

#undef DECLARE_CONVERSIONS

template <>
struct VIEWS_EXPORT TypeConverter<bool> : BaseTypeConverter<true> {
  static base::string16 ToString(bool source_value);
  static base::Optional<bool> FromString(const base::string16& source_value);
  static ValidStrings GetValidStrings();
};

// Special Conversions for base::Optional<T> type ------------------------------

VIEWS_EXPORT const base::string16& GetNullOptStr();

template <typename T>
struct TypeConverter<base::Optional<T>>
    : BaseTypeConverter<TypeConverter<T>::is_serializable> {
  static base::string16 ToString(ArgType<base::Optional<T>> source_value) {
    if (!source_value)
      return GetNullOptStr();
    return TypeConverter<T>::ToString(source_value.value());
  }
  static base::Optional<base::Optional<T>> FromString(
      const base::string16& source_value) {
    if (source_value == GetNullOptStr())
      return base::make_optional<base::Optional<T>>(base::nullopt);

    auto ret = TypeConverter<T>::FromString(source_value);
    return ret ? base::make_optional(ret) : base::nullopt;
  }
  static ValidStrings GetValidStrings() { return {}; }
};

template <typename T>
struct TypeConverter<std::unique_ptr<T>> : BaseTypeConverter<false> {
  static base::string16 ToString(const std::unique_ptr<T>& source_value);
  static base::Optional<std::unique_ptr<T>> FromString(
      const base::string16& source_value);
  static ValidStrings GetValidStrings() { return {}; }
};

}  // namespace metadata
}  // namespace views

#endif  // UI_VIEWS_METADATA_TYPE_CONVERSION_H_
