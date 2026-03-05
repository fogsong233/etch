#include "str2ex.h"
#include "tdfa.h"
#include "tnfa.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace etch::ablation {

template <FixedString Pattern>
consteval auto buildTNFA() {
    constexpr auto tree = parseToRegexTree<Pattern>();
    static_assert(tree.ok(), "failed to parse benchmark regex");

    constexpr auto tnfa = TNFA::fromRegexTreeAuto<tree>();
    static_assert(tnfa.has_value(), "failed to build TNFA for benchmark regex");
    return tnfa.value();
}

template <FixedString Pattern,
          bool EnableRegOptimize,
          bool EnableStateMinimize,
          bool EnableLengthPrune,
          bool EnableExactLiteralFastpath>
consteval auto buildTDFA() {
    constexpr auto tnfa = buildTNFA<Pattern>();
    constexpr auto tdfa = TDFA::fromTNFAAutoTuned<tnfa,
                                                  EnableRegOptimize,
                                                  EnableStateMinimize,
                                                  EnableLengthPrune,
                                                  EnableExactLiteralFastpath>();
    static_assert(tdfa.has_value(), "failed to build TDFA for benchmark regex");
    return tdfa.value();
}

template <FixedString Pattern>
constexpr auto kTNFA = buildTNFA<Pattern>();

template <FixedString Pattern,
          bool EnableRegOptimize,
          bool EnableStateMinimize,
          bool EnableLengthPrune,
          bool EnableExactLiteralFastpath>
constexpr auto kTDFA =
    buildTDFA<Pattern,
              EnableRegOptimize,
              EnableStateMinimize,
              EnableLengthPrune,
              EnableExactLiteralFastpath>();

template <FixedString Pattern>
auto matchTNFA(std::string_view input) -> bool {
    return TNFA::simulation(kTNFA<Pattern>, input).has_value();
}

template <FixedString Pattern>
auto matchTDFAFull(std::string_view input) -> bool {
    return TDFA::isMatch(kTDFA<Pattern, true, true, true, true>, input);
}

template <FixedString Pattern>
auto matchTDFANoRegOptimize(std::string_view input) -> bool {
    return TDFA::isMatch(kTDFA<Pattern, false, true, true, true>, input);
}

template <FixedString Pattern>
auto matchTDFANoStateMinimize(std::string_view input) -> bool {
    return TDFA::isMatch(kTDFA<Pattern, true, false, true, true>, input);
}

template <FixedString Pattern>
auto matchTDFANoFastpath(std::string_view input) -> bool {
    return TDFA::isMatch(kTDFA<Pattern, true, true, false, false>, input);
}

auto makeRandomCorpus(std::size_t count, std::size_t minLen, std::size_t maxLen, uint32_t seed)
    -> std::vector<std::string> {
    constexpr std::string_view alphabet =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-@/ ";

    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> lenDist(minLen, maxLen);
    std::uniform_int_distribution<std::size_t> charDist(0, alphabet.size() - 1);

    std::vector<std::string> out;
    out.reserve(count);
    for(std::size_t i = 0; i < count; ++i) {
        const auto len = lenDist(rng);
        std::string s;
        s.reserve(len);
        for(std::size_t j = 0; j < len; ++j) {
            s.push_back(alphabet[charDist(rng)]);
        }
        out.push_back(std::move(s));
    }
    return out;
}

auto makeCorpus(std::size_t randomCount,
                std::size_t minLen,
                std::size_t maxLen,
                uint32_t seed,
                std::initializer_list<std::string_view> injected) -> std::vector<std::string> {
    auto corpus = makeRandomCorpus(randomCount, minLen, maxLen, seed);
    corpus.reserve(corpus.size() + injected.size());
    for(const auto sample: injected) {
        corpus.emplace_back(sample);
    }
    return corpus;
}

struct BenchResult {
    uint64_t matchedCount = 0;
    uint64_t elapsedNs = 0;
    uint64_t rounds = 0;
    uint64_t totalOps = 0;
};

struct ModelStats {
    uint16_t states = 0;
    uint16_t regs = 0;
    uint32_t ops = 0;
    uint16_t classes = 0;
    uint32_t minLen = 0;
    uint32_t maxLen = 0;
    bool exactLiteral = false;
};

using MatchFn = auto (*)(std::string_view) -> bool;

