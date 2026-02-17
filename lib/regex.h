#pragma once

/**
 * Responsible for parsing regular expressions into tree.
 */
#include "ty.h"

#include <array>
#include <cstddef>

namespace etch::regex {
struct Regex {
    using isRegex = void;
};

template <typename T>
concept RegexType = requires { T::isRegex; };

template <typename L, typename R>
struct Concat : Regex {};

template <typename L, typename R>
struct Alternation : Regex {};

// Max < 0 means no upper bound.
template <RegexType T, int Min, int Max>
struct Repetition : Regex {};

template <CharFn fn>
struct CharCheck : Regex {
    bool check(char ch) const {
        return fn(ch);
    }
};

template <TagTy tag>
struct Tag : Regex {};

enum class RegexTreeNodeKind {
    empty,
    char_range,
    tag,
    concat,
    alternation,
    repetition,
};

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

const inline char* RegexParseErrorToString(RegexParseError error) {
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
        default: return "unknown error";
    }
}

template <std::size_t MaxNodes, std::size_t SplitRangeCnt, std::size_t MaxOwnRange>
struct RegexTree {
    using CharRangeRef = std::array<CharTy, MaxOwnRange>;

    struct Node {
        RegexTreeNodeKind kind = RegexTreeNodeKind::empty;
        int left = -1;
        int right = -1;
        int minRepeat = 1;
        int maxRepeat = 1;  // neg means no upper bound.
        TagTy tag = tag_epsilon;
        CharRangeRef charSupport;
        std::size_t charSupportIdx = 0;
    };

    std::array<Node, MaxNodes> nodes{};
    std::array<CharPair, SplitRangeCnt> splitRanges{};
    std::size_t splitRangeIdx = 0;
    int root = -1;
    std::size_t size = 0;
    RegexParseError error = RegexParseError::none;
    std::size_t errorPos = 0;

    [[nodiscard]] constexpr bool ok() const {
        return error == RegexParseError::none && root >= 0;
    }
};

struct RecursiveRegexVisitor {
    virtual constexpr ~RecursiveRegexVisitor() = default;

    template <class Tree>
    constexpr void traverse(const Tree& tree, int nodeIdx = -1) {
        int current = nodeIdx;
        if(current < 0 || static_cast<std::size_t>(current) >= tree.size) {
            current = tree.root;
        }
        if(current < 0 || static_cast<std::size_t>(current) >= tree.size) {
            (void)visitInvalidNode(current);
            return;
        }

        const auto& node = tree.nodes[static_cast<std::size_t>(current)];
        switch(node.kind) {
            case RegexTreeNodeKind::empty: (void)visitEmpty(current); break;
            case RegexTreeNodeKind::char_range: (void)visitCharRange(current); break;
            case RegexTreeNodeKind::tag: (void)visitTag(current, node.tag); break;
            case RegexTreeNodeKind::concat:
                if(visitConcat(current, node.left, node.right)) {
                    if(node.left >= 0) {
                        traverse(tree, node.left);
                    }
                    if(node.right >= 0) {
                        traverse(tree, node.right);
                    }
                }
                break;
            case RegexTreeNodeKind::alternation:
                if(visitAlternation(current, node.left, node.right)) {
                    if(node.left >= 0) {
                        traverse(tree, node.left);
                    }
                    if(node.right >= 0) {
                        traverse(tree, node.right);
                    }
                }
                break;
            case RegexTreeNodeKind::repetition:
                if(visitRepetition(current, node.left, node.minRepeat, node.maxRepeat)) {
                    if(node.left >= 0) {
                        traverse(tree, node.left);
                    }
                }
                break;
        }
    }

protected:
    virtual constexpr bool visitInvalidNode(int nodeIdx) {
        return true;
    }

    virtual constexpr bool visitEmpty(int nodeIdx) {
        return true;
    }

    virtual constexpr bool visitCharRange(int nodeIdx) {
        return true;
    }

    virtual constexpr bool visitTag(int nodeIdx, TagTy tag) {
        return true;
    }

    virtual constexpr bool visitConcat(int nodeIdx, int left, int right) {
        return true;
    }

    virtual constexpr bool visitAlternation(int nodeIdx, int left, int right) {
        return true;
    }

    virtual constexpr bool visitRepetition(int nodeIdx, int child, int minRepeat, int maxRepeat) {
        return true;
    }
};
}  // namespace etch::regex
