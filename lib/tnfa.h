#pragma once

/**
 * This file defines the TNFA data structure and runtime simulation.
 */

#include "regex.h"
#include "ty.h"
#include "util.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace etch::TNFA {

constexpr inline int offset_unset = -1;

struct RuntimeConfiguration {
    StateTy state{};
    std::vector<int> offsets{};
};

template <unsigned StateN, unsigned MaxConn>
struct Transition {
    struct CharStep {
        StateTy another = 0;

        union {
            CharFn charFn = nullptr;
            char literal;
        };

        bool isLiteral = false;
        bool isEnabled = false;
    };

    struct EpsilonStep {
        StateTy another = 0;
        TagTy tag = tag_epsilon;
        int priority = 0;
        bool isEnabled = false;
    };

    struct Steps {
        std::array<CharStep, MaxConn> charSteps{};
        std::array<EpsilonStep, MaxConn> epsilonSteps{};
    };

    std::array<Steps, StateN> toMap{};
    std::array<Steps, StateN> fromMap{};

private:
    constexpr static bool addCharStep(Steps& steps, StateTy another, CharFn charFn) {
        for(auto& edge: steps.charSteps) {
            if(edge.isEnabled) {
                continue;
            }
            edge.another = another;
            edge.charFn = charFn;
            edge.isLiteral = false;
            edge.isEnabled = true;
            return true;
        }
        return false;
    }

    constexpr static bool addLiteralStep(Steps& steps, StateTy another, char literal) {
        for(auto& edge: steps.charSteps) {
            if(edge.isEnabled) {
                continue;
            }
            edge.another = another;
            edge.literal = literal;
            edge.isLiteral = true;
            edge.isEnabled = true;
            return true;
        }
        return false;
    }

    constexpr static void sortEpsilonSteps(Steps& steps) {
        // Keep enabled epsilon transitions in ascending priority order.
        if constexpr(MaxConn < 2) {
            return;
        }
        for(unsigned i = 0; i + 1 < MaxConn; ++i) {
            for(unsigned j = 0; j + 1 < MaxConn - i; ++j) {
                const auto left = steps.epsilonSteps[j];
                const auto right = steps.epsilonSteps[j + 1];
                const bool shouldSwap =
                    (!left.isEnabled && right.isEnabled) ||
                    (left.isEnabled && right.isEnabled && left.priority > right.priority);
                if(!shouldSwap) {
                    continue;
                }
                steps.epsilonSteps[j] = right;
                steps.epsilonSteps[j + 1] = left;
            }
        }
    }

    constexpr static bool addEpsilonStep(Steps& steps, StateTy another, TagTy tag, int priority) {
        for(auto& edge: steps.epsilonSteps) {
            if(edge.isEnabled) {
                continue;
            }
            edge.another = another;
            edge.tag = tag;
            edge.priority = priority;
            edge.isEnabled = true;
            sortEpsilonSteps(steps);
            return true;
        }
        return false;
    }

public:
    constexpr bool addTransition(StateTy from, StateTy to, CharFn charFn) {
        if(from >= StateN || to >= StateN || charFn == nullptr) {
            return false;
        }
        if(!addCharStep(toMap[from], to, charFn)) {
            return false;
        }
        // Reverse index is best-effort and not used by simulation.
        (void)addCharStep(fromMap[to], from, charFn);
        return true;
    }

    constexpr bool addLiteralTransition(StateTy from, StateTy to, char literal) {
        if(from >= StateN || to >= StateN) {
            return false;
        }
        if(!addLiteralStep(toMap[from], to, literal)) {
            return false;
        }
        // Reverse index is best-effort and not used by simulation.
        (void)addLiteralStep(fromMap[to], from, literal);
        return true;
    }

    constexpr bool
        addEpsilonTransition(StateTy from, StateTy to, TagTy tag = tag_epsilon, int priority = 0) {
        if(from >= StateN || to >= StateN) {
            return false;
        }
        if(!addEpsilonStep(toMap[from], to, tag, priority)) {
            return false;
        }
        // Reverse index is best-effort and not used by simulation.
        (void)addEpsilonStep(fromMap[to], from, tag, priority);
        return true;
    }

    constexpr void sortPriority() {
        for(auto& steps: toMap) {
            sortEpsilonSteps(steps);
        }
        for(auto& steps: fromMap) {
            sortEpsilonSteps(steps);
        }
    }
};

