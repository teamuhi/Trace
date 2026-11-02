#include "app/modal_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

#include "app/theme.hpp"
#include "core/dialog_logic.hpp"

namespace trace::modal {

namespace {

// State of the dialog being drawn (only one modal is open at a time).
struct Frame {
    float contentWidth = 0.0f;
    bool typing = false; // a field was being edited when the frame began
};
Frame g;

constexpr int kStyleVars = 4;
constexpr int kStyleColors = 2;
constexpr std::size_t kMaxChoices = 8;

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

// Text at `pos`, cut off at `maxX`.
void textClipped(ImDrawList* dl, ImVec2 pos, float maxX, ImU32 color, const char* text) {
    dl->PushClipRect(ImVec2(pos.x - 1.0f, pos.y - 1.0f), ImVec2(maxX, pos.y + ImGui::GetTextLineHeight() + 1.0f), true);
    dl->AddText(pos, color, text);
    dl->PopClipRect();
}

// Text centered in a box; wider than the box, it starts at the left padding and is cut off at the right.
void textCentered(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, const char* text) {
    const float padX = std::round(em() * 0.4f);
    const float textW = ImGui::CalcTextSize(text).x;
    const float y = std::round(min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f);
    const float x = textW <= max.x - min.x - 2.0f * padX ? std::round(min.x + (max.x - min.x - textW) * 0.5f) : min.x + padX;
    textClipped(dl, ImVec2(x, y), max.x - padX, color, text);
}

// The round hover plate and the - or + of a number field's arrow.
bool stepper(const char* id, ImVec2 min, float side, bool plus, bool atLimit) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(min);
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(side, side));
    ImGui::PopItemFlag();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImVec2 center(min.x + side * 0.5f, min.y + side * 0.5f);
    if (hovered || held)
        dl->AddCircleFilled(center, side * 0.5f - 1.5f, u32(withAlpha(tokens().accent, held ? 0.55f : 0.32f)));
    const float arm = std::round(side * 0.2f);
    const float thick = std::max(1.0f, std::round(em() * 0.1f));
    const ImU32 color = u32(withAlpha(tokens().text, atLimit ? 0.3f : (hovered ? 1.0f : 0.8f)));
    dl->AddLine(ImVec2(center.x - arm, center.y), ImVec2(center.x + arm, center.y), color, thick);
    if (plus)
        dl->AddLine(ImVec2(center.x, center.y - arm), ImVec2(center.x, center.y + arm), color, thick);
    return clicked;
}

// Side of a button's icon and the room it takes before the label (icon plus gap).
float iconSide() {
    return std::round(em() * 1.1f);
}
float iconSpace() {
    return iconSide() + std::round(em() * 0.45f);
}

// One button: returns whether it was clicked. `isDefault` outlines a button that is not already the accent one.
bool drawButton(const char* label, ButtonKind kind, bool enabled, bool isDefault, float width, float height,
                Icon icon = Icon::None) {
    const Colors c = colors();
    const ThemeTokens& t = tokens();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + height);

    ImGui::PushID(label);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton("##button", ImVec2(width, height));
    ImGui::EndDisabled();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused();
    ImGui::PopID();

    ImVec4 fill;
    ImVec4 ink;
    ImVec4 edge = c.border;
    if (!enabled) {
        fill = withAlpha(c.pill, 0.6f);
        ink = withAlpha(t.text, 0.4f);
    } else if (kind == ButtonKind::DangerQuiet) {
        fill = withAlpha(c.danger, held ? 0.32f : hovered ? 0.18f : 0.0f);
        ink = c.danger;
        edge = withAlpha(c.danger, hovered || held ? 1.0f : 0.6f);
    } else if (kind == ButtonKind::Primary || kind == ButtonKind::Danger) {
        const ImVec4 base = kind == ButtonKind::Primary ? t.accent : c.danger;
        fill = held ? blend(base, ImVec4(0, 0, 0, 1), 0.18f) : hovered ? blend(base, ImVec4(1, 1, 1, 1), 0.16f) : base;
        ink = contrastText(base);
        edge = fill;
    } else {
        fill = held ? blend(c.pill, t.accent, 0.35f) : hovered ? blend(c.pill, t.accent, 0.18f) : c.pill;
        ink = t.text;
    }
    const float round = std::round(em() * 0.4f);
    dl->AddRectFilled(min, max, u32(fill), round);
    dl->AddRect(min, max, u32(edge), round);
    if (enabled && (focused || (isDefault && kind == ButtonKind::Secondary)))
        dl->AddRect(ImVec2(min.x - 1.0f, min.y - 1.0f), ImVec2(max.x + 1.0f, max.y + 1.0f), u32(withAlpha(t.accent, 0.9f)),
                    round + 1.0f, 0, 2.0f);
    const float side = iconSide();
    const float groupW = ImGui::CalcTextSize(label).x + iconSpace();
    if (icon != Icon::None && groupW <= width - 2.0f * std::round(em() * 0.4f)) {
        // Icon and label centered together as one group.
        const float x = std::round(min.x + (width - groupW) * 0.5f);
        drawIcon(dl, icon, ImVec2(x, std::round(min.y + (height - side) * 0.5f)), side, u32(ink));
        textClipped(dl, ImVec2(x + iconSpace(), std::round(min.y + (height - ImGui::GetTextLineHeight()) * 0.5f)), max.x,
                    u32(ink), label);
    } else {
        textCentered(dl, min, max, u32(ink), label);
    }
    return clicked && enabled;
}

