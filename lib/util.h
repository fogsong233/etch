#pragma once

#include <cstddef>

namespace etch {
template <std::size_t N> struct FixedString {
  char data[N]{};

  consteval FixedString(const char (&str)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
      data[i] = str[i];
    }
  }

  [[nodiscard]] consteval std::size_t size() const { return N - 1; }
};

template <class T> constexpr bool depTyTrue = true;
template <auto T> constexpr bool depValTrue = true;
} // namespace etch
