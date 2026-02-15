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
template <typename L, typename R> struct Concat : Regex {};
template <typename L, typename R> struct Alternation : Regex {};
// Max < 0 means no upper bound.
template <RegexType T, int Min, int Max> struct Repetition : Regex {};
template <CharFn fn> struct CharCheck : Regex {
  bool check(char ch) const { return fn(ch); }
};
template <TagTy tag> struct Tag : Regex {};

enum class RegexTreeNodeKind {
  empty,
  literal,
  char_check,
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

inline const char *RegexParseErrorToString(RegexParseError error) {
  switch (error) {
  case RegexParseError::none:
    return "none";
  case RegexParseError::unexpected_end:
    return "unexpected end of pattern";
  case RegexParseError::unexpected_token:
    return "unexpected token";
  case RegexParseError::expected_right_paren:
    return "expected ')'";
  case RegexParseError::expected_left_brace:
    return "expected '{'";
  case RegexParseError::expected_right_brace:
    return "expected '}'";
  case RegexParseError::invalid_integer:
    return "invalid integer in quantifier";
  case RegexParseError::integer_overflow:
    return "integer overflow in quantifier";
  case RegexParseError::unknown_named_char_check:
    return "unknown named character check";
  case RegexParseError::too_many_nodes:
    return "too many nodes in regex tree";
  default:
    return "unknown error";
  }
}

template <std::size_t MaxNodes> struct RegexTree {
  struct Node {
    RegexTreeNodeKind kind = RegexTreeNodeKind::empty;
    int left = -1;
    int right = -1;
    int minRepeat = 1;
    int maxRepeat = 1; // neg means no upper bound.
    TagTy tag = tag_epsilon;
    char literal = '\0';
    CharFn checkFn = nullptr;
  };

  std::array<Node, MaxNodes> nodes{};
  int root = -1;
  std::size_t size = 0;
  RegexParseError error = RegexParseError::none;
  std::size_t errorPos = 0;

  [[nodiscard]] constexpr bool ok() const {
    return error == RegexParseError::none && root >= 0;
  }
};

struct RecursiveRegexVisitor {
  virtual ~RecursiveRegexVisitor() = default;

  template <std::size_t MaxNodes>
  void traverse(const RegexTree<MaxNodes> &tree, int nodeIdx = -1) {
    int current = nodeIdx;
    if (current < 0 || static_cast<std::size_t>(current) >= tree.size) {
      current = tree.root;
    }
    if (current < 0 || static_cast<std::size_t>(current) >= tree.size) {
      (void)visitInvalidNode(current);
      return;
    }

    const auto &node = tree.nodes[static_cast<std::size_t>(current)];
    switch (node.kind) {
    case RegexTreeNodeKind::empty:
      (void)visitEmpty(current);
      break;
    case RegexTreeNodeKind::literal:
      (void)visitLiteral(current, node.literal);
      break;
    case RegexTreeNodeKind::char_check:
      (void)visitCharCheck(current, node.checkFn);
      break;
    case RegexTreeNodeKind::tag:
      (void)visitTag(current, node.tag);
      break;
    case RegexTreeNodeKind::concat:
      if (visitConcat(current, node.left, node.right)) {
        if (node.left >= 0) {
          traverse(tree, node.left);
        }
        if (node.right >= 0) {
          traverse(tree, node.right);
        }
      }
      break;
    case RegexTreeNodeKind::alternation:
      if (visitAlternation(current, node.left, node.right)) {
        if (node.left >= 0) {
          traverse(tree, node.left);
        }
        if (node.right >= 0) {
          traverse(tree, node.right);
        }
      }
      break;
    case RegexTreeNodeKind::repetition:
      if (visitRepetition(current, node.left, node.minRepeat, node.maxRepeat)) {
        if (node.left >= 0) {
          traverse(tree, node.left);
        }
      }
      break;
    }
  }

protected:
  virtual bool visitInvalidNode(int nodeIdx) { return true; }
  virtual bool visitEmpty(int nodeIdx) { return true; }
  virtual bool visitLiteral(int nodeIdx, char literal) { return true; }
  virtual bool visitCharCheck(int nodeIdx, CharFn checkFn) { return true; }
  virtual bool visitTag(int nodeIdx, TagTy tag) { return true; }
  virtual bool visitConcat(int nodeIdx, int left, int right) { return true; }
  virtual bool visitAlternation(int nodeIdx, int left, int right) {
    return true;
  }
  virtual bool visitRepetition(int nodeIdx, int child, int minRepeat,
                               int maxRepeat) {
    return true;
  }
};
} // namespace etch::regex
