#pragma once

#include "util.h"
#include "str2ex.h"
#include "tnfa.h"
#include "tdfa.h"
#include "ty.h"
#include <iostream>
#include <optional>

namespace etch {
enum Strategy {
    // Less compile-time, less storage, more runtime, more flexible.
    NFA,
    // More compile-time, more storage, less runtime, less flexible, ensure O(length-of-input).
    DFA
};

template <class T>
void printErr(const T& value) {
    switch(T::ty) {
        case EtchTy::RegexTree:
            std::cerr << "RegexTree build error: " << regex::RegexParseErrorToString(value.error)
                      << " at position " << value.errorPos << '\n';
            break;
        case EtchTy::TNFAModel:
            std::cerr << "TNFA build error: " << TNFAErrorToString(value.error) << '\n';
            break;
        case EtchTy::TDFAModel:
            std::cerr << "TDFA build error: " << TDFAErrorToString(value.error) << '\n';
            break;
    }
}

template <FixedString str, Strategy strategy = Strategy::DFA>
consteval auto regexify() {
    constexpr auto tree = parseToRegexTree<str>();
    static_assert(tree.ok(), "failed to parse regex pattern, use");
    constexpr auto TNFAModel = TNFA::fromRegexTreeAuto<tree>();
    static_assert(TNFAModel.has_value(), "compile-time TNFA build failed");
    if constexpr(strategy == Strategy::NFA) {
        return *TNFAModel;
    } else {
        constexpr auto TDFA = TDFA::fromTNFAAuto<TNFAModel.value()>();
        static_assert(TDFA.has_value(), "compile-time TDFA build failed");
        return *TDFA;
    }
}

using TagIndexes = std::vector<int>;

template <class T>
std::optional<TagIndexes> match(const T& model, std::string_view input) {
    switch(T::ty) {
        case EtchTy::TNFAModel: {
            return TNFA::simulation(model, input);
        }
        case EtchTy::TDFAModel: {
            return TDFA::simulation(model, input);
        }
            return std::nullopt;
    }
}

}  // namespace etch