float buttonHeight() {
    return ImGui::GetFrameHeight() + std::round(em() * 0.3f);
}

} // namespace

Colors colors() {
    const ThemeTokens& t = tokens();
    Colors c;
    if (isDark()) {
        c.panel = blend(t.windowBg, t.text, 0.07f);
        c.row = blend(t.windowBg, ImVec4(0, 0, 0, 1), 0.30f);
        c.rowHover = blend(c.row, t.text, 0.07f);
        c.pill = blend(c.row, t.text, 0.12f);
        c.input = blend(c.row, ImVec4(0, 0, 0, 1), 0.40f);
        c.danger = ImVec4(240.0f / 255.0f, 100.0f / 255.0f, 100.0f / 255.0f, 1.0f);
    } else {
        c.panel = blend(t.windowBg, t.text, 0.06f);
        c.row = blend(t.windowBg, ImVec4(1, 1, 1, 1), 0.75f);
        c.rowHover = blend(c.row, t.accent, 0.10f);
        c.pill = blend(c.row, t.text, 0.09f);
        c.input = ImVec4(1, 1, 1, 1);
        c.danger = ImVec4(200.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f);
    }
    c.border = withAlpha(t.text, 0.14f);
    c.muted = withAlpha(t.text, 0.62f);
    return c;
}

// --- frame -------------------------------------------------------------------------------------

bool begin(const char* label, float widthEm, bool* close) {
    const float e = em();
    const float pad = std::round(e * 1.1f);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const Colors c = colors();

    g.contentWidth = std::floor(std::min(e * widthEm, std::max(e * 8.0f, viewport->Size.x * 0.92f - 2.0f * pad)));

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, std::round(pad * 0.9f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, std::round(e * 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(std::round(e * 0.6f), std::round(e * 0.5f)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, c.panel);
    ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(tokens().text, 0.28f));

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove;
    if (!ImGui::BeginPopupModal(label, nullptr, flags)) {
        ImGui::PopStyleColor(kStyleColors);
        ImGui::PopStyleVar(kStyleVars);
        return false;
    }
    g.typing = ImGui::IsAnyItemActive();

    // The window is as wide as its content: reserve the width up front so every row can fill it.
    const float y = ImGui::GetCursorPosY();
    ImGui::Dummy(ImVec2(g.contentWidth, 0.0f));
    ImGui::SetCursorPosY(y);

    // Title: the part of the label before the ### that keeps the popup's identity.
    const char* hash = std::strstr(label, "###");
    const std::string title = hash ? std::string(label, hash) : std::string(label);
    const float closeSide = close ? std::round(e * 1.6f) : 0.0f;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + g.contentWidth - (close ? closeSide + std::round(e * 0.4f) : 0.0f));
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (close) {
        // An X at the top right: the way out that is not one of the footer's choices. Escape does the same.
        const ImVec2 after = ImGui::GetCursorScreenPos();
        const ImVec2 titleMin = ImGui::GetItemRectMin();
        const float titleH = ImGui::GetItemRectMax().y - titleMin.y;
        const ImVec2 min(titleMin.x + g.contentWidth - closeSide, titleMin.y + std::round((titleH - closeSide) * 0.5f));
        ImGui::SetCursorScreenPos(min);
        if (ImGui::InvisibleButton("##close", ImVec2(closeSide, closeSide)))
            *close = true;
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hovered || held)
            dl->AddCircleFilled(ImVec2(min.x + closeSide * 0.5f, min.y + closeSide * 0.5f), closeSide * 0.5f,
                                u32(withAlpha(tokens().accent, held ? 0.55f : 0.32f)));
        const float icon = std::round(closeSide * 0.55f);
        drawIcon(dl, Icon::XClose, ImVec2(std::round(min.x + (closeSide - icon) * 0.5f), std::round(min.y + (closeSide - icon) * 0.5f)),
                 icon, u32(withAlpha(tokens().text, hovered || held ? 1.0f : 0.75f)));
        ImGui::SetCursorScreenPos(after);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            *close = true;
    }
    const ImVec2 rule = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(rule.x, rule.y), ImVec2(rule.x + g.contentWidth, rule.y), u32(c.border), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, std::round(e * 0.15f)));
    return true;
}