template <unsigned StateN, unsigned MaxConn>
struct TNFAModel {
    StateTy initialState{};
    StateTy finalState{};
    Transition<StateN, MaxConn> transition{};
};

inline void applyTagUpdate(std::vector<int>& offsets, TagTy tag, int position, int unsetValue) {
    if(tag == tag_epsilon) {
        return;
    }

    const auto absTag = static_cast<std::size_t>(tag > 0 ? tag : -tag);
    if(absTag == 0 || absTag > offsets.size()) {
        return;
    }

    const auto idx = absTag - 1;
    if(tag > 0) {
        offsets[idx] = position;
    } else {
        offsets[idx] = unsetValue;
    }
}

template <unsigned StateN, unsigned MaxConn>
[[nodiscard]] constexpr bool hasSymbolTransition(const Transition<StateN, MaxConn>& transition,
                                                 StateTy state) {
    if(state >= StateN) {
        return false;
    }

    for(const auto& edge: transition.toMap[state].charSteps) {
        if(edge.isEnabled) {
            return true;
        }
    }
    return false;
}

template <unsigned StateN, unsigned MaxConn>
[[nodiscard]] constexpr std::size_t countTags(const Transition<StateN, MaxConn>& transition) {
    std::size_t maxTag = 0;
    for(const auto& steps: transition.toMap) {
        for(const auto& edge: steps.epsilonSteps) {
            if(!edge.isEnabled || edge.tag == tag_epsilon) {
                continue;
            }
            const auto absTag = static_cast<std::size_t>(edge.tag > 0 ? edge.tag : -edge.tag);
            if(absTag > maxTag) {
                maxTag = absTag;
            }
        }
    }
    return maxTag;
}

template <unsigned StateN, unsigned MaxConn>
[[nodiscard]] auto stepOnSymbol(const std::vector<RuntimeConfiguration>& configs,
                                const Transition<StateN, MaxConn>& transition,
                                char symbol) -> std::vector<RuntimeConfiguration> {
    std::vector<RuntimeConfiguration> next;
    next.reserve(configs.size());

    for(const auto& cfg: configs) {
        if(cfg.state >= StateN) {
            continue;
        }

        for(const auto& edge: transition.toMap[cfg.state].charSteps) {
            if(!edge.isEnabled) {
                continue;
            }
            bool matched = false;
            if(edge.isLiteral) {
                matched = (edge.literal == symbol);
            } else {
                if(edge.charFn == nullptr) {
                    continue;
                }
                matched = edge.charFn(symbol);
            }
            if(!matched) {
                continue;
            }

            // we found a valid transition, create a new configuration for the next
            // step.
            auto moved = cfg;
            moved.state = edge.another;
            next.push_back(std::move(moved));
        }
    }

    return next;
}

template <unsigned StateN, unsigned MaxConn>
[[nodiscard]] auto epsilonClosure(const std::vector<RuntimeConfiguration>& configs,
                                  const Transition<StateN, MaxConn>& transition,
                                  StateTy finalState,
                                  int position,
                                  int unsetValue = offset_unset)
    -> std::vector<RuntimeConfiguration> {
    std::vector<RuntimeConfiguration> stack;
    stack.reserve(configs.size() + StateN);

    for(auto it = configs.rbegin(); it != configs.rend(); ++it) {
        stack.push_back(*it);
    }

    std::vector<RuntimeConfiguration> closure;
    closure.reserve(StateN);
    std::array<bool, StateN> visited{};

    while(!stack.empty()) {
        auto current = std::move(stack.back());
        stack.pop_back();

        if(current.state >= StateN || visited[current.state]) {
            continue;
        }

        visited[current.state] = true;
        closure.push_back(current);

        const auto& epsilonSteps = transition.toMap[current.state].epsilonSteps;
        // Reverse push so lower priority value is popped first from the LIFO stack.
        for(unsigned i = MaxConn; i > 0; --i) {
            const auto& edge = epsilonSteps[i - 1];
            if(!edge.isEnabled) {
                continue;
            }

            auto next = current;
            next.state = edge.another;
            applyTagUpdate(next.offsets, edge.tag, position, unsetValue);
            if(next.state < StateN && !visited[next.state]) {
                // Always pop the higher priority epsilon transition first.
                stack.push_back(std::move(next));
            }
        }
    }

    std::vector<RuntimeConfiguration> filtered;
    filtered.reserve(closure.size());
    for(auto& cfg: closure) {
        if(cfg.state == finalState || hasSymbolTransition(transition, cfg.state)) {
            filtered.push_back(std::move(cfg));
        }
    }

    return filtered;
}

