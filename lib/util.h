#pragma once

#include <cstddef>

namespace etch {
template <std::size_t N>
struct FixedString {
    char data[N]{};

    consteval FixedString(const char (&str)[N]) {
        for(std::size_t i = 0; i < N; ++i) {
            data[i] = str[i];
        }
    }

    [[nodiscard]] consteval std::size_t size() const {
        return N - 1;
    }
};

struct Slice {
    std::size_t start = 0;
    std::size_t length = 0;

    [[nodiscard]] constexpr bool empty() const {
        return length == 0;
    }

    [[nodiscard]] constexpr std::size_t begin() const {
        return start;
    }

    [[nodiscard]] constexpr std::size_t size() const {
        return length;
    }

    [[nodiscard]] constexpr std::size_t end() const {
        return start + length;
    }

    template <class Container>
    [[nodiscard]] constexpr auto operator() (const Container& container, std::size_t index) const
        -> decltype(container[0]) {
        return container[start + index];
    }
};
}  // namespace etch
