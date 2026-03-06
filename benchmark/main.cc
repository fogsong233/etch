#include "str2ex.h"
#include "tdfa.h"
#include "tnfa.h"

#include <ctre.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace etch::benchmark {

template <FixedString Pattern>
consteval auto buildTNFA() {
    constexpr auto tree = parseToRegexTree<Pattern>();
    static_assert(tree.ok(), "failed to parse benchmark regex");

    constexpr auto tnfa = TNFA::fromRegexTreeAuto<tree>();
    static_assert(tnfa.has_value(), "failed to build TNFA for benchmark regex");
    return tnfa.value();
}

template <FixedString Pattern>
consteval auto buildTDFA() {
    constexpr auto tnfa = buildTNFA<Pattern>();
    constexpr auto tdfa = TDFA::fromTNFAAuto<tnfa>();
    static_assert(tdfa.has_value(), "failed to build TDFA for benchmark regex");
    return tdfa.value();
}

template <FixedString Pattern>
constexpr auto kTNFA = buildTNFA<Pattern>();

template <FixedString Pattern>
constexpr auto kTDFA = buildTDFA<Pattern>();

template <FixedString Pattern>
auto matchEtchTNFA(std::string_view input) -> bool {
    return TNFA::simulation(kTNFA<Pattern>, input).has_value();
}

template <FixedString Pattern>
auto matchEtchTDFA(std::string_view input) -> bool {
    return TDFA::isMatchStatic<kTDFA<Pattern>>(input);
}

auto matchCtreLiteral(std::string_view input) -> bool {
    return static_cast<bool>(ctre::match<"abc">(input));
}

auto matchCtreEmail(std::string_view input) -> bool {
    return static_cast<bool>(ctre::match<R"(\w+@\w+\.\w+)">(input));
}

auto matchCtreDate(std::string_view input) -> bool {
    return static_cast<bool>(ctre::match<R"(\d{4}-\d{2}-\d{2})">(input));
}

auto matchCtrePath(std::string_view input) -> bool {
    return static_cast<bool>(ctre::match<R"(\w+/\w+(\.\w+)?)">(input));
}

auto matchCtreImage(std::string_view input) -> bool {
    return static_cast<bool>(ctre::match<R"(\w+\.(jpg|png|gif))">(input));
}

auto matchCtreIsoDatetimeTzLong(std::string_view input) -> bool {
    return static_cast<bool>(ctre::match<
        R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|(\+\d{2}:\d{2}|-\d{2}:\d{2})))">(input));
}

auto matchStdRegex(std::string_view input, const std::regex& pattern) -> bool {
    return std::regex_match(input.begin(), input.end(), pattern);
}

auto matchStdLiteral(std::string_view input) -> bool {
    static const auto re =
        std::regex{R"(abc)", std::regex_constants::ECMAScript | std::regex_constants::optimize};
    return matchStdRegex(input, re);
}

auto matchStdEmail(std::string_view input) -> bool {
    static const auto re = std::regex{R"(\w+@\w+\.\w+)",
                                      std::regex_constants::ECMAScript |
                                          std::regex_constants::optimize};
    return matchStdRegex(input, re);
}

auto matchStdDate(std::string_view input) -> bool {
    static const auto re = std::regex{R"(\d{4}-\d{2}-\d{2})",
                                      std::regex_constants::ECMAScript |
                                          std::regex_constants::optimize};
    return matchStdRegex(input, re);
}

auto matchStdPath(std::string_view input) -> bool {
    static const auto re = std::regex{R"(\w+/\w+(\.\w+)?)",
                                      std::regex_constants::ECMAScript |
                                          std::regex_constants::optimize};
    return matchStdRegex(input, re);
}

auto matchStdImage(std::string_view input) -> bool {
    static const auto re = std::regex{R"(\w+\.(jpg|png|gif))",
                                      std::regex_constants::ECMAScript |
                                          std::regex_constants::optimize};
    return matchStdRegex(input, re);
}

auto matchStdIsoDatetimeTzLong(std::string_view input) -> bool {
    static const auto re = std::regex{
        R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|(\+\d{2}:\d{2}|-\d{2}:\d{2})))",
        std::regex_constants::ECMAScript | std::regex_constants::optimize};
    return matchStdRegex(input, re);
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

struct CaseDef {
    std::string_view name{};
    MatchFn etchTnfa{};
    MatchFn etchTdfa{};
    MatchFn ctre{};
    MatchFn stdRegex{};
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
    std::cout << "Regex benchmark (Etch TNFA vs Etch TDFA vs CTRE vs std::regex)\n";
    std::cout << "corpus_size=" << corpusSize << ", target_ops_per_case~" << targetOps << "\n\n";
    std::cout << std::left << std::setw(22) << "case" << std::setw(12) << "engine" << std::setw(12)
              << "ns/op" << std::setw(12) << "Mops/s" << std::setw(14) << "matched" << std::setw(12)
              << "rounds" << '\n';
    std::cout << std::string(82, '-') << '\n';
}

