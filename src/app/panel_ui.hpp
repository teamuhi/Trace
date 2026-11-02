#pragma once

#include <imgui.h>

#include <span>

#include "app/svg_icons.hpp"

namespace trace::panel {

// The shared look of the side panels (Layers, Tools, Shapes, Arrange, ...): a heading with a rule, grouped rounded
// cards and a row of icon-only buttons with tooltips. Colors come from the theme tokens, sizes follow the font size,
// so panels hold up in both themes, at any UI scale and in any language.
//
//   panel::sectionHeader("Layers");
//   ... list ...
//   if (panel::beginCard("##props", "Layer")) {
//       ... controls ...
//   }
//   panel::endCard();                      // always, even when beginCard returned false
//   const panel::ToolbarItem items[] = {{"##new", Icon::AddLayer, "New layer"}, {"##del", Icon::Delete, "Delete", true, false}};
//   const int picked = panel::iconToolbar(items);   // index clicked this frame, or -1

// Colors derived from the theme tokens for the current theme.
struct Colors {
    ImVec4 card;       // the fill of a card
    ImVec4 cardBorder;
    ImVec4 track;      // the groove an icon toolbar sits in
    ImVec4 rule;       // heading rules and separators
    ImVec4 muted;      // headings and hints
    ImVec4 buttonHover;
};
Colors colors();

// A heading in muted text with a thin rule running to the right edge, with a little room above (none at the very top
// of a window). An empty label draws just the rule.
void sectionHeader(const char* label);

// A rounded frame that groups related controls, optionally with a small title. Items inside use the card's inner
// width. Always pair with endCard, whatever beginCard returns.
bool beginCard(const char* id, const char* title = nullptr);
void endCard();

// One icon-only button: the icon centered in a rounded plate, the label as a hover tooltip (also shown while it is
// disabled). `selected` draws it pressed in on the accent, like a toggle that is on. A zero `size` is square at the
// toolbar size (see toolbarButtonSize). Returns whether it was clicked (never while disabled).
bool iconButton(const char* id, Icon icon, const char* tooltip, bool selected = false, bool enabled = true,
                ImVec2 size = ImVec2(0.0f, 0.0f));

struct ToolbarItem {
    const char* id;      // ImGui id, not shown
    Icon icon;
    const char* tooltip; // may be null
    bool enabled = true;
    bool selected = false;
};

// The height of a toolbar button (and the minimum width): scales with the font.
float toolbarButtonSize();

// The items in one grouped track, sharing the available width evenly and wrapping to more rows in a narrow panel.
// Returns the index of the item clicked, or -1. Needs no more than a few dozen items.
int iconToolbar(std::span<const ToolbarItem> items);

} // namespace trace::panel
