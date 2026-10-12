#pragma once

#include <imgui.h>

#include "core/pattern.hpp"

namespace trace {

// Fills the rectangle [min, max] with `base`, then draws the pattern over it. The pattern is laid out from
// `anchor` (a grid line passes through it), so it can follow the image when the view pans and zooms.
// `spacing` is in the same units as the coordinates. `marks` colors lines, dots and plus signs; Checker
// uses `alt` for the odd squares. Spacing is raised when the area would need too many marks.
void drawPattern(ImDrawList* dl, PatternStyle style, ImVec2 min, ImVec2 max, ImVec2 anchor, float spacing,
                 ImU32 base, ImU32 marks, ImU32 alt);

} // namespace trace
