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

#include "regex.h"
#include "str2ex.h"
#include "ty.h"
#include <array>
struct A {
  int x;
  consteval A(int x) : x(x) {}
  consteval auto run() -> int {
    x = x + 1;
    return x;
  }
};
#include <algorithm>
#include <array>
#include <vector>

struct T : public etch::regex::RecursiveRegexVisitor {
  int t = 0;
  constexpr bool visitCharCheck(int nodeIdx, etch::CharFn checkFn) override {
    t += 1;
    return true;
  }
};

int main(int argc, const char **argv) {
  constexpr auto p = [] {
    auto regex1 = etch::parseToRegexTree<R"(aaa\d{0,}\d)">();
    T a{};
    a.traverse(regex1);
    return a.t;
  }();
  std::array<int, p> u{};
  return 0;
}