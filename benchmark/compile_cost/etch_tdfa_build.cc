#include "str2ex.h"
#include "tdfa.h"
#include "tnfa.h"

#include <string_view>

namespace etch::build_cost {

template <FixedString Pattern>
consteval auto buildTDFA() {
    constexpr auto tree = parseToRegexTree<Pattern>();
    static_assert(tree.ok(), "failed to parse regex");

    constexpr auto tnfa = TNFA::fromRegexTreeAuto<tree>();
    static_assert(tnfa.has_value(), "failed to build TNFA");

    constexpr auto tdfa = TDFA::fromTNFAAuto<tnfa.value()>();
    static_assert(tdfa.has_value(), "failed to build TDFA");
    return tdfa.value();
}

template <FixedString Pattern>
constexpr auto kModel = buildTDFA<Pattern>();

auto runSamples(std::string_view input) -> unsigned {
    unsigned matched = 0;
    matched += TDFA::isMatch(kModel<FixedString{"abc"}>, input) ? 1u : 0u;
    matched += TDFA::isMatch(kModel<FixedString{R"(\w+@\w+\.\w+)"}>, input) ? 1u : 0u;
    matched += TDFA::isMatch(kModel<FixedString{R"(\d{4}-\d{2}-\d{2})"}>, input) ? 1u : 0u;
    matched += TDFA::isMatch(kModel<FixedString{R"(\w+/\w+(\.\w+)?)"}>, input) ? 1u : 0u;
    matched += TDFA::isMatch(kModel<FixedString{R"(\w+\.(jpg|png|gif))"}>, input) ? 1u : 0u;
    return matched;
}

}  // namespace etch::build_cost

int main(int argc, char** argv) {
    const std::string_view input =
        argc > 1 ? std::string_view{argv[1]} : std::string_view{"alice@test.com"};
    volatile auto sink = etch::build_cost::runSamples(input);
    (void)sink;
    return 0;
}
