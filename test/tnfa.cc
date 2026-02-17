#include "data.h"

#include "../lib/str2ex.h"
#include "../lib/tnfa.h"

#include <boost/ut.hpp>

#include <optional>
#include <vector>

namespace etch::tests {

namespace ut = boost::ut;

template <testdata::RegexCaseId Id>
consteval auto buildModel() {
    constexpr auto tree = parseToRegexTree<testdata::kRegexPattern<Id>>();
    static_assert(tree.ok(), "failed to parse regex pattern");
    constexpr auto model = TNFA::fromRegexTreeAuto<tree>();
    static_assert(model.has_value(), "compile-time TNFA build failed");
    return *model;
}

template <typename Model>
auto runSimulation(const Model& model, std::string_view input) -> std::optional<std::vector<int>> {
    return TNFA::simulation(model, input);
}

template <testdata::RegexCaseId Id>
void expectFixtureRuntime() {
    constexpr auto model = buildModel<Id>();
    constexpr auto patternView = testdata::regexPatternView<Id>();

    const auto& spec = testdata::fixture(Id);
    ut::expect(spec.regex == patternView);

    for(const auto& sample: spec.testStrings) {
        const auto result = runSimulation(model, sample.input);
        ut::expect(result.has_value() == sample.shouldMatch);

        if(!sample.shouldMatch || !result.has_value()) {
            continue;
        }

        for(const auto [tag, expectedIndex]: sample.tagToIndex) {
            ut::expect(tag > 0);
            const auto tagIdx = static_cast<std::size_t>(tag - 1);
            ut::expect(tagIdx < result->size());
            if(tagIdx < result->size()) {
                ut::expect((*result)[tagIdx] == expectedIndex);
            }
        }
    }
}

template <class Builder>
void expectBuildError(Builder& builder, typename Builder::BuildError err) {
    builder.build();
    ut::expect(builder.error() == err);
}

void registerTnfaTests() {
    using namespace ut;

    "tnfa_runtime_common_patterns"_test = [] {
        expectFixtureRuntime<testdata::RegexCaseId::literal>();
        expectFixtureRuntime<testdata::RegexCaseId::email_like>();
        expectFixtureRuntime<testdata::RegexCaseId::iso_date>();
        expectFixtureRuntime<testdata::RegexCaseId::us_phone>();
        expectFixtureRuntime<testdata::RegexCaseId::path_optional_ext>();
        expectFixtureRuntime<testdata::RegexCaseId::image_ext>();
    };

    "tnfa_runtime_tag_patterns"_test = [] {
        expectFixtureRuntime<testdata::RegexCaseId::tag_basic>();
        expectFixtureRuntime<testdata::RegexCaseId::tag_alt_reset>();
        expectFixtureRuntime<testdata::RegexCaseId::tag_optional_reset>();
        expectFixtureRuntime<testdata::RegexCaseId::tag_star>();
        expectFixtureRuntime<testdata::RegexCaseId::tag_plus>();
        expectFixtureRuntime<testdata::RegexCaseId::tag_repeat_range>();
    };

    "tnfa_builder_errors"_test = [] {
        {
            constexpr auto tree = parseToRegexTree<
                testdata::kParseErrorPattern<testdata::ParseErrorCaseId::missing_right_paren>>();
            using Builder = TNFA::TNFABuilder<decltype(tree), false, 8, 1, 2, 8, 1>;
            Builder builder(tree);
            expectBuildError(builder, Builder::BuildError::invalid_state);
        }

        {
            constexpr auto tree =
                parseToRegexTree<testdata::kRegexPattern<testdata::RegexCaseId::literal>>();
            using Builder = TNFA::TNFABuilder<decltype(tree), false, 2, 1, 2, 8, 1>;
            Builder builder(tree);
            expectBuildError(builder, Builder::BuildError::state_capacity_exceeded);
        }

        {
            constexpr auto tree = parseToRegexTree<testdata::kBuilderEpsilonPattern>();
            using Builder = TNFA::TNFABuilder<decltype(tree), false, 32, 1, 1, 8, 1>;
            Builder builder(tree);
            expectBuildError(builder, Builder::BuildError::epsilon_capacity_exceeded);
        }

        {
            constexpr auto tree = parseToRegexTree<testdata::kBuilderSplitRangePattern>();
            using Builder = TNFA::TNFABuilder<decltype(tree), false, 32, 1, 2, 1, 4>;
            Builder builder(tree);
            expectBuildError(builder, Builder::BuildError::split_range_capacity_exceeded);
        }

        {
            constexpr auto baseTree = parseToRegexTree<testdata::kBuilderCharSupportPattern>();
            auto invalidCharSupportTree = baseTree;
            invalidCharSupportTree.root = 0;
            invalidCharSupportTree.size = 1;
            invalidCharSupportTree.error = regex::RegexParseError::none;
            invalidCharSupportTree.nodes[0].kind = regex::RegexTreeNodeKind::char_range;
            invalidCharSupportTree.nodes[0].charSupportIdx = 5;

            using Builder = TNFA::TNFABuilder<decltype(baseTree), false, 8, 1, 2, 4, 4>;
            Builder builder(invalidCharSupportTree);
            expectBuildError(builder, Builder::BuildError::transition_capacity_exceeded);
        }

        {
            constexpr auto baseTree =
                parseToRegexTree<testdata::kBuilderInvalidRepeatBasePattern>();
            auto invalidRepeatTree = baseTree;
            invalidRepeatTree.root = 0;
            invalidRepeatTree.size = 1;
            invalidRepeatTree.error = regex::RegexParseError::none;
            invalidRepeatTree.nodes[0].kind = regex::RegexTreeNodeKind::repetition;
            invalidRepeatTree.nodes[0].left = 0;
            invalidRepeatTree.nodes[0].right = -1;
            invalidRepeatTree.nodes[0].minRepeat = 3;
            invalidRepeatTree.nodes[0].maxRepeat = 2;

            using Builder = TNFA::TNFABuilder<decltype(baseTree), false, 16, 1, 2, 8, 1>;
            Builder builder(invalidRepeatTree);
            expectBuildError(builder, Builder::BuildError::invalid_repetition);
        }
    };
}

}  // namespace etch::tests
