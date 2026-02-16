#pragma once

#include "regex.h"
#include "util.h"

#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace etch {

namespace CharHelper {
template <char C>
constexpr bool isChar(char ch) {
    return ch == C;
}

constexpr bool isDigit(char ch) {
    return ch >= '0' && ch <= '9';
}

constexpr bool isNotDigit(char ch) {
    return !isDigit(ch);
}

constexpr bool isWord(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '_';
}

constexpr bool isNotWord(char ch) {
    return !isWord(ch);
}

constexpr bool isSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

constexpr bool isNotSpace(char ch) {
    return !isSpace(ch);
}

constexpr bool isAnyChar(char) {
    return true;
}

constexpr bool isDecDigit(char ch) {
    return ch >= '0' && ch <= '9';
}
}  // namespace CharHelper

namespace detail {

template <typename T>
constexpr inline bool always_false_v = false;

template <typename ResolverResult>
consteval auto toCharFn(ResolverResult value) -> CharFn {
    using Decayed = std::remove_cvref_t<ResolverResult>;
    if constexpr(std::is_same_v<Decayed, std::optional<CharFn>>) {
        return value.has_value() ? *value : nullptr;
    } else if constexpr(std::is_convertible_v<Decayed, CharFn>) {
        return static_cast<CharFn>(value);
    } else {
        static_assert(always_false_v<Decayed>,
                      "resolver must return CharFn or std::optional<CharFn>");
    }
}

struct NodeCountResult {
    std::size_t nodeCount = 0;
    regex::RegexParseError error = regex::RegexParseError::none;
    std::size_t errorPos = 0;

    [[nodiscard]] consteval bool ok() const {
        return error == regex::RegexParseError::none;
    }
};

struct NoopResolver {
    consteval auto operator() (std::string_view) const -> std::optional<CharFn> {
        return std::nullopt;
    }
};

template <FixedString Pattern, bool Counting, typename NameResolver, std::size_t MaxNodes = 0>
class ConstexprRegexParser {
    constexpr static std::size_t kPatternSize = Pattern.size();
    constexpr static std::size_t kTreeNodes = MaxNodes == 0 ? 1 : MaxNodes;
    constexpr static std::size_t kMaxNameLen = kPatternSize == 0 ? 1 : kPatternSize;

public:
    using ParseResult = std::conditional_t<Counting, NodeCountResult, regex::RegexTree<kTreeNodes>>;

    consteval explicit ConstexprRegexParser(NameResolver resolver) :
        resolver_(std::move(resolver)) {}

    consteval auto parse() -> ParseResult {
        const int root = parseAlternation();
        if(!hasError_ && !eof()) {
            setError(regex::RegexParseError::unexpected_token);
        }
        if constexpr(!Counting) {
            if(!hasError_) {
                result_.root = root;
            }
            if(result_.root < 0 && !hasError_) {
                setError(regex::RegexParseError::unexpected_end);
            }
        }
        return result_;
    }

private:
    NameResolver resolver_;
    ParseResult result_{};
    std::size_t pos_ = 0;
    bool hasError_ = false;

    [[nodiscard]] consteval bool eof() const {
        return pos_ >= kPatternSize;
    }

    [[nodiscard]] consteval char peek() const {
        return eof() ? '\0' : Pattern.data[pos_];
    }

    consteval auto advance() -> char {
        return eof() ? '\0' : Pattern.data[pos_++];
    }

    consteval auto consume(char ch) -> bool {
        if(peek() != ch) {
            return false;
        }
        ++pos_;
        return true;
    }

    consteval void setError(regex::RegexParseError err) {
        if(hasError_) {
            return;
        }
        hasError_ = true;
        result_.error = err;
        result_.errorPos = pos_;
    }

    consteval auto newNode(regex::RegexTreeNodeKind kind) -> int {
        if constexpr(Counting) {
            const int idx = static_cast<int>(result_.nodeCount++);
            (void)kind;
            return idx;
        } else {
            if(result_.size >= kTreeNodes) {
                setError(regex::RegexParseError::too_many_nodes);
                return -1;
            }
            const int idx = static_cast<int>(result_.size++);
            result_.nodes[static_cast<std::size_t>(idx)].kind = kind;
            return idx;
        }
    }

    consteval auto makeEmptyNode() -> int {
        return newNode(regex::RegexTreeNodeKind::empty);
    }

    consteval auto makeLiteralNode(char ch) -> int {
        const int idx = newNode(regex::RegexTreeNodeKind::literal);
        if constexpr(!Counting) {
            if(idx >= 0) {
                result_.nodes[static_cast<std::size_t>(idx)].literal = ch;
            }
        }
        return idx;
    }

