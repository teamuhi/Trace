#include "app/panel_ui.hpp"

#include <algorithm>
#include <cmath>

#include "app/theme.hpp"
#include "core/panel_layout.hpp"

namespace trace::panel {

namespace {

constexpr int kCardStyleColors = 2;
constexpr int kCardStyleVars = 3;

ImVec4 blend(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
}
ImVec4 withAlpha(ImVec4 c, float alpha) {
    c.w *= alpha;
    return c;
}
ImU32 u32(const ImVec4& c) {
    return ImGui::GetColorU32(c);
}
float em() {
    return ImGui::GetFontSize();
}
float rounding() {
    return std::round(em() * 0.35f);
}

} // namespace

Colors colors() {
    const ThemeTokens& t = tokens();
    Colors c;
    if (isDark()) {
        c.card = blend(t.windowBg, t.text, 0.05f);
        c.track = blend(t.windowBg, ImVec4(0, 0, 0, 1), 0.30f);
    } else {
        c.card = blend(t.windowBg, ImVec4(1, 1, 1, 1), 0.60f);
        c.track = blend(t.windowBg, t.text, 0.08f);
    }
    c.cardBorder = withAlpha(t.text, 0.14f);
    c.rule = withAlpha(t.text, 0.18f);
    c.muted = withAlpha(t.text, 0.62f);
    c.buttonHover = blend(c.track, t.accent, isDark() ? 0.30f : 0.22f);
    return c;
}

void sectionHeader(const char* label) {
    const Colors c = colors();
    if (ImGui::GetCursorPosY() > ImGui::GetCursorStartPos().y + 1.0f) // room above, none at the top of a window
        ImGui::Dummy(ImVec2(0.0f, std::round(em() * 0.3f)));
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float right = start.x + ImGui::GetContentRegionAvail().x;
    const float midY = std::round(start.y + ImGui::GetTextLineHeight() * 0.5f);
    float from = start.x;
    if (label && *label) {
        ImGui::PushStyleColor(ImGuiCol_Text, c.muted);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        from = ImGui::GetItemRectMax().x + std::round(em() * 0.6f);
    } else {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
    }
    if (from < right - em() * 0.5f) // a long label crowds the rule out
        ImGui::GetWindowDrawList()->AddLine(ImVec2(from, midY), ImVec2(right, midY), u32(c.rule), 1.0f);
}

bool beginCard(const char* id, const char* title) {
    const Colors c = colors();
    const float pad = std::round(em() * 0.55f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, c.card);
    ImGui::PushStyleColor(ImGuiCol_Border, c.cardBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    const bool visible = ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
                                           ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (visible && title && *title) {
        ImGui::PushStyleColor(ImGuiCol_Text, c.muted);
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
    }
    return visible;
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(kCardStyleVars);
    ImGui::PopStyleColor(kCardStyleColors);
}

float toolbarButtonSize() {
    return std::round(em() * 2.3f);
}

bool iconButton(const char* id, Icon icon, const char* tooltip, bool selected, bool enabled, ImVec2 size) {
    const Colors c = colors();
    const ThemeTokens& t = tokens();
    if (size.x <= 0.0f)
        size.x = toolbarButtonSize();
    if (size.y <= 0.0f)
        size.y = toolbarButtonSize();

    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImGui::EndDisabled();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const bool focused = enabled && ImGui::IsItemFocused();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 ink = t.text;
    if (selected) {
        ImVec4 fill = t.accent;
        if (held)
            fill = blend(fill, ImVec4(0, 0, 0, 1), 0.18f);
        else if (hovered)
            fill = blend(fill, ImVec4(1, 1, 1, 1), 0.14f);
        dl->AddRectFilled(min, max, u32(withAlpha(fill, enabled ? 1.0f : 0.5f)), rounding());
        ink = contrastText(t.accent);
    } else if (held || hovered) {
        dl->AddRectFilled(min, max, u32(held ? blend(c.buttonHover, t.accent, 0.3f) : c.buttonHover), rounding());
    }
    if (focused)
        dl->AddRect(min, max, u32(withAlpha(t.accent, 0.9f)), rounding(), 0, 2.0f);

    const float side = std::round(std::min({em() * 1.4f, size.x - 4.0f, size.y - 4.0f}));
    if (side > 0.0f) {
        const ImVec2 at(std::round(min.x + (size.x - side) * 0.5f), std::round(min.y + (size.y - side) * 0.5f));
        drawIcon(dl, icon, at, side, u32(withAlpha(ink, enabled ? (hovered || selected ? 1.0f : 0.88f) : 0.38f)));
    }

    if (tooltip && *tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
    return clicked && enabled;
}

int iconToolbar(std::span<const ToolbarItem> items) {
    if (items.empty())
        return -1;
    const Colors c = colors();
    const float size = toolbarButtonSize();
    const float pad = std::round(em() * 0.25f);
    const float gap = std::round(em() * 0.2f);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, size + 2.0f * pad);
    const ToolbarLayout layout = toolbarLayout(width - 2.0f * pad, static_cast<int>(items.size()), size, gap);
    const float height = static_cast<float>(layout.rows) * size + static_cast<float>(layout.rows - 1) * gap + 2.0f * pad;

    ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2(start.x + width, start.y + height), u32(c.track), rounding() + pad * 0.5f);

    int picked = -1;
    ImGui::PushID("##toolbar");
    for (std::size_t i = 0; i < items.size(); ++i) {
        const int col = static_cast<int>(i) % layout.columns;
        const int row = static_cast<int>(i) / layout.columns;
        ImGui::SetCursorScreenPos(ImVec2(start.x + pad + static_cast<float>(col) * (layout.buttonWidth + gap),
                                         start.y + pad + static_cast<float>(row) * (size + gap)));
        const ToolbarItem& item = items[i];
        if (iconButton(item.id, item.icon, item.tooltip, item.selected, item.enabled, ImVec2(layout.buttonWidth, size)))
            picked = static_cast<int>(i);
    }
    ImGui::PopID();
    // Move the cursor under the track with an item, so the window's content grows to include it.
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height));
    ImGui::Dummy(ImVec2(width, 0.0f));
    return picked;
}

} // namespace trace::panel
