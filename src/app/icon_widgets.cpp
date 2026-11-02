#include "app/icon_widgets.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "app/theme.hpp"

namespace trace {

namespace {

float iconSide() {
    return std::round(ImGui::GetFontSize());
}

float iconGap() {
    return std::round(ImGui::GetFontSize() * 0.4f);
}

// Blank text as wide as an icon: ImGui reserves a column of this width at the left of the menu entry, puts its own
// spacing behind it, and the icon is drawn over the column.
std::string menuColumnText() {
    const float space = std::max(1.0f, ImGui::CalcTextSize(" ").x);
    const int count = static_cast<int>(std::ceil(iconSide() / space));
    return std::string(static_cast<std::size_t>(std::max(count, 1)), ' ');
}

// An icon in the menu column at `pos`, level with the text of the row.
// `dl` is the draw list of the window that holds the entry: after BeginMenu opens a submenu the current window is
// the submenu's popup, so the entry's own list has to be taken before the call.
void drawMenuIcon(ImDrawList* dl, Icon icon, ImVec2 pos, bool enabled, int quarterTurns = 0) {
    const float side = iconSide();
    const float alpha = enabled ? 1.0f : ImGui::GetStyle().DisabledAlpha;
    drawIcon(dl, icon, ImVec2(pos.x, pos.y + std::round((ImGui::GetTextLineHeight() - side) * 0.5f)),
             side, ImGui::GetColorU32(ImGuiCol_Text, alpha), quarterTurns);
}

} // namespace

bool iconMenuItem(Icon icon, const char* label, const char* shortcut, bool* selected, bool enabled, int quarterTurns) {
    if (icon == Icon::None) // ImGui lines the label up with the entries that have an icon by itself
        return ImGui::MenuItem(label, shortcut, selected, enabled);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const std::string column = menuColumnText();
    const bool clicked = ImGui::MenuItemEx(label, column.c_str(), shortcut, selected ? *selected : false, enabled);
    if (clicked && selected)
        *selected = !*selected;
    drawMenuIcon(dl, icon, pos, enabled, quarterTurns);
    return clicked;
}

bool iconBeginMenu(Icon icon, const char* label, bool enabled) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList(); // before BeginMenuEx: an open submenu makes its popup the current window
    const std::string column = menuColumnText();
    const bool open = ImGui::BeginMenuEx(label, column.c_str(), enabled);
    drawMenuIcon(dl, icon, pos, enabled);
    return open;
}

float iconTextButtonWidth(const char* label) {
    float content = iconSide();
    if (label && *label)
        content += iconGap() + ImGui::CalcTextSize(label).x;
    return content + ImGui::GetStyle().FramePadding.x * 2.0f;
}

bool iconTextButton(const char* id, Icon icon, const char* label, ImVec2 size, bool active) {
    const bool hasLabel = label && *label;
    const float side = iconSide();
    const float content = side + (hasLabel ? iconGap() + ImGui::CalcTextSize(label).x : 0.0f);
    if (size.x == 0.0f)
        size.x = content + ImGui::GetStyle().FramePadding.x * 2.0f;
    if (size.y == 0.0f)
        size.y = ImGui::GetFrameHeight();

    ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (active) {
        const ThemeTokens& t = tokens();
        ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent);
        textColor = contrastText(t.accent);
    }
    ImGui::PushID(id);
    const bool clicked = ImGui::Button("##icon", size); // blank label: the icon and text are drawn below
    ImGui::PopID();
    if (active)
        ImGui::PopStyleColor(3);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImU32 color = ImGui::GetColorU32(textColor); // follows the dimming of BeginDisabled
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = std::round(min.x + (max.x - min.x - content) * 0.5f);
    drawIcon(dl, icon, ImVec2(x, min.y + std::round((max.y - min.y - side) * 0.5f)), side, color);
    if (hasLabel) {
        x += side + iconGap();
        dl->AddText(ImVec2(x, min.y + std::round((max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f)), color, label);
    }
    return clicked;
}

} // namespace trace