template <unsigned StateN, unsigned MaxConn>
[[nodiscard]] auto simulation(const Transition<StateN, MaxConn>& transition,
                              StateTy initialState,
                              StateTy finalState,
                              std::string_view input,
                              std::size_t tagCount = 0,
                              int unsetValue = offset_unset) -> std::optional<std::vector<int>> {
    if(initialState >= StateN || finalState >= StateN) {
        return std::nullopt;
    }

    if(tagCount == 0) {
        tagCount = countTags(transition);
    }

    std::vector<int> m0(tagCount, unsetValue);
    std::vector<RuntimeConfiguration> configs;
    configs.push_back(RuntimeConfiguration{initialState, m0});

    for(std::size_t i = 0; i < input.size(); ++i) {
        const int k = static_cast<int>(i + 1);
        configs = epsilonClosure(configs, transition, finalState, k, unsetValue);
        configs = stepOnSymbol(configs, transition, input[i]);

        if(configs.empty()) {
            return std::nullopt;
        }
    }

    configs =
        epsilonClosure(configs, transition, finalState, static_cast<int>(input.size()), unsetValue);

    for(const auto& cfg: configs) {
        if(cfg.state == finalState) {
            return cfg.offsets;
        }
    }
    return std::nullopt;
}

template <unsigned StateN, unsigned MaxConn>
[[nodiscard]] auto simulation(const TNFAModel<StateN, MaxConn>& model,
                              std::string_view input,
                              std::size_t tagCount = 0,
                              int unsetValue = offset_unset) -> std::optional<std::vector<int>> {
    return simulation(model.transition,
                      model.initialState,
                      model.finalState,
                      input,
                      tagCount,
                      unsetValue);
}

template <class Tree, bool estimate, unsigned stateN = 0, unsigned MaxConn = 0, unsigned TagN = 1>
struct TNFABuilder : regex::RecursiveRegexVisitor {
    static_assert(estimate || (stateN > 0 && MaxConn > 0),
                  "Build mode requires non-zero stateN and MaxConn");

    struct EstimateResult {
        StateTy stateN_ = 0;
        std::size_t MaxConn_ = 0;
        std::size_t tagCount_ = 0;
    };

    struct BuildData {
        StateTy stateIdx_ = 0;
    };

    struct Fragment {
        StateTy enter = 0;
        StateTy exit = 0;
        std::vector<TagTy> tags{};
    };

    enum class BuildError {
        none,
        state_capacity_exceeded,
        invalid_state,
        null_char_fn,
        transition_capacity_exceeded,
        epsilon_capacity_exceeded,
        invalid_repetition,
    };

    using ResTy = std::conditional_t<estimate, EstimateResult, TNFAModel<stateN, MaxConn>>;
    using AuxTy = std::conditional_t<estimate, std::vector<StateTy>, BuildData>;

    ResTy data_{};
    AuxTy aux_{};
    BuildError buildError_ = BuildError::none;

    StateTy subStateEnter_ = 0;
    StateTy subStateExit_ = 0;
    std::vector<TagTy> subTags_{};
    const Tree& tree_;

    constexpr explicit TNFABuilder(const Tree& tree) : tree_(tree) {}

    constexpr const static char* buildErrorToString(BuildError err) {
        switch(err) {
            case BuildError::none: return "none";
            case BuildError::state_capacity_exceeded: return "state capacity exceeded";
            case BuildError::invalid_state: return "invalid state index";
            case BuildError::null_char_fn: return "null char check function";
            case BuildError::transition_capacity_exceeded:
                return "char transition capacity exceeded";
            case BuildError::epsilon_capacity_exceeded:
                return "epsilon transition capacity exceeded";
            case BuildError::invalid_repetition: return "invalid repetition bounds";
        }
        return "unknown";
    }

    constexpr void reportBuildError(BuildError err) {
        if constexpr(!estimate) {
            if(buildError_ == BuildError::none) {
                buildError_ = err;
            }
        }
    }

    constexpr static void addUniqueTag(std::vector<TagTy>& tags, TagTy tag) {
        if(tag <= 0) {
            return;
        }
        if(std::find(tags.begin(), tags.end(), tag) == tags.end()) {
            tags.push_back(tag);
        }
    }

