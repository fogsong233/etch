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
- Tags: `\g{n}` (0-based index; e.g. `\g{0}` maps to `res[0]`, negative resets are also supported)
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

## Benchmark
```
Regex benchmark (Etch TNFA vs Etch TDFA vs CTRE)
corpus_size=2048, target_ops_per_case~800000

case                  engine      ns/op       Mops/s      matched       rounds      
----------------------------------------------------------------------------------
literal_abc           etch_tnfa   81.63       12.25       389           389         
literal_abc           etch_tdfa   0.81        1236.04     389           389         
literal_abc           ctre        0.81        1231.70     389           389         
email_like            etch_tnfa   984.17      1.02        5057          389         
email_like            etch_tdfa   9.69        103.22      5057          389         
email_like            ctre        15.67       63.81       5057          389         
iso_date              etch_tnfa   94.44       10.59       778           389         
iso_date              etch_tdfa   0.83        1200.06     778           389         
iso_date              ctre        1.32        757.82      778           389         
path_opt_ext          etch_tnfa   833.65      1.20        48236         389         
path_opt_ext          etch_tdfa   7.51        133.11      48236         389         
path_opt_ext          ctre        13.39       74.66       48236         389         
image_ext             etch_tnfa   807.05      1.24        1167          389         
image_ext             etch_tdfa   7.09        141.12      1167          389         
image_ext             ctre        10.80       92.55       1167          389    
```

## Reference
[A closer look at TDFA](https://arxiv.org/html/2206.01398v3#S1)
