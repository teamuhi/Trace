#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/color.hpp"

namespace trace {

// "#RRGGBBAA", or "#RRGGBB" for an opaque color. Anything else (missing '#', wrong length, non-hex digits)
// gives nullopt.
std::optional<Rgba8> parseColor(std::string_view text);

// Always "#RRGGBBAA", upper case.
std::string formatColor(Rgba8 color);

} // namespace trace