    [[nodiscard]] constexpr static auto
        mergeTagSets(const std::vector<TagTy>& lhs, const std::vector<TagTy>& rhs)
        -> std::vector<TagTy> {
        std::vector<TagTy> merged = lhs;
        merged.reserve(lhs.size() + rhs.size());
        for(const auto tag: rhs) {
            addUniqueTag(merged, tag);
        }
        return merged;
    }

    constexpr void recordTagUsage(TagTy tag) {
        if(tag == tag_epsilon) {
            return;
        }
        const auto absTag = static_cast<std::size_t>(tag > 0 ? tag : -tag);
        if constexpr(estimate) {
            if(absTag > data_.tagCount_) {
                data_.tagCount_ = absTag;
            }
        }
    }

    [[nodiscard]] constexpr Fragment snapshot() const {
        return Fragment{subStateEnter_, subStateExit_, subTags_};
    }

    constexpr void pushDelta(StateTy from, StateTy to, CharFn charFn) {
        if constexpr(estimate) {
            if(aux_.size() <= from) {
                aux_.resize(from + 1, 0);
            }
            aux_[from] += 1;
        } else {
            if(from >= stateN || to >= stateN) {
                reportBuildError(BuildError::invalid_state);
                return;
            }
            if(charFn == nullptr) {
                reportBuildError(BuildError::null_char_fn);
                return;
            }
            if(!data_.transition.addTransition(from, to, charFn)) {
                reportBuildError(BuildError::transition_capacity_exceeded);
            }
        }
    }

    constexpr void pushDelta(StateTy from, StateTy to, char literal) {
        if constexpr(estimate) {
            if(aux_.size() <= from) {
                aux_.resize(from + 1, 0);
            }
            aux_[from] += 1;
        } else {
            if(from >= stateN || to >= stateN) {
                reportBuildError(BuildError::invalid_state);
                return;
            }
            if(!data_.transition.addLiteralTransition(from, to, literal)) {
                reportBuildError(BuildError::transition_capacity_exceeded);
            }
        }
    }

    constexpr void
        pushEpsilonDelta(StateTy from, StateTy to, TagTy tag = tag_epsilon, int priority = 0) {
        if constexpr(estimate) {
            if(aux_.size() <= from) {
                aux_.resize(from + 1, 0);
            }
            aux_[from] += 1;
        } else {
            if(from >= stateN || to >= stateN) {
                reportBuildError(BuildError::invalid_state);
                return;
            }
            if(!data_.transition.addEpsilonTransition(from, to, tag, priority)) {
                reportBuildError(BuildError::epsilon_capacity_exceeded);
            }
        }
    }

    constexpr void setInitialState(StateTy state) {
        if constexpr(!estimate) {
            if(state >= stateN) {
                reportBuildError(BuildError::invalid_state);
                return;
            }
            data_.initialState = state;
        }
    }

    constexpr void setFinalState(StateTy state) {
        if constexpr(!estimate) {
            if(state >= stateN) {
                reportBuildError(BuildError::invalid_state);
                return;
            }
            data_.finalState = state;
        }
    }

    [[nodiscard]] constexpr StateTy newState() {
        if constexpr(estimate) {
            aux_.push_back(0);
            return data_.stateN_++;
        } else {
            if(aux_.stateIdx_ >= stateN) {
                reportBuildError(BuildError::state_capacity_exceeded);
                return stateN;
            }
            return aux_.stateIdx_++;
        }
    }

    [[nodiscard]] constexpr BuildError error() const {
        if constexpr(estimate) {
            return BuildError::none;
        } else {
            return buildError_;
        }
    }

    [[nodiscard]] constexpr const char* errorMessage() const {
        return buildErrorToString(error());
    }

    [[nodiscard]] constexpr bool ok() const {
        return error() == BuildError::none;
    }

    [[nodiscard]] constexpr auto result() {
        if constexpr(estimate) {
            std::size_t maxConn = 0;
            for(const auto& conn: aux_) {
                if(conn > maxConn) {
                    maxConn = conn;
                }
            }
            return EstimateResult{data_.stateN_, maxConn, data_.tagCount_};
        } else {
            if(buildError_ != BuildError::none) {
                return std::optional<TNFAModel<stateN, MaxConn>>{};
            }
            return std::optional<TNFAModel<stateN, MaxConn>>(data_);
        }
    }

    constexpr void buildEpsilon() {
        const auto state = newState();
        subStateEnter_ = state;
        subStateExit_ = state;
        subTags_.clear();
    }

