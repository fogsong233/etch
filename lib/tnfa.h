#pragma once

/**
 * This file defines the TNFA data structure and runtime simulation.
 */

#include "regex.h"
#include "ty.h"
#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace etch::TNFA {

inline constexpr int offset_unset = -1;

struct RuntimeConfiguration {
  StateTy state{};
  std::vector<int> offsets{};
};

template <unsigned StateN, unsigned MaxConn> struct Transition {
  struct CharStep {
    StateTy another = 0;
    CharFn charFn = nullptr;
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
  static constexpr bool addCharStep(Steps &steps, StateTy another,
                                    CharFn charFn) {
    for (auto &edge : steps.charSteps) {
      if (edge.isEnabled) {
        continue;
      }
      edge.another = another;
      edge.charFn = charFn;
      edge.isEnabled = true;
      return true;
    }
    return false;
  }

  static constexpr void sortEpsilonSteps(Steps &steps) {
    // Keep enabled epsilon transitions in ascending priority order.
    for (unsigned i = 1; i < MaxConn; ++i) {
      auto key = steps.epsilonSteps[i];
      unsigned j = i;
      while (j > 0) {
        const auto &prev = steps.epsilonSteps[j - 1];
        const bool shouldMove =
            (key.isEnabled && !prev.isEnabled) ||
            (key.isEnabled && prev.isEnabled && prev.priority > key.priority);
        if (!shouldMove) {
          break;
        }
        steps.epsilonSteps[j] = prev;
        --j;
      }
      steps.epsilonSteps[j] = key;
    }
  }

  static constexpr bool addEpsilonStep(Steps &steps, StateTy another, TagTy tag,
                                       int priority) {
    for (auto &edge : steps.epsilonSteps) {
      if (edge.isEnabled) {
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
    if (from >= StateN || to >= StateN || charFn == nullptr) {
      return false;
    }
    return addCharStep(toMap[from], to, charFn) &&
           addCharStep(fromMap[to], from, charFn);
  }

  constexpr bool addEpsilonTransition(StateTy from, StateTy to,
                                      TagTy tag = tag_epsilon,
                                      int priority = 0) {
    if (from >= StateN || to >= StateN) {
      return false;
    }
    return addEpsilonStep(toMap[from], to, tag, priority) &&
           addEpsilonStep(fromMap[to], from, tag, priority);
  }

  constexpr void sortPriority() {
    for (auto &steps : toMap) {
      sortEpsilonSteps(steps);
    }
    for (auto &steps : fromMap) {
      sortEpsilonSteps(steps);
    }
  }
};

inline void applyTagUpdate(std::vector<int> &offsets, TagTy tag, int position,
                           int unsetValue) {
  if (tag == tag_epsilon) {
    return;
  }

  const auto absTag = static_cast<std::size_t>(tag > 0 ? tag : -tag);
  if (absTag == 0 || absTag > offsets.size()) {
    return;
  }

  const auto idx = absTag - 1;
  if (tag > 0) {
    offsets[idx] = position;
  } else {
    offsets[idx] = unsetValue;
  }
}

template <unsigned StateN, unsigned MaxConn>
[[nodiscard]] constexpr bool
hasSymbolTransition(const Transition<StateN, MaxConn> &transition,
                    StateTy state) {
  if (state >= StateN) {
    return false;
  }

  for (const auto &edge : transition.toMap[state].charSteps) {
    if (edge.isEnabled) {
      return true;
    }
  }
  return false;
}

template <unsigned StateN, unsigned MaxConn>
[[nodiscard]] constexpr std::size_t
countTags(const Transition<StateN, MaxConn> &transition) {
  std::size_t maxTag = 0;
  for (const auto &steps : transition.toMap) {
    for (const auto &edge : steps.epsilonSteps) {
      if (!edge.isEnabled || edge.tag == tag_epsilon) {
        continue;
      }
      const auto absTag =
          static_cast<std::size_t>(edge.tag > 0 ? edge.tag : -edge.tag);
      if (absTag > maxTag) {
        maxTag = absTag;
      }
    }
  }
  return maxTag;
}

template <unsigned StateN, unsigned MaxConn>
[[nodiscard]] auto
stepOnSymbol(const std::vector<RuntimeConfiguration> &configs,
             const Transition<StateN, MaxConn> &transition, char symbol)
    -> std::vector<RuntimeConfiguration> {
  std::vector<RuntimeConfiguration> next;
  next.reserve(configs.size());

  for (const auto &cfg : configs) {
    if (cfg.state >= StateN) {
      continue;
    }

    for (const auto &edge : transition.toMap[cfg.state].charSteps) {
      if (!edge.isEnabled || edge.charFn == nullptr) {
        continue;
      }
      if (!edge.charFn(symbol)) {
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
[[nodiscard]] auto
epsilonClosure(const std::vector<RuntimeConfiguration> &configs,
               const Transition<StateN, MaxConn> &transition,
               StateTy finalState, int position, int unsetValue = offset_unset)
    -> std::vector<RuntimeConfiguration> {
  std::vector<RuntimeConfiguration> stack;
  stack.reserve(configs.size() + StateN);

  for (auto it = configs.rbegin(); it != configs.rend(); ++it) {
    stack.push_back(*it);
  }

  std::vector<RuntimeConfiguration> closure;
  closure.reserve(StateN);
  std::array<bool, StateN> visited{};

  while (!stack.empty()) {
    auto current = std::move(stack.back());
    stack.pop_back();

    if (current.state >= StateN || visited[current.state]) {
      continue;
    }

    visited[current.state] = true;
    closure.push_back(current);

    const auto &epsilonSteps = transition.toMap[current.state].epsilonSteps;
    // Reverse push so lower priority value is popped first from the LIFO stack.
    for (unsigned i = MaxConn; i > 0; --i) {
      const auto &edge = epsilonSteps[i - 1];
      if (!edge.isEnabled) {
        continue;
      }

      auto next = current;
      next.state = edge.another;
      applyTagUpdate(next.offsets, edge.tag, position, unsetValue);
      if (next.state < StateN && !visited[next.state]) {
        // Always pop the higher priority epsilon transition first.
        stack.push_back(std::move(next));
      }
    }
  }

  std::vector<RuntimeConfiguration> filtered;
  filtered.reserve(closure.size());
  for (auto &cfg : closure) {
    if (cfg.state == finalState || hasSymbolTransition(transition, cfg.state)) {
      filtered.push_back(std::move(cfg));
    }
  }

  return filtered;
}

template <unsigned StateN, unsigned MaxConn>
[[nodiscard]] auto simulation(const Transition<StateN, MaxConn> &transition,
                              StateTy initialState, StateTy finalState,
                              std::string_view input, std::size_t tagCount = 0,
                              int unsetValue = offset_unset)
    -> std::optional<std::vector<int>> {
  if (initialState >= StateN || finalState >= StateN) {
    return std::nullopt;
  }

  if (tagCount == 0) {
    tagCount = countTags(transition);
  }

  std::vector<int> m0(tagCount, unsetValue);
  std::vector<RuntimeConfiguration> configs;
  configs.push_back(RuntimeConfiguration{initialState, m0});

  for (std::size_t i = 0; i < input.size(); ++i) {
    const int k = static_cast<int>(i + 1);
    configs = epsilonClosure(configs, transition, finalState, k, unsetValue);
    configs = stepOnSymbol(configs, transition, input[i]);

    if (configs.empty()) {
      return std::nullopt;
    }
  }

  configs = epsilonClosure(configs, transition, finalState,
                           static_cast<int>(input.size()), unsetValue);

  for (const auto &cfg : configs) {
    if (cfg.state == finalState) {
      return cfg.offsets;
    }
  }
  return std::nullopt;
}

// convert from regex tree to TNFA transition.
// template <regex::RegexType Ex> consteval auto fromRegex() {

// }
} // namespace etch::TNFA