auto runBenchmark(MatchFn fn, const std::vector<std::string>& corpus, uint64_t rounds)
    -> BenchResult {
    BenchResult result{};
    result.rounds = rounds;
    result.totalOps = rounds * static_cast<uint64_t>(corpus.size());

    const auto start = std::chrono::steady_clock::now();
    for(uint64_t r = 0; r < rounds; ++r) {
        for(const auto& s: corpus) {
            result.matchedCount += static_cast<uint64_t>(fn(s));
        }
    }
    const auto end = std::chrono::steady_clock::now();

    result.elapsedNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    return result;
}

auto toNsPerOp(const BenchResult& result) -> double {
    if(result.totalOps == 0) {
        return 0.0;
    }
    return static_cast<double>(result.elapsedNs) / static_cast<double>(result.totalOps);
}

auto toMops(const BenchResult& result) -> double {
    if(result.elapsedNs == 0) {
        return 0.0;
    }
    return static_cast<double>(result.totalOps) * 1000.0 / static_cast<double>(result.elapsedNs);
}

template <auto Model>
consteval auto makeModelStats() -> ModelStats {
    return ModelStats{
        .states = Model.stateIdx,
        .regs = Model.regCount,
        .ops = Model.opPoolIdx,
        .classes = Model.classCount,
        .minLen = Model.minInputLen,
        .maxLen = Model.maxInputLen,
        .exactLiteral = Model.isExactLiteral,
    };
}

struct EngineDef {
    std::string_view name{};
    MatchFn fn{};
};

struct CaseDef {
    std::string_view name{};
    std::vector<EngineDef> engines{};
    std::vector<std::string> corpus{};
};

auto parseU64(const char* s, uint64_t fallback) -> uint64_t {
    if(s == nullptr) {
        return fallback;
    }
    char* end = nullptr;
    const auto value = std::strtoull(s, &end, 10);
    if(end == s || *end != '\0') {
        return fallback;
    }
    return value == 0 ? fallback : value;
}

void printHeader(uint64_t corpusSize, uint64_t targetOps) {
    std::cout << "TDFA ablation benchmark (full vs disabled optimizations)\n";
    std::cout << "corpus_size=" << corpusSize << ", target_ops_per_case~" << targetOps << "\n\n";
    std::cout << std::left << std::setw(22) << "case" << std::setw(24) << "engine"
              << std::setw(12) << "ns/op" << std::setw(12) << "Mops/s" << std::setw(14)
              << "matched" << std::setw(12) << "rounds" << std::setw(12)
              << "vs_full" << '\n';
    std::cout << std::string(96, '-') << '\n';
}

void printModelStatsHeader() {
    std::cout << "Model ablation (compile-time materialized model size)\n";
    std::cout << std::left << std::setw(24) << "case" << std::setw(24) << "variant"
              << std::setw(10) << "states" << std::setw(10) << "regs" << std::setw(10) << "ops"
              << std::setw(10) << "classes" << std::setw(10) << "minLen" << std::setw(12)
              << "maxLen" << std::setw(10) << "exactLit" << '\n';
    std::cout << std::string(112, '-') << '\n';
}

void printModelStatLine(std::string_view caseName, std::string_view variant, const ModelStats& s) {
    std::cout << std::left << std::setw(24) << caseName << std::setw(24) << variant
              << std::setw(10) << s.states << std::setw(10) << s.regs << std::setw(10) << s.ops
              << std::setw(10) << s.classes << std::setw(10) << s.minLen << std::setw(12)
              << s.maxLen << std::setw(10) << (s.exactLiteral ? "yes" : "no") << '\n';
}

template <FixedString Pattern>
void printPatternModelStats(std::string_view caseName) {
    constexpr auto fullStats = makeModelStats<kTDFA<Pattern, true, true, true, true>>();
    constexpr auto noRegStats = makeModelStats<kTDFA<Pattern, false, true, true, true>>();
    constexpr auto noStateStats = makeModelStats<kTDFA<Pattern, true, false, true, true>>();
    constexpr auto noFastStats = makeModelStats<kTDFA<Pattern, true, true, false, false>>();

    printModelStatLine(caseName, "tdfa_full", fullStats);
    printModelStatLine(caseName, "tdfa_no_regopt", noRegStats);
    printModelStatLine(caseName, "tdfa_no_state_min", noStateStats);
    printModelStatLine(caseName, "tdfa_no_fastpath", noFastStats);
}