void end() {
    ImGui::EndPopup();
    ImGui::PopStyleColor(kStyleColors);
    ImGui::PopStyleVar(kStyleVars);
}

float contentWidth() {
    return g.contentWidth;
}

// --- text --------------------------------------------------------------------------------------

void heading(const char* text) {
    const Colors c = colors();
    ImGui::Dummy(ImVec2(0.0f, std::round(em() * 0.1f)));
    ImGui::PushStyleColor(ImGuiCol_Text, c.muted);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const float from = itemMax.x + std::round(em() * 0.6f);
    if (from < right - em())
        ImGui::GetWindowDrawList()->AddLine(ImVec2(from, std::round(itemMin.y + ImGui::GetTextLineHeight() * 0.5f)),
                                            ImVec2(right, std::round(itemMin.y + ImGui::GetTextLineHeight() * 0.5f)),
                                            u32(c.border), 1.0f);
}

void paragraph(const char* text, bool muted) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    if (muted) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors().muted);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted(text);
    }
    ImGui::PopTextWrapPos();
}

void notice(const char* text, bool warning) {
    const float e = em();
    const float bar = std::max(2.0f, std::round(e * 0.22f));
    const float gap = std::round(e * 0.7f);
    const float left = ImGui::GetCursorPosX();
    const float right = left + ImGui::GetContentRegionAvail().x;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::SetCursorPosX(left + bar + gap);
    ImGui::PushTextWrapPos(right);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    const float bottom = ImGui::GetItemRectMax().y;
    const ImVec4 color = warning ? tokens().warning : tokens().accent;
    ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2(start.x + bar, bottom), u32(color), bar * 0.5f);
}

void valuePlate(const char* text) {
    const Colors c = colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetFrameHeight();
    const float width = std::max(h, ImGui::GetContentRegionAvail().x);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + h);
    dl->AddRectFilled(min, max, u32(c.row), std::round(em() * 0.4f));
    textClipped(dl, ImVec2(min.x + std::round(em() * 0.7f), min.y + ImGui::GetStyle().FramePadding.y), max.x - std::round(em() * 0.5f),
                u32(tokens().text), text);
    ImGui::Dummy(ImVec2(width, h));
}

// --- form rows ---------------------------------------------------------------------------------

Form::Form(std::initializer_list<const char*> labels) {
    labelX_ = ImGui::GetCursorPosX();
    width_ = ImGui::GetContentRegionAvail().x;
    gap_ = std::round(em() * 0.8f);
    float widest = 0.0f;
    for (const char* label : labels) {
        if (label && label[0] != '\0')
            widest = std::max(widest, ImGui::CalcTextSize(label).x);
    }
    labelWidth_ = std::min(std::ceil(widest), std::floor(width_ * 0.4f));
}

float Form::row(const char* label) {
    ImGui::AlignTextToFramePadding();
    if (label && label[0] != '\0') {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::CalcTextSize(label).x <= labelWidth_) {
            ImGui::PushStyleColor(ImGuiCol_Text, colors().muted);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
        } else {
            textClipped(ImGui::GetWindowDrawList(), ImVec2(p.x, p.y + ImGui::GetStyle().FramePadding.y), p.x + labelWidth_,
                        u32(colors().muted), label);
            ImGui::Dummy(ImVec2(labelWidth_, ImGui::GetFrameHeight()));
            ImGui::SetItemTooltip("%s", label);
        }
    } else {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    }
    ImGui::SameLine(labelX_ + labelWidth_ + gap_, 0.0f);
    return width_ - labelWidth_ - gap_;
}

// --- fields ------------------------------------------------------------------------------------

