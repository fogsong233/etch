#pragma once

#include "../lib/regex.h"
#include "../lib/ty.h"
#include "../lib/util.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace etch::testdata {

struct TagIndex {
    TagTy tag = 0;
    int index = -1;
};

struct RegexStringCase {
    std::string_view input{};
    bool shouldMatch = false;
    std::span<const TagIndex> tagToIndex{};
};

struct RegexFixture {
    std::string_view name{};
    std::string_view regex{};
    std::span<const RegexStringCase> testStrings{};
};

struct ParseErrorFixture {
    std::string_view name{};
    std::string_view regex{};
    regex::RegexParseError expectedError = regex::RegexParseError::none;
};

enum class RegexCaseId : std::size_t {
    literal = 0,
    email_like,
    iso_date,
    us_phone,
    path_optional_ext,
    image_ext,
    tag_basic,
    tag_alt_reset,
    tag_optional_reset,
    tag_star,
    tag_plus,
    tag_repeat_range,
};

enum class ParseErrorCaseId : std::size_t {
    dangling_escape = 0,
    missing_right_paren,
    tag_missing_left_brace,
    tag_missing_right_brace,
    invalid_integer,
    integer_overflow,
    unknown_named_char_check,
    invalid_range_quantifier,
};

template <RegexCaseId Id>
struct RegexPattern;

template <>
struct RegexPattern<RegexCaseId::literal> {
    constexpr static auto value = FixedString{"abc"};
};

template <>
struct RegexPattern<RegexCaseId::email_like> {
    constexpr static auto value = FixedString{R"(\w+@\w+\.\w+)"};
};

template <>
struct RegexPattern<RegexCaseId::iso_date> {
    constexpr static auto value = FixedString{R"(\d{4}-\d{2}-\d{2})"};
};

template <>
struct RegexPattern<RegexCaseId::us_phone> {
    constexpr static auto value = FixedString{R"(\d{3}-\d{3}-\d{4})"};
};

template <>
struct RegexPattern<RegexCaseId::path_optional_ext> {
    constexpr static auto value = FixedString{R"(\w+/\w+(\.\w+)?)"};
};

template <>
struct RegexPattern<RegexCaseId::image_ext> {
    constexpr static auto value = FixedString{R"(\w+\.(jpg|png|gif))"};
};

template <>
struct RegexPattern<RegexCaseId::tag_basic> {
    constexpr static auto value = FixedString{R"(\g{0}a\g{1})"};
};

template <>
struct RegexPattern<RegexCaseId::tag_alt_reset> {
    constexpr static auto value = FixedString{R"(\g{0}ab|a)"};
};

template <>
struct RegexPattern<RegexCaseId::tag_optional_reset> {
    constexpr static auto value = FixedString{R"((\g{0}a)?a)"};
};

template <>
struct RegexPattern<RegexCaseId::tag_star> {
    constexpr static auto value = FixedString{R"((\g{0}a)*)"};
};

template <>
struct RegexPattern<RegexCaseId::tag_plus> {
    constexpr static auto value = FixedString{R"((\g{0}a)+)"};
};

template <>
struct RegexPattern<RegexCaseId::tag_repeat_range> {
    constexpr static auto value = FixedString{R"((\g{0}ab){2,3})"};
};

template <RegexCaseId Id>
constexpr inline auto kRegexPattern = RegexPattern<Id>::value;

template <ParseErrorCaseId Id>
struct ParseErrorPattern;

template <>
struct ParseErrorPattern<ParseErrorCaseId::dangling_escape> {
    constexpr static auto value = FixedString{"\\"};
};

template <>
struct ParseErrorPattern<ParseErrorCaseId::missing_right_paren> {
    constexpr static auto value = FixedString{"(ab"};
};

template <>
struct ParseErrorPattern<ParseErrorCaseId::tag_missing_left_brace> {
    constexpr static auto value = FixedString{R"(\g1})"};
};

