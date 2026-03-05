#include <ctre.hpp>

#include <string_view>

namespace etch::build_cost {

auto runSamples(std::string_view input) -> unsigned {
    unsigned matched = 0;
    matched += static_cast<unsigned>(static_cast<bool>(ctre::match<"abc">(input)));
    matched += static_cast<unsigned>(static_cast<bool>(ctre::match<R"(\w+@\w+\.\w+)">(input)));
    matched += static_cast<unsigned>(static_cast<bool>(ctre::match<R"(\d{4}-\d{2}-\d{2})">(input)));
    matched += static_cast<unsigned>(static_cast<bool>(ctre::match<R"(\w+/\w+(\.\w+)?)">(input)));
    matched += static_cast<unsigned>(static_cast<bool>(ctre::match<R"(\w+\.(jpg|png|gif))">(input)));
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