void printLine(std::string_view caseName,
               std::string_view engine,
               const BenchResult& r,
               double fullNsPerOp) {
    const auto nsPerOp = toNsPerOp(r);
    const auto ratio = fullNsPerOp <= 0.0 ? 0.0 : (nsPerOp / fullNsPerOp);
    std::cout << std::left << std::setw(22) << caseName << std::setw(24) << engine << std::setw(12)
              << std::fixed << std::setprecision(2) << nsPerOp << std::setw(12) << std::fixed
              << std::setprecision(2) << toMops(r) << std::setw(14) << r.matchedCount
              << std::setw(12) << r.rounds << std::setw(12) << std::fixed
              << std::setprecision(2) << ratio << '\n';
}

void runCase(const CaseDef& c, uint64_t targetOps) {
    const uint64_t corpusSize = static_cast<uint64_t>(c.corpus.size());
    const uint64_t rounds = std::max<uint64_t>(1, targetOps / std::max<uint64_t>(1, corpusSize));

    for(const auto& engine: c.engines) {
        (void)runBenchmark(engine.fn, c.corpus, 1);
    }

    std::vector<BenchResult> results;
    results.reserve(c.engines.size());
    for(const auto& engine: c.engines) {
        results.push_back(runBenchmark(engine.fn, c.corpus, rounds));
    }

    const auto fullNsPerOp = results.empty() ? 0.0 : toNsPerOp(results.front());

    for(std::size_t i = 0; i < c.engines.size(); ++i) {
        printLine(c.name, c.engines[i].name, results[i], fullNsPerOp);
    }
}

}  // namespace etch::ablation

