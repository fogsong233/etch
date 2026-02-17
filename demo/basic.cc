// #include "debug.h"
// #include "str2ex.h"
// #include "tnfa.h"

// #include <iostream>
// #include <map>

// int main(int argc, const char **argv) {
//   using namespace etch;
//   constexpr auto invalid = parseToRegexTree<R"(a|b|\g{11x3}|c)">();
//   constexpr auto tree = parseToRegexTree<R"(a\g{1}(a|b|c|d){1,}\g{2})">();
//   constexpr auto modelCt = TNFA::fromRegexTreeAuto<tree>();
//   static_assert(modelCt.has_value(), "compile-time TNFA build failed");

//   dumpRegexTree(invalid);
//   dumpRegexTree(tree);

//   auto model = modelCt;
//   if (!model.has_value()) {
//     std::cout << "failed to build TNFA from tree\n";
//     return 1;
//   }

//   constexpr std::string_view input = "abcd";
//   const auto offsets = TNFA::simulation(*model, input);
//   if (!offsets.has_value()) {
//     std::cout << "no match: " << input << '\n';
//     return 0;
//   }

//   std::cout << "match: " << input << '\n';
//   std::cout << "tag offsets:";
//   for (std::size_t i = 0; i < offsets->size(); ++i) {
//     std::cout << " t" << (i + 1) << "=" << (*offsets)[i];
//   }
//   std::cout << '\n';
//   return 0;
// }

#include "str2ex.h"
#include "tnfa.h"
#include <cassert>

using namespace etch;

template <FixedString Pattern>
constexpr auto kTree = parseToRegexTree<Pattern>();

template <FixedString Pattern>
consteval auto buildModel() {
    static_assert(kTree<Pattern>.ok(), "failed to parse regex pattern");
    constexpr auto model = TNFA::fromRegexTreeAuto<kTree<Pattern>>();
    static_assert(model.has_value(), "compile-time TNFA build failed");
    return *model;
}

template <typename Model>
auto runSimulation(const Model& model, std::string_view input) -> std::optional<std::vector<int>> {
    return TNFA::simulation(model, input);
}

int main(int argc, const char** argv) {
    constexpr auto model = buildModel<R"(\w+/\w+(|\.cc))">();
    auto res = runSimulation(model, "etach/regex.pcc").value();
    return 0;
}
