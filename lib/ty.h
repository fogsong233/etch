#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace etch {
using StateTy = unsigned;
using CharFn = auto (*)(char) -> bool;
using TagTy = int;
constexpr TagTy tag_epsilon = 0;
using CharRange = std::bitset<256>;
using CharTy = uint8_t;
using CharPair = std::pair<CharTy, CharTy>;
}  // namespace etch
