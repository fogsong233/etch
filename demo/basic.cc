#include "api.h"
#include <print>

constexpr auto model = etch::regexify<R"(\g{0}\w+\g{1}@\g{2}\w+\g{3}\.\g{4}\w+\g{5})">();

int main() {
    static_assert(model.ty == etch::EtchTy::TDFAModel, "Expected a TDFA model");
    constexpr auto testInput = "alice_01@test.com";
    if(auto res = etch::match(model, testInput); res.has_value()) {
        auto tags = *res;
        std::println("matched with name {}",
                     etch::sliceFromTag(testInput, tags[0], tags[1]).value_or("<invalid>"));
        std::println("matched with host {}",
                     etch::sliceFromTag(testInput, tags[2], tags[3]).value_or("<invalid>"));
        std::println("matched with domain {}",
                     etch::sliceFromTag(testInput, tags[4], tags[5]).value_or("<invalid>"));
    } else {
        std::cout << "no match\n";
    }
}
