#pragma once

#include <cstdint>
#include <vector>

namespace trace {

// True when `data` looks like a TrueType / OpenType font: a known sfnt version and a table directory that
// stays inside the data and names the glyph tables. This is a sanity check before the data is handed to the
// font rasterizer, not a security boundary.
bool looksLikeFont(const std::vector<std::uint8_t>& data);

} // namespace trace