    consteval auto makeCharCheckNode(CharFn fn) -> int {
        const int idx = newNode(regex::RegexTreeNodeKind::char_check);
        if constexpr(!Counting) {
            if(idx >= 0) {
                result_.nodes[static_cast<std::size_t>(idx)].checkFn = fn;
            }
        }
        return idx;
    }

    consteval auto makeTagNode(TagTy tag) -> int {
        const int idx = newNode(regex::RegexTreeNodeKind::tag);
        if constexpr(!Counting) {
            if(idx >= 0) {
                result_.nodes[static_cast<std::size_t>(idx)].tag = tag;
            }
        }
        return idx;
    }

    consteval auto makeBinaryNode(regex::RegexTreeNodeKind kind, int lhs, int rhs) -> int {
        const int idx = newNode(kind);
        if constexpr(!Counting) {
            if(idx >= 0) {
                auto& node = result_.nodes[static_cast<std::size_t>(idx)];
                node.left = lhs;
                node.right = rhs;
            }
        }
        return idx;
    }

    consteval auto makeRepetitionNode(int child, int minRepeat, int maxRepeat) -> int {
        const int idx = newNode(regex::RegexTreeNodeKind::repetition);
        if constexpr(!Counting) {
            if(idx >= 0) {
                auto& node = result_.nodes[static_cast<std::size_t>(idx)];
                node.left = child;
                node.minRepeat = minRepeat;
                node.maxRepeat = maxRepeat;
            }
        }
        return idx;
    }

    consteval auto parseAlternation() -> int {
        int node = parseConcatenation();
        if(hasError_) {
            return -1;
        }
        while(consume('|')) {
            int rhs = parseConcatenation();
            if(hasError_) {
                return -1;
            }
            node = makeBinaryNode(regex::RegexTreeNodeKind::alternation, node, rhs);
            if(hasError_) {
                return -1;
            }
        }
        return node;
    }

    consteval auto parseConcatenation() -> int {
        int node = -1;
        bool hasNode = false;

        while(!eof()) {
            const char ch = peek();
            if(ch == ')' || ch == '|') {
                break;
            }
            int part = parseQuantified();
            if(hasError_) {
                return -1;
            }

            if(!hasNode) {
                node = part;
                hasNode = true;
            } else {
                node = makeBinaryNode(regex::RegexTreeNodeKind::concat, node, part);
            }
            if(hasError_) {
                return -1;
            }
        }

        if(!hasNode) {
            return makeEmptyNode();
        }
        return node;
    }

    consteval auto parseQuantified() -> int {
        int node = parseAtom();
        if(hasError_) {
            return -1;
        }

        while(!eof()) {
            if(consume('*')) {
                node = makeRepetitionNode(node, 0, -1);
            } else if(consume('+')) {
                node = makeRepetitionNode(node, 1, -1);
            } else if(consume('?')) {
                node = makeRepetitionNode(node, 0, 1);
            } else if(consume('{')) {
                node = parseBraceQuantifier(node);
            } else {
                break;
            }
            if(hasError_) {
                return -1;
            }
        }
        return node;
    }

    consteval auto parseBraceQuantifier(int node) -> int {
        const int minRepeat = parseNumber(false);
        if(hasError_) {
            return -1;
        }

        int maxRepeat = minRepeat;
        if(consume('}')) {
            return makeRepetitionNode(node, minRepeat, maxRepeat);
        }

        if(!consume(',')) {
            setError(regex::RegexParseError::expected_right_brace);
            return -1;
        }

        if(consume('}')) {
            return makeRepetitionNode(node, minRepeat, -1);
        }

        maxRepeat = parseNumber(false);
        if(hasError_) {
            return -1;
        }
        if(maxRepeat < minRepeat) {
            setError(regex::RegexParseError::unexpected_token);
            return -1;
        }
        if(!consume('}')) {
            setError(regex::RegexParseError::expected_right_brace);
            return -1;
        }
        return makeRepetitionNode(node, minRepeat, maxRepeat);
    }

    // parse basic char, and \...
    consteval auto parseAtom() -> int {
        if(eof()) {
            setError(regex::RegexParseError::unexpected_end);
            return -1;
        }

        if(consume('(')) {
            int node = parseAlternation();
            if(hasError_) {
                return -1;
            }
            if(!consume(')')) {
                setError(regex::RegexParseError::expected_right_paren);
                return -1;
            }
            return node;
        }

        if(consume('\\')) {
            return parseEscape();
        }

        const char ch = advance();
        if(ch == '*' || ch == '+' || ch == '?') {
            setError(regex::RegexParseError::unexpected_token);
            return -1;
        }
        if(ch == '.') {
            return makeCharCheckNode(CharHelper::isAnyChar);
        }
        return makeLiteralNode(ch);
    }

