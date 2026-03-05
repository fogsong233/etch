#include "data.h"

#include "../lib/str2ex.h"
#include "../lib/tdfa.h"
#include "../lib/tnfa.h"

#include <boost/ut.hpp>

#include <optional>
#include <vector>

namespace etch::tests {

namespace ut = boost::ut;

template <testdata::RegexCaseId Id>
consteval auto buildTnfaModel() {
    constexpr auto tree = parseToRegexTree<testdata::kRegexPattern<Id>>();
    static_assert(tree.ok(), "failed to parse regex pattern");
    constexpr auto tnfa = TNFA::fromRegexTreeAuto<tree>();
    static_assert(tnfa.has_value(), "compile-time TNFA build failed");
    return *tnfa;
}

template <testdata::RegexCaseId Id>
consteval auto buildTdfaModel() {
    constexpr auto tnfa = buildTnfaModel<Id>();
    constexpr auto tdfa = TDFA::fromTNFAAuto<tnfa>();
    static_assert(tdfa.has_value(), "compile-time TDFA build failed");
    return *tdfa;
}

template <typename Model>
auto runTdfa(const Model& model, std::string_view input) -> std::optional<std::vector<int>> {
    return TDFA::simulation(model, input);
}

template <testdata::RegexCaseId Id>
void expectFixtureRuntime() {
    constexpr auto model = buildTdfaModel<Id>();
    constexpr auto patternView = testdata::regexPatternView<Id>();
    const auto& spec = testdata::fixture(Id);

    ut::expect(spec.regex == patternView);
    for(const auto& sample: spec.testStrings) {
        const auto result = runTdfa(model, sample.input);
        ut::expect(result.has_value() == sample.shouldMatch);

        if(!sample.shouldMatch || !result.has_value()) {
            continue;
        }

        for(const auto [tag, expectedIndex]: sample.tagToIndex) {
            ut::expect(tag >= 0);
            const auto tagIdx = static_cast<std::size_t>(tag);
            ut::expect(tagIdx < result->size());
            if(tagIdx < result->size()) {
                ut::expect((*result)[tagIdx] == expectedIndex);
            }
        }
    }
}

template <testdata::RegexCaseId Id>
void expectSameAsTnfa() {
    constexpr auto tdfaModel = buildTdfaModel<Id>();
    constexpr auto tnfaModel = buildTnfaModel<Id>();
    const auto& spec = testdata::fixture(Id);

    for(const auto& sample: spec.testStrings) {
        const auto tdfaResult = TDFA::simulation(tdfaModel, sample.input);
        const auto tnfaResult = TNFA::simulation(tnfaModel, sample.input);
        ut::expect(tdfaResult == tnfaResult);
    }
}

void registerTdfaTests() {
    using namespace ut;

    "tdfa_runtime_common_patterns"_test = [] {
        expectFixtureRuntime<testdata::RegexCaseId::literal>();
        expectFixtureRuntime<testdata::RegexCaseId::email_like>();
        expectFixtureRuntime<testdata::RegexCaseId::iso_date>();
        expectFixtureRuntime<testdata::RegexCaseId::us_phone>();
        expectFixtureRuntime<testdata::RegexCaseId::path_optional_ext>();
        expectFixtureRuntime<testdata::RegexCaseId::image_ext>();
        expectFixtureRuntime<testdata::RegexCaseId::semver_long>();
        expectFixtureRuntime<testdata::RegexCaseId::iso_datetime_tz_long>();
    };

    "tdfa_runtime_tag_patterns"_test = [] {
        expectFixtureRuntime<testdata::RegexCaseId::tag_basic>();
        expectFixtureRuntime<testdata::RegexCaseId::tag_alt_reset>();
        expectFixtureRuntime<testdata::RegexCaseId::tag_optional_reset>();
        expectFixtureRuntime<testdata::RegexCaseId::tag_star>();
        expectFixtureRuntime<testdata::RegexCaseId::tag_plus>();
        expectFixtureRuntime<testdata::RegexCaseId::tag_repeat_range>();
    };

    "tdfa_runtime_tag_zero_based_index"_test = [] {
        constexpr auto tree = parseToRegexTree<FixedString{R"(\g{0}a\g{-0})"}>();
        static_assert(tree.ok());
        constexpr auto tnfaOpt = TNFA::fromRegexTreeAuto<tree>();
        static_assert(tnfaOpt.has_value());
        constexpr auto tdfaOpt = TDFA::fromTNFAAuto<tnfaOpt.value()>();
        static_assert(tdfaOpt.has_value());
        constexpr auto model = *tdfaOpt;

        const auto result = TDFA::simulation(model, "a");
        ut::expect(result.has_value());
        if(result.has_value()) {
            ut::expect(result->size() == static_cast<std::size_t>(1));
            ut::expect((*result)[0] == -1);
        }
    };

    "tdfa_matches_tnfa_runtime"_test = [] {
        expectSameAsTnfa<testdata::RegexCaseId::literal>();
        expectSameAsTnfa<testdata::RegexCaseId::email_like>();
        expectSameAsTnfa<testdata::RegexCaseId::iso_date>();
        expectSameAsTnfa<testdata::RegexCaseId::us_phone>();
        expectSameAsTnfa<testdata::RegexCaseId::path_optional_ext>();
        expectSameAsTnfa<testdata::RegexCaseId::image_ext>();
        expectSameAsTnfa<testdata::RegexCaseId::semver_long>();
        expectSameAsTnfa<testdata::RegexCaseId::iso_datetime_tz_long>();
        expectSameAsTnfa<testdata::RegexCaseId::tag_basic>();
        expectSameAsTnfa<testdata::RegexCaseId::tag_alt_reset>();
        expectSameAsTnfa<testdata::RegexCaseId::tag_optional_reset>();
        expectSameAsTnfa<testdata::RegexCaseId::tag_star>();
        expectSameAsTnfa<testdata::RegexCaseId::tag_plus>();
        expectSameAsTnfa<testdata::RegexCaseId::tag_repeat_range>();
    };

    "tdfa_runtime_step_api"_test = [] {
        constexpr auto model = buildTdfaModel<testdata::RegexCaseId::literal>();
        TDFA::Runtime runtime(model);

        ut::expect(runtime.reset());
        ut::expect(runtime.step('a'));
        ut::expect(runtime.step('b'));
        ut::expect(runtime.step('c'));
        ut::expect(runtime.position() == 3u);
        ut::expect(runtime.alive());

        const auto result = runtime.finish();
        ut::expect(result.has_value());
        if(result.has_value()) {
            ut::expect(result->empty());
        }
    };
}

}  // namespace etch::tests
