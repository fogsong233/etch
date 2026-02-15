#pragma once

namespace etch {
using StateTy = unsigned;
using CharFn = auto (*)(char) -> bool;
using TagTy = int;
constexpr TagTy tag_epsilon = 0;
} // namespace etch