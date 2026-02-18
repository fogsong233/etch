#pragma once

/**
 * This file defines the TNFA data structure and runtime simulation.
 */

#include "regex.h"
#include "ty.h"

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

template <unsigned StateN,
          unsigned MaxCharStep,
          unsigned MaxEpsilonStep,
          unsigned MaxSplitRange,
          unsigned MaxOwnSplit>
struct Transition {
    struct CharStep {
        StateTy another = 0;
        std::array<std::size_t, MaxOwnSplit> charSupportRef{};  // the index in splitRanges
        std::size_t charSupportRefIdx = 0;
        bool isEnabled = false;
    };

    struct EpsilonStep {
        StateTy another = 0;
        TagTy tag = tagEpsilon;
        int priority = 0;
        bool isEnabled = false;
    };

    struct Steps {
        std::array<CharStep, MaxCharStep> charSteps{};
        std::array<EpsilonStep, MaxEpsilonStep> epsilonSteps{};
    };

    std::array<Steps, StateN> toMap{};
    std::array<Steps, StateN> fromMap{};

    std::array<CharPair, MaxSplitRange> splitRanges{};
    std::size_t splitRangeIdx = 0;

private:
    constexpr static bool addCharStep(Steps& steps,
                                      StateTy another,
                                      const std::array<std::size_t, MaxOwnSplit>& charSupportRef,
                                      std::size_t charSupportRefIdx) {
        if(charSupportRefIdx > MaxOwnSplit) {
            return false;
        }

        for(auto& edge: steps.charSteps) {
            if(edge.isEnabled) {
                continue;
            }
            edge.another = another;
            edge.charSupportRef = charSupportRef;
            edge.charSupportRefIdx = charSupportRefIdx;
            edge.isEnabled = true;
            return true;
        }
        return false;
    }

