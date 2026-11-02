#pragma once

#include <string>

#include "core/symmetry_set.hpp"

namespace trace {

class Toolbox;

// Pixel Art only: the Symmetry section of the Tools panel. The add buttons (+ Mirror X, + Mirror Y, + Rotate) and,
// under them, a rounded panel listing the items of ToolSettings::symmetry. Each row has the item's name (double-click
// or the menu's Rename edits it in place), a kebab menu and a trash button, and shows its position, angle or copy
// count as labeled pills: side by side when the panel is wide, one per line when it is narrow. Everything is disabled while a tool
// gesture runs, and a typed value is dropped if a gesture began meanwhile. Must run between ImGui::Begin and End.
// `canvasWidth` and `canvasHeight` give the units of the position fields (0 hides them).
void drawSymmetrySection(Toolbox& tools, int canvasWidth, int canvasHeight);

// The list name of item `index`: the custom name when the user gave one, else "Mirror X", "Mirror Y" or "Rotate",
// with " (2)", " (3)" for later items of the same kind and preset (see symmetryOrdinal). Empty for an index outside
// the list.
std::string symmetryItemName(const SymmetrySet& set, int index);

// Rows of the list that are expanded, or under the pointer in the panel this frame (bit i is item i): the canvas
// draws those items thicker.
unsigned symmetryEmphasisMask();

// The Mirror X (preset 0) or Mirror Y (preset 1) shortcut: with no such item, adds one; otherwise disables all of
// them when any is enabled, else enables all of them. Returns false when that was refused because the list is full
// or the copies would pass kMaxSymmetryCopies.
bool toggleMirrorPreset(SymmetrySet& set, int preset);

} // namespace trace