template <>
struct ParseErrorPattern<ParseErrorCaseId::tag_missing_right_brace> {
    constexpr static auto value = FixedString{R"(\g{1)"};
};

template <>
struct ParseErrorPattern<ParseErrorCaseId::invalid_integer> {
    constexpr static auto value = FixedString{R"(a{,2})"};
};

template <>
struct ParseErrorPattern<ParseErrorCaseId::integer_overflow> {
    constexpr static auto value = FixedString{R"(a{2147483648})"};
};

template <>
struct ParseErrorPattern<ParseErrorCaseId::unknown_named_char_check> {
    constexpr static auto value = FixedString{R"(\x{unknown})"};
};

template <>
struct ParseErrorPattern<ParseErrorCaseId::invalid_range_quantifier> {
    constexpr static auto value = FixedString{R"(a{3,2})"};
};

template <ParseErrorCaseId Id>
constexpr inline auto kParseErrorPattern = ParseErrorPattern<Id>::value;

template <RegexCaseId Id>
constexpr auto regexPatternView() -> std::string_view {
    return std::string_view{kRegexPattern<Id>.data, kRegexPattern<Id>.size()};
}

template <ParseErrorCaseId Id>
constexpr auto parseErrorPatternView() -> std::string_view {
    return std::string_view{kParseErrorPattern<Id>.data, kParseErrorPattern<Id>.size()};
}

constexpr inline auto kBuilderEpsilonPattern = FixedString{R"(a|b)"};
constexpr inline auto kBuilderSplitRangePattern = FixedString{R"(\w\d)"};
constexpr inline auto kBuilderCharSupportPattern = FixedString{R"(\w)"};
constexpr inline auto kBuilderInvalidRepeatBasePattern = FixedString{R"(a)"};
constexpr inline auto kParseTooManyNodesPattern = FixedString{R"(ab)"};

template <typename T, std::size_t N>
constexpr auto spanOf(const std::array<T, N>& arr) -> std::span<const T> {
    return std::span<const T>{arr.data(), arr.size()};
}

constexpr auto toIndex(RegexCaseId id) -> std::size_t {
    return static_cast<std::size_t>(id);
}

constexpr auto toIndex(ParseErrorCaseId id) -> std::size_t {
    return static_cast<std::size_t>(id);
}

constexpr inline std::array<TagIndex, 0> kNoTags{};

constexpr inline std::array kLiteralStrings{
    RegexStringCase{"abc",  true,  spanOf(kNoTags)},
    RegexStringCase{"ab",   false, spanOf(kNoTags)},
    RegexStringCase{"abcd", false, spanOf(kNoTags)},
};

constexpr inline std::array kEmailLikeStrings{
    RegexStringCase{"alice_01@test.com", true,  spanOf(kNoTags)},
    RegexStringCase{"a@b.c",             true,  spanOf(kNoTags)},
    RegexStringCase{"alice@test",        false, spanOf(kNoTags)},
    RegexStringCase{"alice@@test.com",   false, spanOf(kNoTags)},
    RegexStringCase{"alice-test.com",    false, spanOf(kNoTags)},
};

constexpr inline std::array kIsoDateStrings{
    RegexStringCase{"2024-12-31", true,  spanOf(kNoTags)},
    RegexStringCase{"1999-01-01", true,  spanOf(kNoTags)},
    RegexStringCase{"2024-2-31",  false, spanOf(kNoTags)},
    RegexStringCase{"24-12-31",   false, spanOf(kNoTags)},
    RegexStringCase{"2024/12/31", false, spanOf(kNoTags)},
};

constexpr inline std::array kUsPhoneStrings{
    RegexStringCase{"123-456-7890", true,  spanOf(kNoTags)},
    RegexStringCase{"000-000-0000", true,  spanOf(kNoTags)},
    RegexStringCase{"123-45-7890",  false, spanOf(kNoTags)},
    RegexStringCase{"1234567890",   false, spanOf(kNoTags)},
};