bool numberField(const char* id, int& value, int lo, int hi, const char* unit, int step, int fastStep, float width) {
    const Colors c = colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetFrameHeight();
    if (width <= 0.0f)
        width = std::min(ImGui::GetContentRegionAvail().x, em() * 14.0f);
    width = std::max(width, h * 3.5f);

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + h);
    dl->AddRectFilled(min, max, u32(c.input), h * 0.5f);
    dl->AddRect(min, max, u32(c.border), h * 0.5f);

    ImGui::PushID(id);
    bool changed = false;
    const int delta = ImGui::GetIO().KeyCtrl ? fastStep : step;
    if (stepper("##dec", min, h, false, value <= lo)) {
        value = steppedValue(value, -delta, lo, hi);
        changed = true;
    }
    if (stepper("##inc", ImVec2(max.x - h, min.y), h, true, value >= hi)) {
        value = steppedValue(value, delta, lo, hi);
        changed = true;
    }

    const float unitWidth = unit[0] != '\0' ? ImGui::CalcTextSize(unit).x + std::round(em() * 0.3f) : 0.0f;
    ImGui::SetCursorScreenPos(ImVec2(min.x + h, min.y));
    ImGui::SetNextItemWidth(std::max(1.0f, width - 2.0f * h - unitWidth));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    changed |= ImGui::InputInt("##value", &value, 0, 0);
    const bool active = ImGui::IsItemActive();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    value = std::clamp(value, lo, hi);

    if (unitWidth > 0.0f)
        dl->AddText(ImVec2(max.x - h - unitWidth, min.y + ImGui::GetStyle().FramePadding.y), u32(c.muted), unit);
    if (active)
        dl->AddRect(min, max, u32(withAlpha(tokens().accent, 0.9f)), h * 0.5f, 0, 1.5f);

    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(width, h));
    ImGui::PopID();
    return changed;
}

bool slider(const char* id, int& value, int lo, int hi, const char* format) {
    const Colors c = colors();
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, em() * 14.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, c.input);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
    const bool changed = ImGui::SliderInt(id, &value, lo, hi, format);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    value = std::clamp(value, lo, hi); // Ctrl+click can type any number
    return changed;
}

bool checkbox(const char* label, bool& value) {
    const Colors c = colors();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, c.input);
    ImGui::PushStyleColor(ImGuiCol_Border, c.border);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool changed = ImGui::Checkbox(label, &value);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    return changed;
}

// --- choices -----------------------------------------------------------------------------------

int choices(const char* id, int selected, std::initializer_list<const char*> labels, unsigned disabledMask) {
    const Colors c = colors();
    const ThemeTokens& t = tokens();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetFrameHeight();
    const float padX = std::round(em() * 0.9f);
    const float gap = std::round(em() * 0.4f);
    const float startX = ImGui::GetCursorPosX();
    const float avail = ImGui::GetContentRegionAvail().x;

    int result = -1;
    float x = 0.0f; // used width on the current line
    int index = 0;
    ImGui::PushID(id);
    for (const char* label : labels) {
        if (static_cast<std::size_t>(index) >= kMaxChoices)
            break;
        const float width = std::min(avail, std::ceil(ImGui::CalcTextSize(label).x) + 2.0f * padX);
        if (index > 0) {
            if (x + gap + width <= avail) {
                ImGui::SameLine(0.0f, gap);
                x += gap;
            } else {
                ImGui::SetCursorPosX(startX);
                x = 0.0f;
            }
        }
        const bool enabled = (disabledMask & (1u << static_cast<unsigned>(index))) == 0;
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + width, min.y + h);
        ImGui::PushID(index);
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::InvisibleButton("##choice", ImVec2(width, h));
        ImGui::EndDisabled();
        const bool hovered = enabled && ImGui::IsItemHovered();
        const bool on = index == selected;
        ImGui::PopID();

        const float round = h * 0.5f;
        if (on) {
            dl->AddRectFilled(min, max, u32(withAlpha(t.accent, enabled ? 1.0f : 0.5f)), round);
        } else {
            dl->AddRectFilled(min, max, u32(hovered ? blend(c.pill, t.accent, 0.2f) : c.pill), round);
            dl->AddRect(min, max, u32(c.border), round);
        }
        const ImVec4 ink = on ? contrastText(t.accent) : t.text;
        textCentered(dl, min, max, u32(withAlpha(ink, enabled ? 1.0f : 0.4f)), label);
        if (clicked && enabled)
            result = index;
        x += width;
        ++index;
    }
    ImGui::PopID();
    return result;
}

void drawSwatch(ImDrawList* dl, ImVec2 min, ImVec2 max, const ImVec4& color) {
    const float round = std::round(em() * 0.2f);
    if (color.w < 1.0f) {
        const float cell = std::max(3.0f, std::round(em() * 0.35f));
        dl->AddRectFilled(min, max, IM_COL32(255, 255, 255, 255), round);
        dl->PushClipRect(min, max, true);
        for (float y = min.y; y < max.y; y += cell) {
            for (float x = min.x; x < max.x; x += cell) {
                const int column = static_cast<int>((x - min.x) / cell);
                const int row = static_cast<int>((y - min.y) / cell);
                if (((column + row) & 1) != 0)
                    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cell, y + cell), IM_COL32(190, 190, 190, 255));
            }
        }
        dl->PopClipRect();
    }
    if (color.w > 0.0f)
        dl->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(color), round);
    dl->AddRect(min, max, u32(withAlpha(tokens().text, 0.35f)), round);
}

