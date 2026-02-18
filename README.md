# Etch

Etch is a C++23 compile-time regex project. It parses regex patterns into a `RegexTree` at compile time, then builds a `TNFA` / `TDFA`, and runs matching at runtime.

The project is currently header-only, with the main public entry in `lib/api.h`.

## Highlights

- Compile-time pipeline: `regex -> RegexTree -> TNFA -> TDFA`
- Unified runtime API: `etch::match(model, input)`
- Two strategies: `etch::Strategy::NFA` and `etch::Strategy::DFA`
- `NFA`: lighter build cost, slower runtime
- `DFA`: heavier build cost, faster runtime (linear scan)
- Unified error types in `lib/ty.h` (`RegexParseError` / `TNFAError` / `TDFAError`)

## Supported Syntax

- Literals and concatenation: `abc`
- Alternation: `a|b`
- Grouping: `(ab)`
- Quantifiers: `*`, `+`, `?`, `{m}`, `{m,}`, `{m,n}`
- Wildcard: `.`
- Common escapes: `\d \D \w \W \s \S \t \n \r \f \v \0 \\`
- Tags: `\g{n}` (positive/negative integers)
- Named character checks: `\x{name}` (requires custom resolver)

## Quick Start

```cpp
#include "api.h"
#include <iostream>

constexpr auto model = etch::regexify<etch::FixedString{R"(\w+@\w+\.\w+)"}>();

int main() {
    if(auto res = etch::match(model, "alice_01@test.com"); res.has_value()) {
        std::cout << "match\n";
    } else {
        std::cout << "no match\n";
    }
}
```

Use the NFA strategy:

```cpp
constexpr auto nfaModel =
    etch::regexify<etch::FixedString{"abc"}, etch::Strategy::NFA>();
```

Match return type is `std::optional<std::vector<int>>`:

- No match: `std::nullopt`
- Match: tag offset vector (usually empty when no tags are used)
- Unset tag value: `-1` (see `TNFA::offset_unset`)

## Build

Requires a C++23 compiler and xmake.

```bash
xmake f -y -m release
```

Build test target only:

```bash
xmake -y -b tnfa_test
```

## Test

```bash
xmake run tnfa_test
```

Test entry is `test/main.cc`, covering parse / TNFA / TDFA tests.

## Reference
[A closer look at TDFA](https://arxiv.org/html/2206.01398v3#S1)