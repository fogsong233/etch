#pragma once

#include "util.h"
#include "str2ex.h"
#include "tnfa.h"
#include "tdfa.h"
#include "ty.h"
#include <cstddef>
#include <iostream>
#include <optional>
#include <string_view>
#include <type_traits>

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

template <class T,
          typename = std::enable_if_t<T::ty == EtchTy::TNFAModel || T::ty == EtchTy::TDFAModel>>
std::optional<TagIndexes> match(const T& model, std::string_view input) {
    if constexpr(T::ty == EtchTy::TNFAModel) {
        return TNFA::simulation(model, input);
    } else if constexpr(T::ty == EtchTy::TDFAModel) {
        return TDFA::simulation(model, input);
    } else {
        static_assert(false, "unsupported model type");
    }
}

inline std::optional<std::string_view> sliceFromTag(std::string_view input,
                                                    int tagIndex1,
                                                    int tagIndex2) {
    const auto maxBoundary = input.size() + 1;
    if(tagIndex1 <= 0 || tagIndex2 <= 0 || static_cast<std::size_t>(tagIndex1) > maxBoundary ||
       static_cast<std::size_t>(tagIndex2) > maxBoundary || tagIndex1 > tagIndex2) {
        return std::nullopt;
    }
    const auto start = static_cast<std::size_t>(tagIndex1 - 1);
    const auto len = static_cast<std::size_t>(tagIndex2 - tagIndex1);
    return input.substr(start, len);
}
}  // namespace etch