constexpr inline std::array kPathOptionalExtStrings{
    RegexStringCase{"src/main.cc",  true,  spanOf(kNoTags)},
    RegexStringCase{"src/main",     true,  spanOf(kNoTags)},
    RegexStringCase{"src/",         false, spanOf(kNoTags)},
    RegexStringCase{"/main.cc",     false, spanOf(kNoTags)},
    RegexStringCase{"src/main.c++", false, spanOf(kNoTags)},
};

constexpr inline std::array kImageExtStrings{
    RegexStringCase{"photo.jpg", true,  spanOf(kNoTags)},
    RegexStringCase{"icon.png",  true,  spanOf(kNoTags)},
    RegexStringCase{"anim.gif",  true,  spanOf(kNoTags)},
    RegexStringCase{"photo.bmp", false, spanOf(kNoTags)},
    RegexStringCase{"foo.jpgx",  false, spanOf(kNoTags)},
};

constexpr inline std::array kTagBasicMapA{
    TagIndex{0, 1},
    TagIndex{1, 2},
};
constexpr inline std::array kTagBasicStrings{
    RegexStringCase{"a", true,  spanOf(kTagBasicMapA)},
    RegexStringCase{"",  false, spanOf(kNoTags)      },
};

constexpr inline std::array kTagAltMapA{
    TagIndex{0, -1},
};
constexpr inline std::array kTagAltMapAB{
    TagIndex{0, 1},
};
constexpr inline std::array kTagAltStrings{
    RegexStringCase{"a",  true,  spanOf(kTagAltMapA) },
    RegexStringCase{"ab", true,  spanOf(kTagAltMapAB)},
    RegexStringCase{"b",  false, spanOf(kNoTags)     },
};

constexpr inline std::array kTagOptionalMapA{
    TagIndex{0, -1},
};
constexpr inline std::array kTagOptionalMapAA{
    TagIndex{0, 1},
};
constexpr inline std::array kTagOptionalStrings{
    RegexStringCase{"a",  true,  spanOf(kTagOptionalMapA) },
    RegexStringCase{"aa", true,  spanOf(kTagOptionalMapAA)},
    RegexStringCase{"",   false, spanOf(kNoTags)          },
};

constexpr inline std::array kTagStarMapEmpty{
    TagIndex{0, -1},
};
constexpr inline std::array kTagStarMapAAA{
    TagIndex{0, 3},
};
constexpr inline std::array kTagStarStrings{
    RegexStringCase{"",    true,  spanOf(kTagStarMapEmpty)},
    RegexStringCase{"aaa", true,  spanOf(kTagStarMapAAA)  },
    RegexStringCase{"b",   false, spanOf(kNoTags)         },
};

constexpr inline std::array kTagPlusMapA{
    TagIndex{0, 1},
};
constexpr inline std::array kTagPlusMapAAA{
    TagIndex{0, 3},
};
constexpr inline std::array kTagPlusStrings{
    RegexStringCase{"",    false, spanOf(kNoTags)       },
    RegexStringCase{"a",   true,  spanOf(kTagPlusMapA)  },
    RegexStringCase{"aaa", true,  spanOf(kTagPlusMapAAA)},
};

constexpr inline std::array kTagRangeMapABAB{
    TagIndex{0, -1},
};
constexpr inline std::array kTagRangeMapABABAB{
    TagIndex{0, 5},
};
constexpr inline std::array kTagRangeStrings{
    RegexStringCase{"ab",       false, spanOf(kNoTags)           },
    RegexStringCase{"abab",     true,  spanOf(kTagRangeMapABAB)  },
    RegexStringCase{"ababab",   true,  spanOf(kTagRangeMapABABAB)},
    RegexStringCase{"abababab", false, spanOf(kNoTags)           },
};