    // Build ntags(T, qf): a chain of negative-tag epsilon transitions.
    constexpr void ntags(const std::vector<TagTy>& tags, std::optional<StateTy> exit = std::nullopt) {
        std::vector<TagTy> normalized;
        normalized.reserve(tags.size());
        for(const auto tag: tags) {
            addUniqueTag(normalized, tag > 0 ? tag : -tag);
        }

        if(normalized.empty()) {
            if(exit.has_value()) {
                subStateEnter_ = subStateExit_ = *exit;
            } else {
                buildEpsilon();
            }
            subTags_.clear();
            return;
        }

        subStateEnter_ = newState();
        StateTy nowState = subStateEnter_;
        for(std::size_t i = 0; i < normalized.size(); ++i) {
            StateTy nextState = 0;
            if(exit.has_value() && i + 1 == normalized.size()) {
                nextState = *exit;
            } else {
                nextState = newState();
            }
            pushEpsilonDelta(nowState, nextState, -normalized[i], 1);
            nowState = nextState;
        }
        subStateExit_ = nowState;
        subTags_ = std::move(normalized);
    }

    constexpr void buildRepetitionNode(int child, int minRepeat, int maxRepeat) {
        if(minRepeat < 0 || (maxRepeat >= 0 && maxRepeat < minRepeat)) {
            reportBuildError(BuildError::invalid_repetition);
            buildEpsilon();
            return;
        }

        if(maxRepeat == 0) {
            buildEpsilon();
            return;
        }

        if(minRepeat > 1) {
            const auto nextMax = maxRepeat < 0 ? -1 : (maxRepeat - 1);
            buildRepetitionNode(child, minRepeat - 1, nextMax);
            const auto suffix = snapshot();

            traverse(tree_, child);
            const auto prefix = snapshot();

            pushEpsilonDelta(prefix.exit, suffix.enter);
            subStateEnter_ = prefix.enter;
            subStateExit_ = suffix.exit;
            subTags_ = mergeTagSets(prefix.tags, suffix.tags);
            return;
        }

        if(minRepeat == 0) {
            buildRepetitionNode(child, 1, maxRepeat);
            const auto oneToMany = snapshot();

            ntags(oneToMany.tags, oneToMany.exit);
            const auto zeroBranch = snapshot();

            const auto entry = newState();
            pushEpsilonDelta(entry, oneToMany.enter, tag_epsilon, 1);
            pushEpsilonDelta(entry, zeroBranch.enter, tag_epsilon, 2);

            subStateEnter_ = entry;
            subStateExit_ = oneToMany.exit;
            subTags_ = oneToMany.tags;
            return;
        }

        // minRepeat == 1
        if(maxRepeat == 1) {
            traverse(tree_, child);
            return;
        }

        if(maxRepeat < 0) {
            // e{1,inf}
            traverse(tree_, child);
            const auto oneOrMore = snapshot();

            const auto finalState = newState();
            pushEpsilonDelta(oneOrMore.exit, oneOrMore.enter, tag_epsilon, 1);
            pushEpsilonDelta(oneOrMore.exit, finalState, tag_epsilon, 2);

            subStateEnter_ = oneOrMore.enter;
            subStateExit_ = finalState;
            subTags_ = oneOrMore.tags;
            return;
        }

        // 1 < maxRepeat < inf: e{1,m} == e · e{0,m-1}
        traverse(tree_, child);
        const auto mandatory = snapshot();

        buildRepetitionNode(child, 0, maxRepeat - 1);
        const auto optionalTail = snapshot();

        pushEpsilonDelta(mandatory.exit, optionalTail.enter);
        subStateEnter_ = mandatory.enter;
        subStateExit_ = optionalTail.exit;
        subTags_ = mergeTagSets(mandatory.tags, optionalTail.tags);
    }

    constexpr void build() {
        if(!tree_.ok()) {
            reportBuildError(BuildError::invalid_state);
            buildEpsilon();
            setInitialState(subStateEnter_);
            setFinalState(subStateExit_);
            return;
        }

        traverse(tree_);
        setInitialState(subStateEnter_);
        setFinalState(subStateExit_);
    }

    constexpr bool visitEmpty(int nodeIdx) override {
        (void)nodeIdx;
        buildEpsilon();
        return false;
    }

