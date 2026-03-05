#include <regex>
#include <string_view>

namespace etch::build_cost {

auto match(std::string_view s, const std::regex& re) -> bool {
    return std::regex_match(s.begin(), s.end(), re);
}

auto runSamples(std::string_view input) -> unsigned {
    static const auto kLiteral =
        std::regex{R"(abc)", std::regex_constants::ECMAScript | std::regex_constants::optimize};
    static const auto kEmail = std::regex{R"(\w+@\w+\.\w+)",
                                          std::regex_constants::ECMAScript |
                                              std::regex_constants::optimize};
    static const auto kDate = std::regex{R"(\d{4}-\d{2}-\d{2})",
                                         std::regex_constants::ECMAScript |
                                             std::regex_constants::optimize};
    static const auto kPath = std::regex{R"(\w+/\w+(\.\w+)?)",
                                         std::regex_constants::ECMAScript |
                                             std::regex_constants::optimize};
    static const auto kImage = std::regex{R"(\w+\.(jpg|png|gif))",
                                          std::regex_constants::ECMAScript |
                                              std::regex_constants::optimize};

    unsigned matched = 0;
    matched += match(input, kLiteral) ? 1u : 0u;
    matched += match(input, kEmail) ? 1u : 0u;
    matched += match(input, kDate) ? 1u : 0u;
    matched += match(input, kPath) ? 1u : 0u;
    matched += match(input, kImage) ? 1u : 0u;
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