bool radioRow(const char* id, bool selected, const char* label, const ImVec4* swatch) {
    const Colors c = colors();
    const ThemeTokens& t = tokens();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float e = em();
    const float h = ImGui::GetFrameHeight() + std::round(e * 0.3f);
    const float width = std::max(h, ImGui::GetContentRegionAvail().x);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + h);

    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool focused = ImGui::IsItemFocused();

    const float round = std::round(e * 0.4f);
    dl->AddRectFilled(min, max, u32(hovered ? c.rowHover : c.row), round);
    if (selected)
        dl->AddRect(min, max, u32(withAlpha(t.accent, 0.9f)), round, 0, 1.5f);
    else if (focused)
        dl->AddRect(min, max, u32(withAlpha(t.accent, 0.6f)), round);

    const ImVec2 center(min.x + e * 1.0f, min.y + h * 0.5f);
    dl->AddCircle(center, e * 0.4f, u32(selected ? t.accent : withAlpha(t.text, 0.6f)), 0, 1.5f);
    if (selected)
        dl->AddCircleFilled(center, e * 0.2f, u32(t.accent));

    float right = max.x - e * 0.6f;
    if (swatch) {
        const float sw = std::round(e * 1.6f);
        const float sh = std::round(e * 1.0f);
        const ImVec2 smin(right - sw, min.y + (h - sh) * 0.5f);
        drawSwatch(dl, smin, ImVec2(smin.x + sw, smin.y + sh), *swatch);
        right = smin.x - std::round(e * 0.5f);
    }
    textClipped(dl, ImVec2(min.x + e * 1.9f, min.y + (h - ImGui::GetTextLineHeight()) * 0.5f), right, u32(t.text), label);
    return clicked;
}

// --- buttons -----------------------------------------------------------------------------------

bool button(const char* label, ButtonKind kind, float width) {
    if (width <= 0.0f)
        width = std::ceil(ImGui::CalcTextSize(label).x) + 2.0f * std::round(em() * 0.9f);
    return drawButton(label, kind, true, false, width, ImGui::GetFrameHeight());
}

int footer(std::initializer_list<FooterButton> buttons) {
    const float e = em();
    const Colors c = colors();
    const float height = buttonHeight();
    const float gap = std::round(e * 0.6f);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float startX = ImGui::GetCursorPosX();
    const int count = static_cast<int>(buttons.size());

    float widest = 0.0f;
    for (const FooterButton& b : buttons)
        widest = std::max(widest, ImGui::CalcTextSize(b.label).x + (b.icon != Icon::None ? iconSpace() : 0.0f));
    const float width = std::max(std::round(e * 6.5f), std::ceil(widest) + 2.0f * std::round(e * 1.2f));
    const float total = static_cast<float>(count) * width + static_cast<float>(std::max(0, count - 1)) * gap;
    const bool stacked = total > avail;

    ImGui::Dummy(ImVec2(0.0f, std::round(e * 0.2f)));
    const ImVec2 rule = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(rule.x, rule.y), ImVec2(rule.x + avail, rule.y), u32(c.border), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, std::round(e * 0.1f)));

    int pressed = -1;
    int enterIndex = -1;
    int escapeIndex = -1;
    const auto draw = [&](int i, const FooterButton& b, float w) {
        if (drawButton(b.label, b.kind, b.enabled, b.enter, w, height, b.icon) && pressed < 0)
            pressed = i;
        if (b.enter) {
            ImGui::SetItemDefaultFocus();
            if (b.enabled)
                enterIndex = i;
        }
        if (b.escape)
            escapeIndex = i;
    };

    if (!stacked) {
        ImGui::SetCursorPosX(startX + avail - total);
        int i = 0;
        for (const FooterButton& b : buttons) {
            if (i > 0)
                ImGui::SameLine(0.0f, gap);
            draw(i, b, width);
            ++i;
        }
    } else {
        // Too narrow for one line: full-width buttons, the last (primary) one on top.
        const FooterButton* list = buttons.begin();
        for (int i = count - 1; i >= 0; --i)
            draw(i, list[i], avail);
    }

    if (pressed < 0 && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        pressed = escapeIndex;
    if (pressed < 0 && !g.typing && enterIndex >= 0 &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
        pressed = enterIndex;
    return pressed;
}

} // namespace trace::modal
