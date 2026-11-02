#pragma once

#include <imgui.h>

#include "app/svg_icons.hpp"
#include "tools/tool.hpp"

namespace trace {

// The SVG icon of a pixel/paint tool, and of a page of the Settings window (0 General, 1 Theme, 2 View, 3 Tools,
// 4 Pixel Art, 5 Flowchart, 6 Scripts, 7 Keybinds, 8 About; a page outside that range has none).
Icon toolIcon(ToolId tool);
Icon settingsIcon(int page);

// Draw those icons inside the square [min, min + size]. They take their color from the caller (theme text color,
// or the contrast color on the accent when the tool is selected); see drawIcon in svg_icons.hpp.
void drawToolIcon(ImDrawList* dl, ToolId tool, ImVec2 min, float size, ImU32 color);
void drawSettingsIcon(ImDrawList* dl, int page, ImVec2 min, float size, ImU32 color);

// Small state icons for rows and buttons: an open or struck-through eye (layer visibility), a closed or open padlock
// (layer lock), three vertical dots (a row's menu), a bin (delete) and a chevron pointing right or down (a row that
// is closed or open). The dots and chevrons are drawn with draw-list lines; the others are SVG icons.
enum class UiIcon { Eye, EyeOff, Lock, Unlock, Kebab, Trash, ChevronRight, ChevronDown };
void drawUiIcon(ImDrawList* dl, UiIcon icon, ImVec2 min, float size, ImU32 color);

} // namespace trace
