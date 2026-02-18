#pragma once

#include <bitset>
#include <cstdint>
#include <utility>

namespace etch {
using StateTy = unsigned;
using CharFn = auto (*)(char) -> bool;
using TagTy = int;
constexpr TagTy tagEpsilon = 0;
using CharRange = std::bitset<256>;
using CharTy = uint8_t;
using CharPair = std::pair<CharTy, CharTy>;

enum class EtchTy { RegexTree, TNFAModel, TDFAModel };

enum class TNFAError {
    none,
    state_capacity_exceeded,
    invalid_state,
    transition_capacity_exceeded,
    epsilon_capacity_exceeded,
    split_range_capacity_exceeded,
    invalid_repetition,
};

enum class TDFAError {
    none,
    invalid_tnfa,
    state_capacity_exceeded,
    register_capacity_exceeded,
    op_capacity_exceeded,
    tag_capacity_exceeded,
    class_capacity_exceeded,
};

[[nodiscard]] constexpr auto TNFAErrorToString(TNFAError err) -> const char* {
    switch(err) {
        case TNFAError::none: return "none";
        case TNFAError::state_capacity_exceeded: return "state capacity exceeded";
        case TNFAError::invalid_state: return "invalid state index";
        case TNFAError::transition_capacity_exceeded: return "char transition capacity exceeded";
        case TNFAError::epsilon_capacity_exceeded: return "epsilon transition capacity exceeded";
        case TNFAError::split_range_capacity_exceeded: return "split range capacity exceeded";
        case TNFAError::invalid_repetition: return "invalid repetition bounds";
    }
    return "unknown";
}

[[nodiscard]] constexpr auto TDFAErrorToString(TDFAError err) -> const char* {
    switch(err) {
        case TDFAError::none: return "none";
        case TDFAError::invalid_tnfa: return "invalid tnfa model";
        case TDFAError::state_capacity_exceeded: return "tdfa state capacity exceeded";
        case TDFAError::register_capacity_exceeded: return "tdfa register capacity exceeded";
        case TDFAError::op_capacity_exceeded: return "tdfa operation capacity exceeded";
        case TDFAError::tag_capacity_exceeded: return "tdfa tag capacity exceeded";
        case TDFAError::class_capacity_exceeded: return "tdfa class capacity exceeded";
    }
    return "unknown";
}
}  // namespace etch

namespace etch::regex {
enum class RegexParseError {
    none,
    unexpected_end,
    unexpected_token,
    expected_right_paren,
    expected_left_brace,
    expected_right_brace,
    invalid_integer,
    integer_overflow,
    unknown_named_char_check,
    too_many_nodes,
};

[[nodiscard]] constexpr auto RegexParseErrorToString(RegexParseError error) -> const char* {
    switch(error) {
        case RegexParseError::none: return "none";
        case RegexParseError::unexpected_end: return "unexpected end of pattern";
        case RegexParseError::unexpected_token: return "unexpected token";
        case RegexParseError::expected_right_paren: return "expected ')'";
        case RegexParseError::expected_left_brace: return "expected '{'";
        case RegexParseError::expected_right_brace: return "expected '}'";
        case RegexParseError::invalid_integer: return "invalid integer in quantifier";
        case RegexParseError::integer_overflow: return "integer overflow in quantifier";
        case RegexParseError::unknown_named_char_check: return "unknown named character check";
        case RegexParseError::too_many_nodes: return "too many nodes in regex tree";
    }
    return "unknown";
}
}  // namespace etch::regex
