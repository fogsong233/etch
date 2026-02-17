#include "data.h"

#include "../lib/str2ex.h"

#include <boost/ut.hpp>

namespace etch::tests {

namespace ut = boost::ut;

template <testdata::RegexCaseId Id>
void expectFixtureParseOk() {
    constexpr auto tree = parseToRegexTree<testdata::kRegexPattern<Id>>();
    constexpr auto patternView = testdata::regexPatternView<Id>();
    const auto& spec = testdata::fixture(Id);

    ut::expect(spec.regex == patternView);
    ut::expect(tree.ok());
}

template <testdata::ParseErrorCaseId Id>
void expectFixtureParseError() {
    constexpr auto tree = parseToRegexTree<testdata::kParseErrorPattern<Id>>();
    constexpr auto patternView = testdata::parseErrorPatternView<Id>();
    const auto& spec = testdata::fixture(Id);

    ut::expect(spec.regex == patternView);
    ut::expect(!tree.ok());
    ut::expect(tree.error == spec.expectedError);
}

void registerParseTests() {
    using namespace ut;

    "regex_tree_parse_common_patterns"_test = [] {
        expectFixtureParseOk<testdata::RegexCaseId::literal>();
        expectFixtureParseOk<testdata::RegexCaseId::email_like>();
        expectFixtureParseOk<testdata::RegexCaseId::iso_date>();
        expectFixtureParseOk<testdata::RegexCaseId::us_phone>();
        expectFixtureParseOk<testdata::RegexCaseId::path_optional_ext>();
        expectFixtureParseOk<testdata::RegexCaseId::image_ext>();
    };

    "regex_tree_parse_tag_patterns"_test = [] {
        expectFixtureParseOk<testdata::RegexCaseId::tag_basic>();
        expectFixtureParseOk<testdata::RegexCaseId::tag_alt_reset>();
        expectFixtureParseOk<testdata::RegexCaseId::tag_optional_reset>();
        expectFixtureParseOk<testdata::RegexCaseId::tag_star>();
        expectFixtureParseOk<testdata::RegexCaseId::tag_plus>();
        expectFixtureParseOk<testdata::RegexCaseId::tag_repeat_range>();
    };

    "regex_tree_parse_errors"_test = [] {
        expectFixtureParseError<testdata::ParseErrorCaseId::dangling_escape>();
        expectFixtureParseError<testdata::ParseErrorCaseId::missing_right_paren>();
        expectFixtureParseError<testdata::ParseErrorCaseId::tag_missing_left_brace>();
        expectFixtureParseError<testdata::ParseErrorCaseId::tag_missing_right_brace>();
        expectFixtureParseError<testdata::ParseErrorCaseId::invalid_integer>();
        expectFixtureParseError<testdata::ParseErrorCaseId::integer_overflow>();
        expectFixtureParseError<testdata::ParseErrorCaseId::unknown_named_char_check>();
        expectFixtureParseError<testdata::ParseErrorCaseId::invalid_range_quantifier>();
    };

    "regex_tree_parse_too_many_nodes"_test = [] {
        using TinyTree = regex::RegexTree<1, 1, 1>;
        using TinyParser = detail::ConstexprRegexParser<testdata::kParseTooManyNodesPattern,
                                                        false,
                                                        detail::NoopResolver,
                                                        TinyTree>;
        constexpr auto tinyTree = TinyParser(detail::NoopResolver{}).parse();

        ut::expect(!tinyTree.ok());
        ut::expect(tinyTree.error == regex::RegexParseError::too_many_nodes);
    };
}

}  // namespace etch::tests
