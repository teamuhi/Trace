#pragma once

#include <imgui.h>

#include "tools/tool.hpp"

namespace trace {

// Draws the icon of a tool inside the square [min, min + size] using only draw-list primitives, so it is
// sharp at any interface scale and takes its color from the caller (theme text color, or the contrast
// color on the accent when the tool is selected).
void drawToolIcon(ImDrawList* dl, ToolId tool, ImVec2 min, float size, ImU32 color);

// Same for the category icons of the Settings window, by page index (0 General, 1 Theme, 2 View, 3 Tools,
// 4 Pixel Art, 5 Scripts, 6 Keybinds, 7 About). A page outside that range draws nothing.
void drawSettingsIcon(ImDrawList* dl, int page, ImVec2 min, float size, ImU32 color);

// Small state icons for rows and buttons: an open or struck-through eye (layer visibility) and a closed or
// open padlock (layer lock).
enum class UiIcon { Eye, EyeOff, Lock, Unlock };
void drawUiIcon(ImDrawList* dl, UiIcon icon, ImVec2 min, float size, ImU32 color);

} // namespace trace
