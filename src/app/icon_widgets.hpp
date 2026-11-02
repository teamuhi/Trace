#pragma once

#include <imgui.h>

#include "app/svg_icons.hpp"

namespace trace {

// Widgets that carry an icon, so every panel and menu shows the same icon the same way. Icons take their color
// from the theme's text color (and dim with the widget when it is disabled).

// A menu entry with its icon in the column at the left. All entries of one menu line their labels up behind the
// widest icon column, so entries without an icon still keep that room blank. `quarterTurns` rotates the icon
// clockwise (see drawIcon). Returns what ImGui::MenuItem returns.
bool iconMenuItem(Icon icon, const char* label, const char* shortcut = nullptr, bool* selected = nullptr,
                  bool enabled = true, int quarterTurns = 0);

// A submenu entry with an icon; pair it with ImGui::EndMenu like ImGui::BeginMenu.
bool iconBeginMenu(Icon icon, const char* label, bool enabled = true);

// A button showing the icon and, when `label` is not null, the label after it. `id` is the button's ImGui id (it is
// not shown). A zero `size` fits the content; a width of `-FLT_MIN` spans the available width. `active` draws the
// button as pressed in, the way a toggle looks while it is on.
bool iconTextButton(const char* id, Icon icon, const char* label = nullptr, ImVec2 size = ImVec2(0.0f, 0.0f),
                    bool active = false);

// The width a fitted iconTextButton takes with this label (just the icon when `label` is null).
float iconTextButtonWidth(const char* label = nullptr);

} // namespace trace
