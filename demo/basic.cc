#include "debug.h"
#include "str2ex.h"
#include "tnfa.h"

#include <cstdio>

int main(int argc, const char **argv) {
  using namespace etch;
  auto res1 = parseToRegexTree<R"(a|b|\g{11x3}|c)">();
  auto res2 = parseToRegexTree<R"(\g{1}abc(\d+|f(1|2{0,1}))\g{2})">();
  dumpRegexTree(res1); // error one
  dumpRegexTree(res2); // good one
}