constexpr inline std::array kRegexFixtures{
    RegexFixture{"literal",            regexPatternView<RegexCaseId::literal>(),   spanOf(kLiteralStrings) },
    RegexFixture{"email_like",
                 regexPatternView<RegexCaseId::email_like>(),
                 spanOf(kEmailLikeStrings)                                                                 },
    RegexFixture{"iso_date",           regexPatternView<RegexCaseId::iso_date>(),  spanOf(kIsoDateStrings) },
    RegexFixture{"us_phone",           regexPatternView<RegexCaseId::us_phone>(),  spanOf(kUsPhoneStrings) },
    RegexFixture{"path_optional_ext",
                 regexPatternView<RegexCaseId::path_optional_ext>(),
                 spanOf(kPathOptionalExtStrings)                                                           },
    RegexFixture{"image_ext",          regexPatternView<RegexCaseId::image_ext>(), spanOf(kImageExtStrings)},
    RegexFixture{"tag_basic",          regexPatternView<RegexCaseId::tag_basic>(), spanOf(kTagBasicStrings)},
    RegexFixture{"tag_alt_reset",
                 regexPatternView<RegexCaseId::tag_alt_reset>(),
                 spanOf(kTagAltStrings)                                                                    },
    RegexFixture{"tag_optional_reset",
                 regexPatternView<RegexCaseId::tag_optional_reset>(),
                 spanOf(kTagOptionalStrings)                                                               },
    RegexFixture{"tag_star",           regexPatternView<RegexCaseId::tag_star>(),  spanOf(kTagStarStrings) },
    RegexFixture{"tag_plus",           regexPatternView<RegexCaseId::tag_plus>(),  spanOf(kTagPlusStrings) },
    RegexFixture{"tag_repeat_range",
                 regexPatternView<RegexCaseId::tag_repeat_range>(),
                 spanOf(kTagRangeStrings)                                                                  },
};

constexpr inline std::array kParseErrorFixtures{
    ParseErrorFixture{"dangling_escape",
                      parseErrorPatternView<ParseErrorCaseId::dangling_escape>(),
                      regex::RegexParseError::unexpected_end          },
    ParseErrorFixture{"missing_right_paren",
                      parseErrorPatternView<ParseErrorCaseId::missing_right_paren>(),
                      regex::RegexParseError::expected_right_paren    },
    ParseErrorFixture{"tag_missing_left_brace",
                      parseErrorPatternView<ParseErrorCaseId::tag_missing_left_brace>(),
                      regex::RegexParseError::expected_left_brace     },
    ParseErrorFixture{"tag_missing_right_brace",
                      parseErrorPatternView<ParseErrorCaseId::tag_missing_right_brace>(),
                      regex::RegexParseError::expected_right_brace    },
    ParseErrorFixture{"invalid_integer",
                      parseErrorPatternView<ParseErrorCaseId::invalid_integer>(),
                      regex::RegexParseError::invalid_integer         },
    ParseErrorFixture{"integer_overflow",
                      parseErrorPatternView<ParseErrorCaseId::integer_overflow>(),
                      regex::RegexParseError::integer_overflow        },
    ParseErrorFixture{"unknown_named_char_check",
                      parseErrorPatternView<ParseErrorCaseId::unknown_named_char_check>(),
                      regex::RegexParseError::unknown_named_char_check},
    ParseErrorFixture{"invalid_range_quantifier",
                      parseErrorPatternView<ParseErrorCaseId::invalid_range_quantifier>(),
                      regex::RegexParseError::unexpected_token        },
};

constexpr auto fixture(RegexCaseId id) -> const RegexFixture& {
    return kRegexFixtures[toIndex(id)];
}

constexpr auto fixture(ParseErrorCaseId id) -> const ParseErrorFixture& {
    return kParseErrorFixtures[toIndex(id)];
}

}  // namespace etch::testdata