    consteval auto parseEscape() -> int {
        if(eof()) {
            setError(regex::RegexParseError::unexpected_end);
            return -1;
        }

        const char esc = advance();
        switch(esc) {
            case '\\': return makeLiteralNode('\\');
            case 'd': return makeCharCheckNode(CharHelper::isDigit);
            case 'D': return makeCharCheckNode(CharHelper::isNotDigit);
            case 'w': return makeCharCheckNode(CharHelper::isWord);
            case 'W': return makeCharCheckNode(CharHelper::isNotWord);
            case 's': return makeCharCheckNode(CharHelper::isSpace);
            case 'S': return makeCharCheckNode(CharHelper::isNotSpace);
            case 't': return makeLiteralNode('\t');
            case 'n': return makeLiteralNode('\n');
            case 'r': return makeLiteralNode('\r');
            case 'f': return makeLiteralNode('\f');
            case 'v': return makeLiteralNode('\v');
            case '0': return makeLiteralNode('\0');
            case 'g': return parseTagEscape();
            case 'x': return parseNamedCharCheckEscape();
            default: return makeLiteralNode(esc);
        }
    }

    consteval auto parseNumber(bool withSign = true) -> int {
        int sign = 1;
        if(withSign) {
            if(consume('+')) {
                sign = 1;
            } else if(consume('-')) {
                sign = -1;
            }
        }
        if(eof() || !CharHelper::isDecDigit(peek())) {
            setError(regex::RegexParseError::invalid_integer);
            return -1;
        }
        long long value = 0;
        while(!eof() && CharHelper::isDecDigit(peek())) {
            value = value * 10 + (advance() - '0');
            if(value > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
                setError(regex::RegexParseError::integer_overflow);
                return -1;
            }
        }
        return sign * static_cast<int>(value);
    }

    consteval auto parseTagEscape() -> int {
        if(!consume('{')) {
            setError(regex::RegexParseError::expected_left_brace);
            return -1;
        }
        auto value = parseNumber();
        if(hasError_) {
            return -1;
        }
        if(!consume('}')) {
            setError(regex::RegexParseError::expected_right_brace);
            return -1;
        }

        return makeTagNode(static_cast<TagTy>(value));
    }

    consteval auto parseNamedCharCheckEscape() -> int {
        if(!consume('{')) {
            setError(regex::RegexParseError::expected_left_brace);
            return -1;
        }

        std::array<char, kMaxNameLen> nameBuf{};
        std::size_t nameLen = 0;
        while(!eof() && peek() != '}') {
            if(nameLen >= nameBuf.size()) {
                setError(regex::RegexParseError::unexpected_token);
                return -1;
            }
            nameBuf[nameLen++] = advance();
        }

        if(!consume('}')) {
            setError(regex::RegexParseError::expected_right_brace);
            return -1;
        }
        if(nameLen == 0) {
            setError(regex::RegexParseError::unexpected_token);
            return -1;
        }

        const std::string_view name(nameBuf.data(), nameLen);
        if constexpr(Counting) {
            return makeCharCheckNode(nullptr);
        } else {
            const CharFn fn = toCharFn(resolver_(name));
            if(fn == nullptr) {
                setError(regex::RegexParseError::unknown_named_char_check);
                return -1;
            }
            return makeCharCheckNode(fn);
        }
    }
};

}  // namespace detail

template <FixedString Pattern, typename NameResolver>
consteval auto parseToRegexTree(NameResolver resolver) {
    using Counter = detail::ConstexprRegexParser<Pattern, true, detail::NoopResolver>;
    constexpr auto count = Counter(detail::NoopResolver{}).parse();
    constexpr std::size_t exactNodes = count.ok() ? count.nodeCount : 1;
    using Parser =
        detail::ConstexprRegexParser<Pattern, false, std::decay_t<NameResolver>, exactNodes>;
    Parser parser(std::move(resolver));
    auto tree = parser.parse();
    if constexpr(!count.ok()) {
        tree.root = -1;
        tree.size = 0;
        tree.error = count.error;
        tree.errorPos = count.errorPos;
    }
    return tree;
}

template <FixedString Pattern>
consteval auto parseToRegexTree() {
    auto resolver = [](std::string_view) -> std::optional<CharFn> {
        return std::nullopt;
    };
    return parseToRegexTree<Pattern>(resolver);
}

template <FixedString Pattern, typename NameResolver>
consteval auto parseToEx(NameResolver resolver) {
    return parseToRegexTree<Pattern>(std::move(resolver));
}

template <FixedString Pattern>
consteval auto parseToEx() {
    return parseToRegexTree<Pattern>();
}

}  // namespace etch
