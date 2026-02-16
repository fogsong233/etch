#include "str2ex.h"
#include "tnfa.h"

#include <boost/ut.hpp>

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace ut = boost::ut;
using namespace etch;

template <FixedString Pattern>
consteval auto buildModel() {
    constexpr auto tree = parseToRegexTree<Pattern>();
    static_assert(tree.ok(), "failed to parse regex pattern");
    constexpr auto model = TNFA::fromRegexTreeAuto<tree>();
    static_assert(model.has_value(), "compile-time TNFA build failed");
    return *model;
}

template <typename Model>
auto runSimulation(const Model& model, std::string_view input) -> std::optional<std::vector<int>> {
    return TNFA::simulation(model, input);
}

template <typename Model>
void expectMatch(const Model& model, std::string_view input) {
    ut::expect(runSimulation(model, input).has_value());
}

template <typename Model>
void expectNoMatch(const Model& model, std::string_view input) {
    ut::expect(!runSimulation(model, input).has_value());
}

template <typename Model, std::size_t N>
void expectTags(const Model& model, std::string_view input, const std::array<int, N>& expected) {
    const auto result = runSimulation(model, input);
    ut::expect(result.has_value());
    if(!result.has_value()) {
        return;
    }
    ut::expect(result->size() == expected.size());
    for(std::size_t i = 0; i < N; ++i) {
        ut::expect((*result)[i] == expected[i]);
    }
}

constexpr auto kLiteral = buildModel<R"(abc)">();
constexpr auto kAlternation = buildModel<R"(a|ab)">();
constexpr auto kStar = buildModel<R"(ab*c)">();
constexpr auto kPlus = buildModel<R"(ab+c)">();
constexpr auto kQuestion = buildModel<R"(ab?c)">();
constexpr auto kRange = buildModel<R"(ab{2,4}c)">();
constexpr auto kClass = buildModel<R"(\d+\w)">();
constexpr auto kAny = buildModel<R"(a.*c)">();
constexpr auto kTagBasic = buildModel<R"(\g{1}a\g{2})">();
constexpr auto kTagAltReset = buildModel<R"(\g{1}ab|a)">();
constexpr auto kTagOptionalReset = buildModel<R"((\g{1}a)?a)">();
constexpr auto kTagStar = buildModel<R"((\g{1}a)*)">();

int main() {
    using namespace ut;

    "literal"_test = [] {
        expectMatch(kLiteral, "abc");
        expectNoMatch(kLiteral, "ab");
        expectNoMatch(kLiteral, "abcd");
    };

    "alternation"_test = [] {
        expectMatch(kAlternation, "a");
        expectMatch(kAlternation, "ab");
        expectNoMatch(kAlternation, "b");
    };

    "repetition_star_plus_question"_test = [] {
        expectMatch(kStar, "ac");
        expectMatch(kStar, "abbbbbc");
        expectNoMatch(kStar, "abbbbbd");

        expectNoMatch(kPlus, "ac");
        expectMatch(kPlus, "abc");
        expectMatch(kPlus, "abbbbbc");

        expectMatch(kQuestion, "ac");
        expectMatch(kQuestion, "abc");
        expectNoMatch(kQuestion, "abbc");
    };

    "repetition_range"_test = [] {
        expectNoMatch(kRange, "abc");
        expectMatch(kRange, "abbc");
        expectMatch(kRange, "abbbc");
        expectMatch(kRange, "abbbbc");
        expectNoMatch(kRange, "abbbbbc");
    };

    "char_check_and_dot"_test = [] {
        expectMatch(kClass, "123a");
        expectMatch(kClass, "9_");
        expectNoMatch(kClass, "123-");
        expectNoMatch(kClass, "a1");

        expectMatch(kAny, "ac");
        expectMatch(kAny, "abbbbbc");
        expectNoMatch(kAny, "abbbbbd");
    };

    "tag_positions"_test = [] {
        expectTags(kTagBasic, "a", std::array<int, 2>{1, 1});
    };

    "tag_reset_in_alternation"_test = [] {
        expectTags(kTagAltReset, "a", std::array<int, 1>{-1});
    };

    "tag_reset_in_optional"_test = [] {
        expectTags(kTagOptionalReset, "a", std::array<int, 1>{-1});
        expectTags(kTagOptionalReset, "aa", std::array<int, 1>{1});
    };

    "tag_in_star_repetition"_test = [] {
        expectTags(kTagStar, "", std::array<int, 1>{-1});
        expectTags(kTagStar, "aaa", std::array<int, 1>{3});
    };

    return 0;
}