    constexpr bool visitInvalidNode(int nodeIdx) override {
        (void)nodeIdx;
        reportBuildError(BuildError::invalid_state);
        buildEpsilon();
        return false;
    }

    constexpr bool visitLiteral(int nodeIdx, char literal) override {
        (void)nodeIdx;
        subStateEnter_ = newState();
        subStateExit_ = newState();
        subTags_.clear();
        pushDelta(subStateEnter_, subStateExit_, literal);
        return false;
    }

    constexpr bool visitCharCheck(int nodeIdx, CharFn checkFn) override {
        (void)nodeIdx;
        subStateEnter_ = newState();
        subStateExit_ = newState();
        subTags_.clear();
        pushDelta(subStateEnter_, subStateExit_, checkFn);
        return false;
    }

    constexpr bool visitTag(int nodeIdx, TagTy tag) override {
        (void)nodeIdx;
        subStateEnter_ = newState();
        subStateExit_ = newState();
        subTags_.clear();
        if(tag == tag_epsilon) {
            pushEpsilonDelta(subStateEnter_, subStateExit_);
            return false;
        }
        pushEpsilonDelta(subStateEnter_, subStateExit_, tag);
        addUniqueTag(subTags_, tag > 0 ? tag : -tag);
        recordTagUsage(tag);
        return false;
    }

    constexpr bool visitConcat(int nodeIdx, int left, int right) override {
        (void)nodeIdx;
        traverse(tree_, left);
        const auto leftFrag = snapshot();

        traverse(tree_, right);
        const auto rightFrag = snapshot();

        pushEpsilonDelta(leftFrag.exit, rightFrag.enter);
        subStateEnter_ = leftFrag.enter;
        subStateExit_ = rightFrag.exit;
        subTags_ = mergeTagSets(leftFrag.tags, rightFrag.tags);
        return false;
    }

    constexpr bool visitAlternation(int nodeIdx, int left, int right) override {
        (void)nodeIdx;
        // Build right branch first so both branches can share its final state.
        traverse(tree_, right);
        const auto rightFrag = snapshot();

        ntags(rightFrag.tags, rightFrag.exit);
        const auto resetRightTags = snapshot();

        traverse(tree_, left);
        const auto leftFrag = snapshot();

        pushEpsilonDelta(leftFrag.exit, resetRightTags.enter);

        ntags(leftFrag.tags, rightFrag.enter);
        const auto resetLeftTags = snapshot();

        const auto entry = newState();
        pushEpsilonDelta(entry, leftFrag.enter, tag_epsilon, 1);
        pushEpsilonDelta(entry, resetLeftTags.enter, tag_epsilon, 2);

        subStateEnter_ = entry;
        subStateExit_ = rightFrag.exit;
        subTags_ = mergeTagSets(leftFrag.tags, rightFrag.tags);
        return false;
    }

    constexpr bool visitRepetition(int nodeIdx, int child, int minRepeat, int maxRepeat) override {
        (void)nodeIdx;
        buildRepetitionNode(child, minRepeat, maxRepeat);
        return false;
    }
};

template <class Tree, unsigned StateN, unsigned MaxConn, unsigned TagN = 1>
[[nodiscard]] constexpr auto fromRegexTree(const Tree& tree)
    -> std::optional<TNFAModel<StateN, MaxConn>> {
    TNFABuilder<Tree, false, StateN, MaxConn, TagN> builder(tree);
    builder.build();
    return builder.result();
}

template <auto tree>
[[nodiscard]] consteval auto fromRegexTreeAuto() {
    using TreeTy = std::remove_cvref_t<decltype(tree)>;
    if constexpr(!tree.ok()) {
        return std::optional<TNFAModel<1, 1>>{};
    } else {
        constexpr auto estimate = []() consteval {
            TNFABuilder<TreeTy, true> builder(tree);
            builder.build();
            return builder.result();
        }();

        constexpr auto kStateN =
            estimate.stateN_ == 0 ? 1u : static_cast<unsigned>(estimate.stateN_);
        constexpr auto kMaxConn =
            estimate.MaxConn_ == 0 ? 1u : static_cast<unsigned>(estimate.MaxConn_);
        constexpr auto kTagN =
            estimate.tagCount_ == 0 ? 1u : static_cast<unsigned>(estimate.tagCount_);

        TNFABuilder<TreeTy, false, kStateN, kMaxConn, kTagN + 1> builder(tree);
        builder.build();
        return builder.result();
    }
}

}  // namespace etch::TNFA
