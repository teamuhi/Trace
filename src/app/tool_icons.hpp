#pragma once

#include <imgui.h>

#include "tools/tool.hpp"

namespace trace {

// Draws the icon of a tool inside the square [min, min + size] using only draw-list primitives, so it is
// sharp at any interface scale and takes its color from the caller (theme text color, or the contrast
// color on the accent when the tool is selected).
void drawToolIcon(ImDrawList* dl, ToolId tool, ImVec2 min, float size, ImU32 color);

} // namespace trace