void printLine(std::string_view caseName, std::string_view engine, const BenchResult& r) {
    std::cout << std::left << std::setw(22) << caseName << std::setw(12) << engine << std::setw(12)
              << std::fixed << std::setprecision(2) << toNsPerOp(r) << std::setw(12) << std::fixed
              << std::setprecision(2) << toMops(r) << std::setw(14) << r.matchedCount
              << std::setw(12) << r.rounds << '\n';
}

void runCase(const CaseDef& c, uint64_t targetOps) {
    const uint64_t corpusSize = static_cast<uint64_t>(c.corpus.size());
    const uint64_t rounds = std::max<uint64_t>(1, targetOps / std::max<uint64_t>(1, corpusSize));

    (void)runBenchmark(c.etchTnfa, c.corpus, 1);
    (void)runBenchmark(c.etchTdfa, c.corpus, 1);
    (void)runBenchmark(c.ctre, c.corpus, 1);
    (void)runBenchmark(c.stdRegex, c.corpus, 1);

    const auto etchTnfa = runBenchmark(c.etchTnfa, c.corpus, rounds);
    const auto etchTdfa = runBenchmark(c.etchTdfa, c.corpus, rounds);
    const auto ctre = runBenchmark(c.ctre, c.corpus, rounds);
    const auto stdRegex = runBenchmark(c.stdRegex, c.corpus, rounds);

    printLine(c.name, "etch_tnfa", etchTnfa);
    printLine(c.name, "etch_tdfa", etchTdfa);
    printLine(c.name, "ctre", ctre);
    printLine(c.name, "std_regex", stdRegex);
}

}  // namespace etch::benchmark

int main(int argc, char** argv) {
    using namespace etch::benchmark;

    int argBase = 1;
    if(argc > argBase && std::string_view(argv[argBase]) == "--") {
        ++argBase;
    }

    const uint64_t corpusSize = parseU64(argc > argBase ? argv[argBase] : nullptr, 8192);
    const uint64_t targetOps =
        parseU64(argc > argBase + 1 ? argv[argBase + 1] : nullptr, 5'000'000);

    auto cases = std::vector<CaseDef>{
        CaseDef{"literal_abc",
                &matchEtchTNFA<etch::FixedString{"abc"}>,
                &matchEtchTDFA<etch::FixedString{"abc"}>,
                &matchCtreLiteral,
                &matchStdLiteral,
                makeCorpus(corpusSize, 1, 12, 11, {"abc", "ab", "abcd", "zzzabc"})},
        CaseDef{"iso_datetime_tz_long",
                &matchEtchTNFA<
                    etch::FixedString{
                        R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|(\+\d{2}:\d{2}|-\d{2}:\d{2})))"}>,
                &matchEtchTDFA<
                    etch::FixedString{
                        R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|(\+\d{2}:\d{2}|-\d{2}:\d{2})))"}>,
                &matchCtreIsoDatetimeTzLong,
                &matchStdIsoDatetimeTzLong,
                makeCorpus(corpusSize,
                           1,
                           96,
                           47,
                           {"2024-12-31T23:59:59Z",
                            "1999-01-01T00:00:00+08:00",
                            "2030-06-15T12:30:45-05:30",
                            "2024-12-31 23:59:59Z",
                            "2024-12-31T23:59Z"})},
        CaseDef{"email_like",
                &matchEtchTNFA<etch::FixedString{R"(\w+@\w+\.\w+)"}>,
                &matchEtchTDFA<etch::FixedString{R"(\w+@\w+\.\w+)"}>,
                &matchCtreEmail,
                &matchStdEmail,
                makeCorpus(corpusSize,
                           4,
                           36,
                           13,
                           {"alice_01@test.com", "a@b.c", "alice@test", "alice@@test.com"})},
        CaseDef{"iso_date",
                &matchEtchTNFA<etch::FixedString{R"(\d{4}-\d{2}-\d{2})"}>,
                &matchEtchTDFA<etch::FixedString{R"(\d{4}-\d{2}-\d{2})"}>,
                &matchCtreDate,
                &matchStdDate,
                makeCorpus(corpusSize,
                           6,
                           14,
                           17,
                           {"2024-12-31", "1999-01-01", "2024/12/31", "24-12-31"})},
        CaseDef{"path_opt_ext",
                &matchEtchTNFA<etch::FixedString{R"(\w+/\w+(\.\w+)?)"}>,
                &matchEtchTDFA<etch::FixedString{R"(\w+/\w+(\.\w+)?)"}>,
                &matchCtrePath,
                &matchStdPath,
                makeCorpus(corpusSize, 4, 24, 19, {"src/main.cc", "src/main", "src/", "/main.cc"})},
        CaseDef{"image_ext",
                &matchEtchTNFA<etch::FixedString{R"(\w+\.(jpg|png|gif))"}>,
                &matchEtchTDFA<etch::FixedString{R"(\w+\.(jpg|png|gif))"}>,
                &matchCtreImage,
                &matchStdImage,
                makeCorpus(corpusSize, 4, 24, 23, {"photo.jpg", "icon.png", "anim.gif", "photo.bmp"})},
    };

    printHeader(corpusSize, targetOps);
    for(const auto& c: cases) {
        runCase(c, targetOps);
    }

    return 0;
}
