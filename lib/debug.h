#pragma once
#include "printer.h"
#include "regex.h"
#include "ty.h"
#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

namespace etch {

template <typename Tree>
struct Regex2PrintNode : public regex::RecursiveRegexVisitor {
    const Tree& tree;

    using RegexNode = PrintNode<std::string>;
    using ChildRef = typename decltype(RegexNode::children)::value_type;
    std::vector<RegexNode> data;
    std::size_t root = 0;
    std::size_t parent = 0;

    template <typename T = ChildRef>
    void addChild(std::size_t parentIdx, std::size_t childIdx) {
        if constexpr(std::is_pointer_v<T>) {
            data[parentIdx].children.push_back(static_cast<T>(&data[childIdx]));
        } else {
            data[parentIdx].children.push_back(static_cast<T>(childIdx));
        }
    }

    std::size_t newPrintNode(const std::string& content) {
        const std::size_t idx = data.size();
        data.push_back(RegexNode{content, {}});
        return idx;
    }

    Regex2PrintNode(const Tree& tree) : RecursiveRegexVisitor(), tree(tree) {
        data.reserve(32);
        data.push_back(RegexNode{"[RegexTree]", {}});
        root = 0;
        parent = root;
        if(tree.ok()) {
            traverse(tree);
        }
    }

    bool visitEmpty(int nodeIdx) final {
        const auto idx = newPrintNode("empty");
        addChild(parent, idx);
        return true;
    }

    bool visitInvalidNode(int nodeIdx) final {
        const auto idx = newPrintNode("invalid node in index " + std::to_string(nodeIdx));
        addChild(parent, idx);
        return true;
    }

    bool visitCharRange(int nodeIdx) final {
        std::string content = "char ranges: [";
        const auto& node = tree.nodes[static_cast<std::size_t>(nodeIdx)];
        for(std::size_t i = 0; i < node.charSupportIdx; ++i) {
            const auto splitIdx = static_cast<std::size_t>(node.charSupport[i]);
            if(splitIdx >= tree.splitRangeIdx) {
                continue;
            }
            const auto range = tree.splitRanges[splitIdx];
            if(!content.empty() && content.back() != '[') {
                content += ", ";
            }
            content += std::to_string(range.first) + "('" + static_cast<char>(range.first) + "')-" +
                       std::to_string(range.second) + "('" + static_cast<char>(range.second) + "')";
        }
        content += "]";
        const auto idx = newPrintNode(content);
        addChild(parent, idx);
        return true;
    }

    bool visitTag(int nodeIdx, TagTy tag) final {
        const auto idx = newPrintNode("tag " + std::to_string(tag));
        addChild(parent, idx);
        return true;
    }

    bool visitConcat(int nodeIdx, int left, int right) final {
        const auto idx = newPrintNode("concat");
        addChild(parent, idx);
        const auto prevParent = parent;
        parent = idx;
        if(left >= 0) {
            traverse(tree, left);
        }
        if(right >= 0) {
            traverse(tree, right);
        }
        parent = prevParent;
        return false;
    }

    bool visitAlternation(int nodeIdx, int left, int right) final {
        const auto idx = newPrintNode("alternation");
        addChild(parent, idx);
        const auto prevParent = parent;
        parent = idx;
        if(left >= 0) {
            traverse(tree, left);
        }
        if(right >= 0) {
            traverse(tree, right);
        }
        parent = prevParent;
        return false;
    }

    bool visitRepetition(int nodeIdx, int child, int minRepeat, int maxRepeat) final {
        const auto idx = newPrintNode("repetition {" + std::to_string(minRepeat) + "," +
                                      (maxRepeat < 0 ? "inf" : std::to_string(maxRepeat)) + "}");
        addChild(parent, idx);
        const auto prevParent = parent;
        parent = idx;
        if(child >= 0) {
            traverse(tree, child);
        }
        parent = prevParent;
        return false;
    }

    void print(std::ostream& o) const {
        if(tree.ok()) {
            data[root].print_self(o);
        } else {
            o << "Failed to parse regex tree: " << regex::RegexParseErrorToString(tree.error)
              << " at position " << tree.errorPos << '\n';
        }
    }
};

template <typename Tree>
void dumpRegexTree(const Tree& tree, std::ostream& o = std::cout) {
    Regex2PrintNode printer(tree);
    printer.print(o);
}
}  // namespace etch
