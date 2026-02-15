#pragma once

#include "util.h"
#include <map>
#include <string>
namespace etch {
template <FixedString str> consteval auto parseBraces() {
  std::map<int, std::string> m;
  return str.size();
}
} // namespace etch