    constexpr static void sortEpsilonSteps(Steps& steps) {
        if constexpr(MaxEpsilonStep < 2) {
            return;
        }

        for(unsigned i = 0; i + 1 < MaxEpsilonStep; ++i) {
            for(unsigned j = 0; j + 1 < MaxEpsilonStep - i; ++j) {
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
    template <std::size_t N>
    constexpr bool setSplitRanges(const std::array<CharPair, N>& ranges, std::size_t count) {
        if(count > N || count > MaxSplitRange) {
            return false;
        }
        splitRangeIdx = count;
        for(std::size_t i = 0; i < count; ++i) {
            splitRanges[i] = ranges[i];
        }
        return true;
    }

    constexpr bool addTransition(StateTy from,
                                 StateTy to,
                                 const std::array<std::size_t, MaxOwnSplit>& charSupportRef,
                                 std::size_t charSupportRefIdx) {
        if(from >= StateN || to >= StateN) {
            return false;
        }
        if(!addCharStep(toMap[from], to, charSupportRef, charSupportRefIdx)) {
            return false;
        }
        (void)addCharStep(fromMap[to], from, charSupportRef, charSupportRefIdx);
        return true;
    }

    constexpr bool
        addEpsilonTransition(StateTy from, StateTy to, TagTy tag = tagEpsilon, int priority = 0) {
        if(from >= StateN || to >= StateN) {
            return false;
        }
        if(!addEpsilonStep(toMap[from], to, tag, priority)) {
            return false;
        }
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

template <unsigned StateN,
          unsigned MaxCharStep,
          unsigned MaxConn,
          unsigned MaxSplitRange,
          unsigned MaxOwnSplit>
struct TNFAModel {
    constexpr static auto ty = etch::EtchTy::TNFAModel;
    StateTy initialState{};
    StateTy finalState{};
    Transition<StateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit> transition{};
    TNFAError error = TNFAError::none;

    constexpr auto tagSize() const {
        return countTags(transition);
    }

    constexpr auto stateSize() const {
        return StateN;
    }

    constexpr auto charRangeSize() const {
        return transition.splitRangeIdx + 1;
    }

    [[nodiscard]] constexpr auto errorMsg(TNFAError err) const -> const char* {
        return TNFAErrorToString(err);
    }
};

inline void applyTagUpdate(std::vector<int>& offsets, TagTy tag, int position, int unsetValue) {
    if(tag == tagEpsilon) {
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

template <unsigned StateN,
          unsigned MaxCharStep,
          unsigned MaxConn,
          unsigned MaxSplitRange,
          unsigned MaxOwnSplit>
[[nodiscard]] constexpr bool hasSymbolTransition(
    const Transition<StateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit>& transition,
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

template <unsigned StateN,
          unsigned MaxCharStep,
          unsigned MaxConn,
          unsigned MaxSplitRange,
          unsigned MaxOwnSplit>
[[nodiscard]] constexpr std::size_t countTags(
    const Transition<StateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit>& transition) {
    std::size_t maxTag = 0;
    for(const auto& steps: transition.toMap) {
        for(const auto& edge: steps.epsilonSteps) {
            if(!edge.isEnabled || edge.tag == tagEpsilon) {
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

template <unsigned StateN,
          unsigned MaxCharStep,
          unsigned MaxConn,
          unsigned MaxSplitRange,
          unsigned MaxOwnSplit>
[[nodiscard]] auto stepOnSymbol(
    const std::vector<RuntimeConfiguration>& configs,
    const Transition<StateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit>& transition,
    char symbol) -> std::vector<RuntimeConfiguration> {
    std::vector<RuntimeConfiguration> next;
    next.reserve(configs.size());

    const auto symbolIdx = static_cast<CharTy>(static_cast<unsigned char>(symbol));

    for(const auto& cfg: configs) {
        if(cfg.state >= StateN) {
            continue;
        }

        for(const auto& edge: transition.toMap[cfg.state].charSteps) {
            if(!edge.isEnabled) {
                continue;
            }

            bool matched = false;
            for(std::size_t i = 0; i < edge.charSupportRefIdx; ++i) {
                const auto splitIdx = static_cast<std::size_t>(edge.charSupportRef[i]);
                if(splitIdx >= transition.splitRangeIdx) {
                    continue;
                }
                const auto range = transition.splitRanges[splitIdx];
                if(symbolIdx >= range.first && symbolIdx <= range.second) {
                    matched = true;
                    break;
                }
            }
            if(!matched) {
                continue;
            }

            auto moved = cfg;
            moved.state = edge.another;
            next.push_back(std::move(moved));
        }
    }

    return next;
}

template <unsigned StateN,
          unsigned MaxCharStep,
          unsigned MaxConn,
          unsigned MaxSplitRange,
          unsigned MaxOwnSplit>
[[nodiscard]] auto epsilonClosure(
    const std::vector<RuntimeConfiguration>& configs,
    const Transition<StateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit>& transition,
    StateTy finalState,
    int position,
    int unsetValue = offset_unset) -> std::vector<RuntimeConfiguration> {
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
        for(unsigned i = MaxConn; i > 0; --i) {
            const auto& edge = epsilonSteps[i - 1];
            if(!edge.isEnabled) {
                continue;
            }

            auto next = current;
            next.state = edge.another;
            applyTagUpdate(next.offsets, edge.tag, position, unsetValue);
            if(next.state < StateN && !visited[next.state]) {
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

template <unsigned StateN,
          unsigned MaxCharStep,
          unsigned MaxConn,
          unsigned MaxSplitRange,
          unsigned MaxOwnSplit>
[[nodiscard]] auto simulation(
    const Transition<StateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit>& transition,
    StateTy initialState,
    StateTy finalState,
    std::string_view input,
    std::size_t tagCount = 0,
    int unsetValue = offset_unset) -> std::optional<std::vector<int>> {
    if(initialState >= StateN || finalState >= StateN) {
        return std::nullopt;
    }
    if(input.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
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

    const auto finalBoundary = static_cast<int>(input.size() + 1);
    configs = epsilonClosure(configs, transition, finalState, finalBoundary, unsetValue);

    for(const auto& cfg: configs) {
        if(cfg.state == finalState) {
            return cfg.offsets;
        }
    }
    return std::nullopt;
}

template <unsigned StateN,
          unsigned MaxCharStep,
          unsigned MaxConn,
          unsigned MaxSplitRange,
          unsigned MaxOwnSplit>
[[nodiscard]] auto
    simulation(const TNFAModel<StateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit>& model,
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

template <class Tree,
          bool estimate,
          unsigned stateN = 0,
          unsigned MaxCharStep = 0,
          unsigned MaxConn = 0,
          unsigned MaxSplitRange = 0,
          unsigned MaxOwnSplit = 0,
          unsigned TagN = 1>
struct TNFABuilder : regex::RecursiveRegexVisitor {
    static_assert(estimate || (stateN > 0 && MaxCharStep > 0 && MaxConn > 0 && MaxSplitRange > 0 &&
                               MaxOwnSplit > 0),
                  "Build mode requires non-zero capacities");

    struct EstimateResult {
        StateTy stateN_ = 0;
        std::size_t MaxCharStep_ = 0;
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

    using ResTy =
        std::conditional_t<estimate,
                           EstimateResult,
                           TNFAModel<stateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit>>;

    struct ConnStat {
        std::size_t charSteps = 0;
        std::size_t epsilonSteps = 0;
    };

    using AuxTy = std::conditional_t<estimate, std::vector<ConnStat>, BuildData>;

    ResTy data_{};
    AuxTy aux_{};
    TNFAError buildError_ = TNFAError::none;

    StateTy subStateEnter_ = 0;
    StateTy subStateExit_ = 0;
    std::vector<TagTy> subTags_{};
    const Tree& tree_;

    constexpr explicit TNFABuilder(const Tree& tree) : tree_(tree) {}

    constexpr void reportBuildError(TNFAError err) {
        if constexpr(!estimate) {
            if(buildError_ == TNFAError::none) {
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

    [[nodiscard]] constexpr static auto mergeTagSets(const std::vector<TagTy>& lhs,
                                                     const std::vector<TagTy>& rhs)
        -> std::vector<TagTy> {
        std::vector<TagTy> merged = lhs;
        merged.reserve(lhs.size() + rhs.size());
        for(const auto tag: rhs) {
            addUniqueTag(merged, tag);
        }
        return merged;
    }

    constexpr void recordTagUsage(TagTy tag) {
        if(tag == tagEpsilon) {
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

    constexpr void pushDelta(StateTy from,
                             StateTy to,
                             const typename Tree::CharRangeRef& charSupportRef,
                             std::size_t charSupportRefIdx) {
        if constexpr(estimate) {
            if(aux_.size() <= from) {
                aux_.resize(from + 1);
            }
            aux_[from].charSteps += 1;
        } else {
            if(from >= stateN || to >= stateN) {
                reportBuildError(TNFAError::invalid_state);
                return;
            }
            if(!data_.transition.addTransition(from, to, charSupportRef, charSupportRefIdx)) {
                reportBuildError(TNFAError::transition_capacity_exceeded);
            }
        }
    }

    constexpr void
        pushEpsilonDelta(StateTy from, StateTy to, TagTy tag = tagEpsilon, int priority = 0) {
        if constexpr(estimate) {
            if(aux_.size() <= from) {
                aux_.resize(from + 1);
            }
            aux_[from].epsilonSteps += 1;
        } else {
            if(from >= stateN || to >= stateN) {
                reportBuildError(TNFAError::invalid_state);
                return;
            }
            if(!data_.transition.addEpsilonTransition(from, to, tag, priority)) {
                reportBuildError(TNFAError::epsilon_capacity_exceeded);
            }
        }
    }

    constexpr void setInitialState(StateTy state) {
        if constexpr(!estimate) {
            if(state >= stateN) {
                reportBuildError(TNFAError::invalid_state);
                return;
            }
            data_.initialState = state;
        }
    }

    constexpr void setFinalState(StateTy state) {
        if constexpr(!estimate) {
            if(state >= stateN) {
                reportBuildError(TNFAError::invalid_state);
                return;
            }
            data_.finalState = state;
        }
    }

    [[nodiscard]] constexpr StateTy newState() {
        if constexpr(estimate) {
            aux_.push_back(ConnStat{});
            return data_.stateN_++;
        } else {
            if(aux_.stateIdx_ >= stateN) {
                reportBuildError(TNFAError::state_capacity_exceeded);
                return stateN;
            }
            return aux_.stateIdx_++;
        }
    }

    [[nodiscard]] constexpr TNFAError error() const {
        if constexpr(estimate) {
            return TNFAError::none;
        }
        return buildError_;
    }

    [[nodiscard]] constexpr const char* errorMessage() const {
        return TNFAErrorToString(error());
    }

    [[nodiscard]] constexpr bool ok() const {
        return error() == TNFAError::none;
    }

    [[nodiscard]] constexpr auto result() {
        if constexpr(estimate) {
            std::size_t maxCharStep = 0;
            std::size_t maxConn = 0;
            for(const auto& conn: aux_) {
                if(conn.charSteps > maxCharStep) {
                    maxCharStep = conn.charSteps;
                }
                if(conn.epsilonSteps > maxConn) {
                    maxConn = conn.epsilonSteps;
                }
            }
            return EstimateResult{data_.stateN_, maxCharStep, maxConn, data_.tagCount_};
        } else {
            if(buildError_ != TNFAError::none) {
                return std::optional<
                    TNFAModel<stateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit>>{};
            }
            data_.error = buildError_;
            return std::optional<
                TNFAModel<stateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit>>(data_);
        }
    }

    constexpr void buildEpsilon() {
        const auto state = newState();
        subStateEnter_ = state;
        subStateExit_ = state;
        subTags_.clear();
    }

    constexpr void ntags(const std::vector<TagTy>& tags,
                         std::optional<StateTy> exit = std::nullopt) {
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
            reportBuildError(TNFAError::invalid_repetition);
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
            pushEpsilonDelta(entry, oneToMany.enter, tagEpsilon, 1);
            pushEpsilonDelta(entry, zeroBranch.enter, tagEpsilon, 2);

            subStateEnter_ = entry;
            subStateExit_ = oneToMany.exit;
            subTags_ = oneToMany.tags;
            return;
        }

        if(maxRepeat == 1) {
            traverse(tree_, child);
            return;
        }

        if(maxRepeat < 0) {
            traverse(tree_, child);
            const auto oneOrMore = snapshot();

            const auto finalState = newState();
            pushEpsilonDelta(oneOrMore.exit, oneOrMore.enter, tagEpsilon, 1);
            pushEpsilonDelta(oneOrMore.exit, finalState, tagEpsilon, 2);

            subStateEnter_ = oneOrMore.enter;
            subStateExit_ = finalState;
            subTags_ = oneOrMore.tags;
            return;
        }

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
        if constexpr(!estimate) {
            if(!data_.transition.setSplitRanges(tree_.splitRanges, tree_.splitRangeIdx)) {
                reportBuildError(TNFAError::split_range_capacity_exceeded);
            }
        }

        if(!tree_.ok()) {
            reportBuildError(TNFAError::invalid_state);
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
        reportBuildError(TNFAError::invalid_state);
        buildEpsilon();
        return false;
    }

    constexpr bool visitCharRange(int nodeIdx) override {
        if(nodeIdx < 0 || static_cast<std::size_t>(nodeIdx) >= tree_.size) {
            reportBuildError(TNFAError::invalid_state);
            buildEpsilon();
            return false;
        }

        const auto& node = tree_.nodes[static_cast<std::size_t>(nodeIdx)];
        subStateEnter_ = newState();
        subStateExit_ = newState();
        subTags_.clear();
        pushDelta(subStateEnter_, subStateExit_, node.charSupport, node.charSupportIdx);
        return false;
    }

    constexpr bool visitTag(int nodeIdx, TagTy tag) override {
        (void)nodeIdx;
        subStateEnter_ = newState();
        subStateExit_ = newState();
        subTags_.clear();
        if(tag == tagEpsilon) {
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
        pushEpsilonDelta(entry, leftFrag.enter, tagEpsilon, 1);
        pushEpsilonDelta(entry, resetLeftTags.enter, tagEpsilon, 2);

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

template <class Tree,
          unsigned StateN,
          unsigned MaxCharStep,
          unsigned MaxConn,
          unsigned MaxSplitRange,
          unsigned MaxOwnSplit,
          unsigned TagN = 1>
[[nodiscard]] constexpr auto fromRegexTree(const Tree& tree)
    -> std::optional<TNFAModel<StateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit>> {
    TNFABuilder<Tree, false, StateN, MaxCharStep, MaxConn, MaxSplitRange, MaxOwnSplit, TagN>
        builder(tree);
    builder.build();
    return builder.result();
}

template <auto tree>
[[nodiscard]] consteval auto fromRegexTreeAuto() {
    using TreeTy = std::remove_cvref_t<decltype(tree)>;
    if constexpr(!tree.ok()) {
        return std::optional<TNFAModel<1, 1, 1, 1, 1>>{};
    } else {
        constexpr auto estimate = []() consteval {
            TNFABuilder<TreeTy, true> builder(tree);
            builder.build();
            return builder.result();
        }();

        constexpr auto kStateN =
            estimate.stateN_ == 0 ? 1u : static_cast<unsigned>(estimate.stateN_);
        constexpr auto kMaxCharStep =
            estimate.MaxCharStep_ == 0 ? 1u : static_cast<unsigned>(estimate.MaxCharStep_);
        constexpr auto kMaxConn =
            estimate.MaxConn_ == 0 ? 1u : static_cast<unsigned>(estimate.MaxConn_);
        constexpr auto kMaxSplitRange =
            tree.splitRangeIdx == 0 ? 1u : static_cast<unsigned>(tree.splitRangeIdx);
        constexpr auto kMaxOwnSplit = []() consteval {
            std::size_t maxOwnSplit = 0;
            for(std::size_t i = 0; i < tree.size; ++i) {
                const auto& node = tree.nodes[i];
                if(node.kind == regex::RegexTreeNodeKind::char_range &&
                   node.charSupportIdx > maxOwnSplit) {
                    maxOwnSplit = node.charSupportIdx;
                }
            }
            return maxOwnSplit == 0 ? 1u : static_cast<unsigned>(maxOwnSplit);
        }();
        constexpr auto kTagN =
            estimate.tagCount_ == 0 ? 1u : static_cast<unsigned>(estimate.tagCount_);

        TNFABuilder<TreeTy,
                    false,
                    kStateN,
                    kMaxCharStep,
                    kMaxConn,
                    kMaxSplitRange,
                    kMaxOwnSplit,
                    kTagN + 1>
            builder(tree);
        builder.build();
        return builder.result();
    }
}

}  // namespace etch::TNFA