int main(int argc, char** argv) {
    using namespace etch::ablation;

    int argBase = 1;
    if(argc > argBase && std::string_view(argv[argBase]) == "--") {
        ++argBase;
    }

    const uint64_t corpusSize = parseU64(argc > argBase ? argv[argBase] : nullptr, 8192);
    const uint64_t targetOps =
        parseU64(argc > argBase + 1 ? argv[argBase + 1] : nullptr, 5'000'000);

    auto cases = std::vector<CaseDef>{
        CaseDef{"literal_abc",
                {
                    EngineDef{"etch_tdfa_full", &matchTDFAFull<etch::FixedString{"abc"}>},
                    EngineDef{"etch_tdfa_no_regopt",
                              &matchTDFANoRegOptimize<etch::FixedString{"abc"}>},
                    EngineDef{"etch_tdfa_no_state_min",
                              &matchTDFANoStateMinimize<etch::FixedString{"abc"}>},
                    EngineDef{"etch_tdfa_no_fastpath",
                              &matchTDFANoFastpath<etch::FixedString{"abc"}>},
                    EngineDef{"etch_tnfa", &matchTNFA<etch::FixedString{"abc"}>},
                },
                makeCorpus(corpusSize, 1, 12, 11, {"abc", "ab", "abcd", "zzzabc"})},
        CaseDef{"email_like",
                {
                    EngineDef{"etch_tdfa_full",
                              &matchTDFAFull<etch::FixedString{R"(\w+@\w+\.\w+)"}>},
                    EngineDef{"etch_tdfa_no_regopt",
                              &matchTDFANoRegOptimize<etch::FixedString{R"(\w+@\w+\.\w+)"}>},
                    EngineDef{"etch_tdfa_no_state_min",
                              &matchTDFANoStateMinimize<etch::FixedString{R"(\w+@\w+\.\w+)"}>},
                    EngineDef{"etch_tdfa_no_fastpath",
                              &matchTDFANoFastpath<etch::FixedString{R"(\w+@\w+\.\w+)"}>},
                    EngineDef{"etch_tnfa", &matchTNFA<etch::FixedString{R"(\w+@\w+\.\w+)"}>},
                },
                makeCorpus(corpusSize,
                           4,
                           36,
                           13,
                           {"alice_01@test.com", "a@b.c", "alice@test", "alice@@test.com"})},
        CaseDef{"iso_date",
                {
                    EngineDef{"etch_tdfa_full",
                              &matchTDFAFull<etch::FixedString{R"(\d{4}-\d{2}-\d{2})"}>},
                    EngineDef{"etch_tdfa_no_regopt",
                              &matchTDFANoRegOptimize<etch::FixedString{R"(\d{4}-\d{2}-\d{2})"}>},
                    EngineDef{"etch_tdfa_no_state_min",
                              &matchTDFANoStateMinimize<etch::FixedString{R"(\d{4}-\d{2}-\d{2})"}>},
                    EngineDef{"etch_tdfa_no_fastpath",
                              &matchTDFANoFastpath<etch::FixedString{R"(\d{4}-\d{2}-\d{2})"}>},
                    EngineDef{"etch_tnfa", &matchTNFA<etch::FixedString{R"(\d{4}-\d{2}-\d{2})"}>},
                },
                makeCorpus(corpusSize,
                           6,
                           14,
                           17,
                           {"2024-12-31", "1999-01-01", "2024/12/31", "24-12-31"})},
        CaseDef{"path_opt_ext",
                {
                    EngineDef{"etch_tdfa_full",
                              &matchTDFAFull<etch::FixedString{R"(\w+/\w+(\.\w+)?)"}>},
                    EngineDef{"etch_tdfa_no_regopt",
                              &matchTDFANoRegOptimize<etch::FixedString{R"(\w+/\w+(\.\w+)?)"}>},
                    EngineDef{"etch_tdfa_no_state_min",
                              &matchTDFANoStateMinimize<etch::FixedString{R"(\w+/\w+(\.\w+)?)"}>},
                    EngineDef{"etch_tdfa_no_fastpath",
                              &matchTDFANoFastpath<etch::FixedString{R"(\w+/\w+(\.\w+)?)"}>},
                    EngineDef{"etch_tnfa", &matchTNFA<etch::FixedString{R"(\w+/\w+(\.\w+)?)"}>},
                },
                makeCorpus(corpusSize, 4, 24, 19, {"src/main.cc", "src/main", "src/", "/main.cc"})},
        CaseDef{"image_ext",
                {
                    EngineDef{"etch_tdfa_full",
                              &matchTDFAFull<etch::FixedString{R"(\w+\.(jpg|png|gif))"}>},
                    EngineDef{"etch_tdfa_no_regopt",
                              &matchTDFANoRegOptimize<etch::FixedString{R"(\w+\.(jpg|png|gif))"}>},
                    EngineDef{"etch_tdfa_no_state_min",
                              &matchTDFANoStateMinimize<etch::FixedString{R"(\w+\.(jpg|png|gif))"}>},
                    EngineDef{"etch_tdfa_no_fastpath",
                              &matchTDFANoFastpath<etch::FixedString{R"(\w+\.(jpg|png|gif))"}>},
                    EngineDef{"etch_tnfa", &matchTNFA<etch::FixedString{R"(\w+\.(jpg|png|gif))"}>},
                },
                makeCorpus(corpusSize, 4, 24, 23, {"photo.jpg", "icon.png", "anim.gif", "photo.bmp"})},
        CaseDef{"iso_datetime_tz_long",
                {
                    EngineDef{"etch_tdfa_full",
                              &matchTDFAFull<etch::FixedString{
                                  R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|(\+\d{2}:\d{2}|-\d{2}:\d{2})))"}>},
                    EngineDef{"etch_tdfa_no_regopt",
                              &matchTDFANoRegOptimize<etch::FixedString{
                                  R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|(\+\d{2}:\d{2}|-\d{2}:\d{2})))"}>},
                    EngineDef{"etch_tdfa_no_state_min",
                              &matchTDFANoStateMinimize<etch::FixedString{
                                  R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|(\+\d{2}:\d{2}|-\d{2}:\d{2})))"}>},
                    EngineDef{"etch_tdfa_no_fastpath",
                              &matchTDFANoFastpath<etch::FixedString{
                                  R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|(\+\d{2}:\d{2}|-\d{2}:\d{2})))"}>},
                    EngineDef{"etch_tnfa",
                              &matchTNFA<etch::FixedString{
                                  R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|(\+\d{2}:\d{2}|-\d{2}:\d{2})))"}>},
                },
                makeCorpus(corpusSize,
                           12,
                           40,
                           47,
                           {"2024-12-31T23:59:59Z",
                            "1999-01-01T00:00:00+08:00",
                            "2030-06-15T12:30:45-05:30",
                            "2024-12-31 23:59:59Z",
                            "2024-12-31T23:59Z"})},
        CaseDef{"tag_plus",
                {
                    EngineDef{"etch_tdfa_full",
                              &matchTDFAFull<etch::FixedString{R"((\g{0}a)+)"}>},
                    EngineDef{"etch_tdfa_no_regopt",
                              &matchTDFANoRegOptimize<etch::FixedString{R"((\g{0}a)+)"}>},
                    EngineDef{"etch_tdfa_no_state_min",
                              &matchTDFANoStateMinimize<etch::FixedString{R"((\g{0}a)+)"}>},
                    EngineDef{"etch_tdfa_no_fastpath",
                              &matchTDFANoFastpath<etch::FixedString{R"((\g{0}a)+)"}>},
                    EngineDef{"etch_tnfa", &matchTNFA<etch::FixedString{R"((\g{0}a)+)"}>},
                },
                makeCorpus(corpusSize,
                           1,
                           20,
                           29,
                           {"a", "aa", "aaaa", "b", "ab", "aaaaab"})},
        CaseDef{"tag_alt_reset",
                {
                    EngineDef{"etch_tdfa_full",
                              &matchTDFAFull<etch::FixedString{R"(\g{0}ab|a)"}>},
                    EngineDef{"etch_tdfa_no_regopt",
                              &matchTDFANoRegOptimize<etch::FixedString{R"(\g{0}ab|a)"}>},
                    EngineDef{"etch_tdfa_no_state_min",
                              &matchTDFANoStateMinimize<etch::FixedString{R"(\g{0}ab|a)"}>},
                    EngineDef{"etch_tdfa_no_fastpath",
                              &matchTDFANoFastpath<etch::FixedString{R"(\g{0}ab|a)"}>},
                    EngineDef{"etch_tnfa", &matchTNFA<etch::FixedString{R"(\g{0}ab|a)"}>},
                },
                makeCorpus(corpusSize, 1, 12, 31, {"a", "ab", "b", "aa", "aba"})},
        CaseDef{"tag_repeat_range",
                {
                    EngineDef{"etch_tdfa_full",
                              &matchTDFAFull<etch::FixedString{R"((\g{0}ab){2,3})"}>},
                    EngineDef{"etch_tdfa_no_regopt",
                              &matchTDFANoRegOptimize<etch::FixedString{R"((\g{0}ab){2,3})"}>},
                    EngineDef{"etch_tdfa_no_state_min",
                              &matchTDFANoStateMinimize<etch::FixedString{R"((\g{0}ab){2,3})"}>},
                    EngineDef{"etch_tdfa_no_fastpath",
                              &matchTDFANoFastpath<etch::FixedString{R"((\g{0}ab){2,3})"}>},
                    EngineDef{"etch_tnfa", &matchTNFA<etch::FixedString{R"((\g{0}ab){2,3})"}>},
                },
                makeCorpus(corpusSize, 1, 20, 37, {"abab", "ababab", "ab", "abababab", "aabb"})},
    };

    printModelStatsHeader();
    printPatternModelStats<etch::FixedString{"abc"}>("literal_abc");
    printPatternModelStats<etch::FixedString{R"(\w+@\w+\.\w+)"}>("email_like");
    printPatternModelStats<etch::FixedString{R"(\d{4}-\d{2}-\d{2})"}>("iso_date");
    printPatternModelStats<etch::FixedString{R"(\w+/\w+(\.\w+)?)"}>("path_opt_ext");
    printPatternModelStats<etch::FixedString{R"(\w+\.(jpg|png|gif))"}>("image_ext");
    printPatternModelStats<
        etch::FixedString{
            R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|(\+\d{2}:\d{2}|-\d{2}:\d{2})))"}>(
        "iso_datetime_tz_long");
    printPatternModelStats<etch::FixedString{R"((\g{0}a)+)"}>("tag_plus");
    printPatternModelStats<etch::FixedString{R"(\g{0}ab|a)"}>("tag_alt_reset");
    printPatternModelStats<etch::FixedString{R"((\g{0}ab){2,3})"}>("tag_repeat_range");
    std::cout << '\n';

    printHeader(corpusSize, targetOps);
    for(const auto& c: cases) {
        runCase(c, targetOps);
    }

    return 0;
}
