#include "app/symmetry_panel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "app/icon_widgets.hpp"
#include "app/theme.hpp"
#include "app/tools_panel.hpp"
#include "app/tool_icons.hpp"
#include "core/i18n.hpp"
#include "core/symmetry.hpp"
#include "tools/toolbox.hpp"

namespace trace {

namespace {

// --- state -------------------------------------------------------------------------------------

constexpr int kFieldsPerItem = 3; // posX, posY, angle: one text buffer each
constexpr int kRotateCounts[] = {2, 3, 4, 6, 8};

// Plain UI state of the list: which row the pointer is on and the units of the position fields.
struct ListState {
    bool percent = false;  // position fields in percent of the canvas instead of pixels (one setting for the list)
    int hover = -1;        // the row under the pointer this frame
    int hoverFrame = -10;
    std::array<bool, kMaxSymmetryItems> rowHot{}; // last frame's hover, to tint the row before it is drawn
    // The inline rename: the row whose name is a text field (-1 for none), its text, and the frames it began and was
    // last drawn on (a rename that stops being drawn, e.g. the panel was collapsed, is dropped).
    int editIndex = -1;
    bool editFocus = false;
    int editStartFrame = 0;
    int editSeenFrame = 0;
    SymmetryName editBuf{};
};
ListState g_list;

unsigned bitOf(int i) {
    return 1u << static_cast<unsigned>(i);
}

// --- colors ------------------------------------------------------------------------------------

ImVec4 blend(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
}
ImVec4 withAlpha(ImVec4 c, float alpha) {
    c.w *= alpha;
    return c;
}

// A panel a little off the window color with darker rows (light cards in the light theme), pills for the fields and a
// red for the destructive buttons. No theme token is red, so the red is fixed per theme.
struct Colors {
    ImVec4 panel;
    ImVec4 row;
    ImVec4 rowHover;
    ImVec4 pill;
    ImVec4 input;
    ImVec4 border;
    ImVec4 borderHot; // the row border under the pointer
    ImVec4 danger;
};
Colors makeColors() {
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
    c.borderHot = withAlpha(t.accent, 0.7f);
    return c;
}

// --- names -------------------------------------------------------------------------------------

// The automatic name of item `index`: "Mirror X", "Mirror Y" or "Rotate", numbered "(2)", "(3)" after the first.
std::string autoName(const SymmetrySet& set, int index) {
    if (index < 0 || index >= set.count || index >= kMaxSymmetryItems)
        return {};
    const SymmetryItem& it = set.items[static_cast<std::size_t>(index)];
    const bool rotate = it.kind == SymmetryKind::Rotate;
    const std::string base = tr(rotate ? Str::SymNameRotate : (it.preset == 1 ? Str::SymNameMirrorY : Str::SymNameMirrorX));
    const int ordinal = symmetryOrdinal(set, index);
    return ordinal <= 1 ? base : trf(Str::SymNameNumbered, {base, std::to_string(ordinal)});
}

// Shortens `text` with "..." until it fits `width` (a whole UTF-8 character at a time). Returns whether it was cut.
bool fitWithEllipsis(std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
        return false;
    constexpr const char* kEllipsis = "...";
    while (!text.empty()) {
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0u) == 0x80u)
            text.pop_back(); // the continuation bytes of the last character, then its lead byte
        if (!text.empty())
            text.pop_back();
        if (ImGui::CalcTextSize((text + kEllipsis).c_str()).x <= width)
            break;
    }
    text += kEllipsis;
    return true;
}

void beginRename(const SymmetrySet& set, int index) {
    g_list.editIndex = index;
    g_list.editFocus = true;
    g_list.editStartFrame = g_list.editSeenFrame = ImGui::GetFrameCount();
    g_list.editBuf = cleanSymmetryName(symmetryItemName(set, index));
}

// Applies the typed text: unchanged keeps the item as it is; empty (or the automatic name) goes back to the automatic name.
void commitRename(SymmetrySet& set, int index) {
    if (index < 0 || index >= set.count)
        return;
    const SymmetryName typed = cleanSymmetryName(symmetryNameView(g_list.editBuf));
    const std::string_view text = symmetryNameView(typed);
    SymmetryItem& it = set.items[static_cast<std::size_t>(index)];
    if (text == symmetryItemName(set, index))
        return;
    if (text.empty() || text == autoName(set, index))
        it.name = SymmetryName{};
    else
        it.name = typed;
}

// --- number fields -----------------------------------------------------------------------------

struct FieldSlot {
    double value = 0.0; // the unrounded value while the field is being dragged or typed in
    bool active = false;
};
std::array<FieldSlot, kMaxSymmetryItems * kFieldsPerItem> g_slots;

// Forgets any drag in progress and drops the keyboard focus: the rows were reordered, so a slot would otherwise
// carry its value over to another item.
void resetFields() {
    for (FieldSlot& f : g_slots)
        f.active = false;
    g_list.editIndex = -1;
    ImGui::ClearActiveID();
}

// A number field you drag left or right to change (double-click or Ctrl+click to type a value). Returns the new
// value as it changes, so an axis follows the drag live. While active it keeps the unrounded value, so a slow drag
// still moves even when the stored value only has half-pixel steps; otherwise it shows `shown`, which follows an item
// dragged on the canvas. lo == hi means unbounded.
std::optional<double> dragField(int slot, const char* id, double shown, float speed, double lo, double hi,
                                const char* format, float width, bool allowCommit) {
    FieldSlot& f = g_slots[static_cast<std::size_t>(slot)];
    double v = f.active ? f.value : shown;

    ImGui::SetNextItemWidth(width);
    const ImGuiSliderFlags flags = ImGuiSliderFlags_NoRoundToFormat | (lo < hi ? ImGuiSliderFlags_AlwaysClamp : 0);
    const bool changed = ImGui::DragScalar(id, ImGuiDataType_Double, &v, speed, &lo, &hi, format, flags);
    f.active = ImGui::IsItemActive();
    if (f.active)
        f.value = v;
    if (changed && allowCommit && std::isfinite(v))
        return v;
    return std::nullopt;
}

// --- pills -------------------------------------------------------------------------------------

struct PillMetrics {
    float padX;
    float padY;
    float gap;
    float height;
};
PillMetrics pillMetrics() {
    const float em = ImGui::GetFontSize();
    PillMetrics m;
    m.padX = std::round(em * 0.55f);
    m.padY = std::round(em * 0.18f);
    m.gap = std::round(em * 0.3f);
    m.height = ImGui::GetFrameHeight() + 2.0f * m.padY;
    return m;
}

float minInputWidth() {
    return ImGui::GetFontSize() * 2.6f; // room for "33.5" or "180"
}

// The width the unit column of a stacked pill reserves: the widest unit ("px", "%" or the degree sign) and a gap.
float stackedUnitSlot() {
    const float widest = std::max({ImGui::CalcTextSize("px").x, ImGui::CalcTextSize("%").x, ImGui::CalcTextSize("\xC2\xB0").x});
    return widest + pillMetrics().gap;
}

// Starts a pill of width `w` at the cursor: the rounded plate and its muted label. The caller draws the input at
// the returned spot, then calls endPill. Two layouts:
//  - inline (`stacked` false): "label [value] unit" side by side with the other pills of the row. A pill that is too
//    narrow drops its unit first and then its label (the tooltips still name the field).
//  - stacked (the pills are one per line, each as wide as the row): the label at the left, then the value and the unit
//    in a right-aligned block of fixed width, so values and units line up from one pill (and one row) to the next.
struct PillLayout {
    ImVec2 min;
    ImVec2 inputPos;
    ImVec2 unitPos;
    float inputWidth;
    bool showUnit;
};
PillLayout beginPill(const Colors& c, const char* label, const char* unit, float w, bool stacked) {
    const PillMetrics m = pillMetrics();
    const ThemeTokens& t = tokens();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    PillLayout p;
    p.min = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p.min, ImVec2(p.min.x + w, p.min.y + m.height), ImGui::GetColorU32(c.pill), m.height * 0.5f);

    const float labelW = ImGui::CalcTextSize(label).x;
    const float unitW = unit[0] != '\0' ? ImGui::CalcTextSize(unit).x : 0.0f;
    const float minInput = minInputWidth();
    const float textY = p.min.y + m.padY;
    bool showLabel = true;
    float unitSpace = 0.0f;
    float input = 0.0f;
    if (stacked) {
        unitSpace = stackedUnitSlot(); // reserved even for a pill without a unit, so the inputs line up
        input = std::min(ImGui::GetFontSize() * 5.5f, w - m.padX * 2.0f - labelW - m.gap - unitSpace);
        p.showUnit = unitW > 0.0f;
        showLabel = input >= minInput;
        if (!showLabel)
            input = w - m.padX * 2.0f - unitSpace;
        p.inputWidth = std::max(1.0f, std::floor(input));
        const float inputX = p.min.x + w - m.padX - unitSpace - p.inputWidth;
        p.inputPos = ImVec2(inputX, textY);
        p.unitPos = ImVec2(inputX + p.inputWidth + m.gap, textY);
    } else {
        unitSpace = unitW > 0.0f ? unitW + m.gap : 0.0f;
        input = w - m.padX * 2.0f - labelW - m.gap - unitSpace;
        p.showUnit = unitW > 0.0f;
        if (input < minInput && p.showUnit) { // drop the unit first
            p.showUnit = false;
            unitSpace = 0.0f;
            input = w - m.padX * 2.0f - labelW - m.gap;
        }
        showLabel = input >= minInput;
        if (!showLabel)
            input = w - m.padX * 2.0f - unitSpace;
        p.inputWidth = std::max(1.0f, std::floor(input));
        const float inputX = p.min.x + m.padX + (showLabel ? labelW + m.gap : 0.0f);
        p.inputPos = ImVec2(inputX, textY);
        p.unitPos = ImVec2(inputX + p.inputWidth + m.gap, textY);
    }

    if (showLabel) {
        ImGui::SetCursorScreenPos(ImVec2(p.min.x + m.padX, textY));
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, withAlpha(t.text, 0.62f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
    }
    ImGui::SetCursorScreenPos(p.inputPos);
    return p;
}

void endPill(const PillLayout& p, const char* unit, float w) {
    const PillMetrics m = pillMetrics();
    if (p.showUnit) {
        ImGui::SetCursorScreenPos(p.unitPos);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, withAlpha(tokens().text, 0.62f));
        ImGui::TextUnformatted(unit);
        ImGui::PopStyleColor();
    }
    ImGui::SetCursorScreenPos(p.min);
    ImGui::Dummy(ImVec2(w, m.height));
}

// A pill with a draggable number: "label: [value] unit". Returns the value while it changes.
std::optional<double> numberPill(const Colors& c, int slot, const char* label, double shown, const char* unit,
                                 float speed, double lo, double hi, const char* format, float w, bool stacked,
                                 bool allowCommit, const char* tip) {
    const PillLayout p = beginPill(c, label, unit, w, stacked);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, c.input);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
    ImGui::PushID(slot);
    const auto changed = dragField(slot, "##v", shown, speed, lo, hi, format, p.inputWidth, allowCommit);
    ImGui::SetItemTooltip("%s", tip);
    ImGui::PopID();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    endPill(p, unit, w);
    return changed;
}

// --- list operations ---------------------------------------------------------------------------

struct Op {
    enum class Kind { None, Remove, Toggle, Duplicate, Center, Up, Down, Clear } kind = Kind::None;
    int index = -1;
};

bool canEnable(const SymmetrySet& set, int index) {
    SymmetrySet next = set;
    next.items[static_cast<std::size_t>(index)].enabled = true;
    return symmetryCopyEstimate(next) <= kMaxSymmetryCopies;
}

bool atCenter(const SymmetryItem& it, int width, int height) {
    return resolveAxis(it.posX, width) == width && resolveAxis(it.posY, height) == height;
}

void applyOp(SymmetrySet& set, const Op& op) {
    if (op.kind == Op::Kind::None)
        return;
    if (op.kind == Op::Kind::Clear) {
        set = SymmetrySet{};
        resetFields();
        return;
    }
    const int i = op.index;
    if (i < 0 || i >= set.count)
        return;
    auto& items = set.items;
    const auto at = [&items](int k) -> SymmetryItem& { return items[static_cast<std::size_t>(k)]; };
    switch (op.kind) {
    case Op::Kind::Remove:
        removeSymmetryItem(set, i);
        break;
    case Op::Kind::Toggle:
        if (at(i).enabled || canEnable(set, i))
            at(i).enabled = !at(i).enabled;
        break;
    case Op::Kind::Duplicate: {
        const SymmetryItem copy = at(i);
        if (addSymmetryItem(set, copy)) // appended, then moved up to sit right after the original
            std::rotate(items.begin() + i + 1, items.begin() + set.count - 1, items.begin() + set.count);
        break;
    }
    case Op::Kind::Center:
        at(i).posX = kSymmetryCenter;
        at(i).posY = kSymmetryCenter;
        break;
    case Op::Kind::Up:
        if (i > 0)
            std::swap(at(i), at(i - 1));
        break;
    case Op::Kind::Down:
        if (i + 1 < set.count)
            std::swap(at(i), at(i + 1));
        break;
    default:
        break;
    }
    g_list.hover = -1;
    resetFields();
}

// --- widgets -----------------------------------------------------------------------------------

void hintWrapped(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

// A square icon button with a rounded hover plate; the icon takes `color`, the plate `plate` (alpha-scaled on hover).
bool iconButton(const char* id, UiIcon icon, float side, const ImVec4& color, const ImVec4& plate) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (hovered || held)
        dl->AddRectFilled(min, ImVec2(min.x + side, min.y + side), ImGui::GetColorU32(withAlpha(plate, held ? 0.5f : 0.3f)),
                          std::round(side * 0.28f));
    const float iconSize = std::round(side * 0.62f);
    const float off = std::round((side - iconSize) * 0.5f);
    drawUiIcon(dl, icon, ImVec2(min.x + off, min.y + off),
               iconSize, ImGui::GetColorU32(withAlpha(color, hovered ? 1.0f : 0.8f)));
    return clicked;
}

// Disabled menu entries explain themselves on hover.
bool menuEntry(Icon icon, const char* label, bool enabled, const char* whyNot = nullptr) {
    const bool clicked = iconMenuItem(icon, label, nullptr, nullptr, enabled);
    if (!enabled && whyNot && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", whyNot);
    return clicked;
}

// Whether three labeled pills ("posX: [33.5]") fit side by side in `avail`. Judged on the widest label of any row kind,
// never on the row itself, so every row of the list uses the same layout.
bool fieldsFitInline(float avail) {
    const PillMetrics m = pillMetrics();
    float labelW = 0.0f;
    for (const Str s : {Str::SymFieldPosX, Str::SymFieldPosY, Str::SymFieldAngle, Str::SymFieldCopies})
        labelW = std::max(labelW, ImGui::CalcTextSize((std::string(tr(s)) + ":").c_str()).x);
    const float pill = m.padX * 2.0f + labelW + m.gap + minInputWidth();
    return avail >= pill * 3.0f + m.gap * 2.0f;
}

// The fields of a row. A wide panel puts the pills side by side, sharing the width; a narrow one stacks them, one
// per line and as wide as the row, so each keeps its label (and unit) and the values line up in a column.
void drawFields(const Colors& c, SymmetrySet& set, int index, int canvasW, int canvasH, bool allowCommit) {
    SymmetryItem& it = set.items[static_cast<std::size_t>(index)];
    const PillMetrics m = pillMetrics();
    const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const bool stacked = !fieldsFitInline(avail);
    const bool sizes = canvasW > 0 && canvasH > 0;
    const bool percent = g_list.percent;
    const char* posUnit = percent ? "%" : "px";

    enum class Field { PosX, PosY, Angle, Copies };
    struct Entry {
        Field field;
        std::string label;
    };
    std::array<Entry, 3> entries{};
    int n = 0;
    const auto add = [&](Field f, Str label) {
        entries[static_cast<std::size_t>(n++)] = Entry{f, std::string(tr(label)) + ":"};
    };
    if (it.kind == SymmetryKind::Mirror) {
        if (sizes) {
            add(Field::PosX, Str::SymFieldPosX);
            add(Field::PosY, Str::SymFieldPosY);
        }
        add(Field::Angle, Str::SymFieldAngle);
    } else {
        if (sizes) {
            add(Field::PosX, Str::SymFieldPosX);
            add(Field::PosY, Str::SymFieldPosY);
        }
        add(Field::Copies, Str::SymFieldCopies);
    }

    const auto position = [&](int axisIndex, int& stored, int size, const Entry& e, float w) {
        const double value = percent ? axisToPercent(stored, size) : axisToPixels(stored, size);
        const char* tip = tr(percent ? Str::OptSymmetryPercentTip : Str::OptSymmetryPixelsTip);
        // Dragging 1 px of mouse moves the axis by about 1/256 of the canvas side (and a quarter percent).
        const float speed = percent ? 0.25f : std::max(0.05f, static_cast<float>(size) / 256.0f);
        const double hi = percent ? 100.0 : static_cast<double>(size);
        if (const auto moved = numberPill(c, index * kFieldsPerItem + axisIndex, e.label.c_str(), value, posUnit, speed,
                                          0.0, hi, percent ? "%.2f" : "%.1f", w, stacked, allowCommit, tip))
            stored = storedAxis(percent ? axisFromPercent(*moved, size) : axisFromPixels(*moved, size), size);
    };

    // Inline: all the fields share one line, each taking an equal part of the width. Stacked: each has a line.
    const int first = 0;
    const int last = n - 1;
    const float w = stacked ? avail
                            : std::max(1.0f, std::floor((avail - m.gap * static_cast<float>(std::max(0, n - 1))) / static_cast<float>(std::max(1, n))));
    {
        for (int k = first; k <= last; ++k) {
            if (k > first && !stacked)
                ImGui::SameLine(0.0f, m.gap);
            const Entry& e = entries[static_cast<std::size_t>(k)];
            ImGui::PushID(k);
            switch (e.field) {
            case Field::PosX:
                position(0, it.posX, canvasW, e, w);
                break;
            case Field::PosY:
                position(1, it.posY, canvasH, e, w);
                break;
            case Field::Angle: {
                // Whole degrees folded into 0..179, so dragging past 179 wraps to 0; fmod keeps a huge typed value
                // inside int range.
                if (const auto moved = numberPill(c, index * kFieldsPerItem + 2, e.label.c_str(), it.angle, "\xC2\xB0",
                                                  0.5f, 0.0, 0.0, "%.0f", w, stacked, allowCommit, tr(Str::SymAngleTip)))
                    it.angle = normalizeMirrorAngle(static_cast<int>(std::fmod(std::round(*moved), 180.0)));
                break;
            }
            case Field::Copies: {
                const PillLayout p = beginPill(c, e.label.c_str(), "", w, stacked);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, c.input);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
                ImGui::SetNextItemWidth(p.inputWidth);
                if (ImGui::BeginCombo("##copies", std::to_string(it.count).c_str())) {
                    for (const int count : kRotateCounts) {
                        SymmetrySet next = set;
                        next.items[static_cast<std::size_t>(index)].count = count;
                        const bool ok = count == it.count || symmetryCopyEstimate(next) <= kMaxSymmetryCopies;
                        ImGui::BeginDisabled(!ok);
                        if (ImGui::Selectable(std::to_string(count).c_str(), count == it.count) && allowCommit)
                            it.count = count;
                        ImGui::EndDisabled();
                        if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("%s", tr(Str::SymEnableLimit));
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip("%s", tr(Str::SymCopiesTip));
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                endPill(p, "", w);
                break;
            }
            }
            ImGui::PopID();
        }
    }
}

// The height shared by the name line, the kebab and the trash button: a little taller than a frame, so the buttons
// are easy to hit.
float rowLineHeight() {
    return std::round(ImGui::GetFrameHeight() + ImGui::GetFontSize() * 0.3f);
}

// The name of a row: clipped with an ellipsis (the tooltip has the full name), a faint plate under the pointer, and
// a double-click turns it into the rename field.
void drawNameLabel(const Colors& c, const SymmetrySet& set, int index, float width, float height) {
    const SymmetryItem& it = set.items[static_cast<std::size_t>(index)];
    const float em = ImGui::GetFontSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + height);
    ImGui::InvisibleButton("##name", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        beginRename(set, index);

    if (hovered)
        dl->AddRectFilled(min, max, ImGui::GetColorU32(withAlpha(c.pill, 0.7f)), std::round(height * 0.3f));
    const float inset = std::round(em * 0.4f);
    const std::string full = symmetryItemName(set, index);
    std::string shown = full;
    const bool clipped = fitWithEllipsis(shown, std::max(1.0f, width - inset * 2.0f));
    dl->AddText(ImVec2(min.x + inset, min.y + std::round((height - ImGui::GetTextLineHeight()) * 0.5f)),
                ImGui::GetColorU32(withAlpha(tokens().text, it.enabled ? 1.0f : 0.45f)), shown.c_str());
    if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        if (clipped)
            ImGui::SetTooltip("%s\n%s", full.c_str(), tr(Str::SymRenameTip));
        else
            ImGui::SetTooltip("%s", tr(Str::SymRenameTip));
    }
}

// The rename field in place of the name. Enter or losing the focus keeps the text, Escape drops it.
void drawRenameField(const Colors& c, SymmetrySet& set, int index, float width, float height) {
    const float em = ImGui::GetFontSize();
    g_list.editSeenFrame = ImGui::GetFrameCount();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, c.input);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::round(height * 0.3f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(std::round(em * 0.4f), std::max(0.0f, std::floor((height - em) * 0.5f))));
    if (g_list.editFocus) {
        ImGui::SetKeyboardFocusHere();
        g_list.editFocus = false;
    }
    ImGui::SetNextItemWidth(width);
    const std::string hint = autoName(set, index);
    const bool enter = ImGui::InputTextWithHint("##rename", hint.c_str(), g_list.editBuf.data(), g_list.editBuf.size(),
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool active = ImGui::IsItemActive();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // The field takes a frame to get the focus; after that, not being active means it was left.
    const bool left = !active && ImGui::GetFrameCount() > g_list.editStartFrame + 1;
    if (enter || left) {
        if (enter || !ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            commitRename(set, index);
        g_list.editIndex = -1;
    }
}

// One row: the name, the kebab menu and the trash button, then the fields.
void drawRow(const Colors& c, SymmetrySet& set, int index, int canvasW, int canvasH, bool busy, Op& op) {
    SymmetryItem& it = set.items[static_cast<std::size_t>(index)];
    const ImGuiStyle& style = ImGui::GetStyle();
    const float em = ImGui::GetFontSize();
    const bool hot = g_list.rowHot[static_cast<std::size_t>(index)];

    ImGui::PushID(index);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, std::round(em * 0.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(em * 0.8f), std::round(em * 0.55f)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, hot ? c.rowHover : c.row);
    ImGui::PushStyleColor(ImGuiCol_Border, hot ? c.borderHot : c.border);
    const bool visible = ImGui::BeginChild("##row", ImVec2(0.0f, 0.0f),
                                           ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders |
                                               ImGuiChildFlags_AlwaysUseWindowPadding,
                                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    if (visible) {
        const float side = rowLineHeight();
        const float gap = std::round(style.ItemSpacing.x * 0.5f);
        const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float nameWidth = std::max(1.0f, avail - side * 2.0f - gap * 2.0f);

        // Name (dimmed when the item is off), or its rename field.
        if (g_list.editIndex == index)
            drawRenameField(c, set, index, nameWidth, side);
        else
            drawNameLabel(c, set, index, nameWidth, side);

        // Kebab menu.
        ImGui::SameLine(0.0f, gap);
        if (iconButton("##more", UiIcon::Kebab, side, tokens().text, tokens().text))
            ImGui::OpenPopup("##menu");
        ImGui::SetItemTooltip("%s", tr(Str::SymMoreTip));
        if (ImGui::BeginPopup("##menu")) {
            if (ImGui::MenuItem(tr(Str::SymMenuRename)))
                beginRename(set, index);
            const bool canToggle = it.enabled || canEnable(set, index);
            if (menuEntry(it.enabled ? Icon::EyeClosedHideLayer : Icon::EyeOpenDisplayLayer,
                          tr(it.enabled ? Str::SymMenuDisable : Str::SymMenuEnable), canToggle, tr(Str::SymEnableLimit)))
                op = Op{Op::Kind::Toggle, index};
            const bool canCopy = canAddSymmetryItem(set, it);
            if (menuEntry(Icon::Duplicate, tr(Str::SymMenuDuplicate), canCopy, tr(Str::SymAddLimit)))
                op = Op{Op::Kind::Duplicate, index};
            if (menuEntry(Icon::Anchor, tr(Str::SymMenuCenter), !atCenter(it, canvasW, canvasH)))
                op = Op{Op::Kind::Center, index};
            ImGui::Separator();
            if (menuEntry(Icon::ItemForward, tr(Str::SymMenuUp), index > 0))
                op = Op{Op::Kind::Up, index};
            if (menuEntry(Icon::ItemBackward, tr(Str::SymMenuDown), index + 1 < set.count))
                op = Op{Op::Kind::Down, index};
            ImGui::Separator();
            if (ImGui::MenuItem(tr(Str::SymMenuUnitPx), nullptr, !g_list.percent))
                g_list.percent = false;
            if (ImGui::MenuItem(tr(Str::SymMenuUnitPercent), nullptr, g_list.percent))
                g_list.percent = true;
            ImGui::EndPopup();
        }

        // Trash.
        ImGui::SameLine(0.0f, gap);
        if (iconButton("##remove", UiIcon::Trash, side, c.danger, c.danger))
            op = Op{Op::Kind::Remove, index};
        ImGui::SetItemTooltip("%s", tr(Str::SymRemoveTip));

        // The fields; a disabled item shows them dimmed (they still work).
        ImGui::Dummy(ImVec2(0.0f, std::round(em * 0.15f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, std::round(em * 0.3f)));
        if (!it.enabled)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * 0.5f);
        drawFields(c, set, index, canvasW, canvasH, !busy);
        if (!it.enabled)
            ImGui::PopStyleVar();
        ImGui::PopStyleVar();

        const bool nowHot = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        g_list.rowHot[static_cast<std::size_t>(index)] = nowHot;
        if (nowHot) {
            g_list.hover = index;
            g_list.hoverFrame = ImGui::GetFrameCount();
        }
    }
    ImGui::EndChild();
    ImGui::PopID();
}

// "Symmetry list:" on the left and the red "All" pill on the right, both centered on a line as tall as a row's name
// line (the pill goes on a line of its own when the panel is too narrow for both).
void drawHeader(const Colors& c, const SymmetrySet& set, Op& op) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const PillMetrics m = pillMetrics();
    const float side = rowLineHeight();
    const float pillH = ImGui::GetFrameHeight();
    const char* title = tr(Str::SymTitle);
    const char* all = tr(Str::SymClearAll);
    const float avail = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float titleW = ImGui::CalcTextSize(title).x;
    const float buttonW = std::min(ImGui::CalcTextSize(all).x + m.padX * 2.0f, avail);
    const ImVec2 line = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::Dummy(ImVec2(avail, side));
    dl->PushClipRect(line, ImVec2(line.x + avail, line.y + side), true);
    dl->AddText(ImVec2(line.x, line.y + std::round((side - ImGui::GetTextLineHeight()) * 0.5f)),
                ImGui::GetColorU32(ImGuiCol_Text), title);
    dl->PopClipRect();

    const bool sameLine = avail >= titleW + style.ItemSpacing.x + buttonW;
    ImVec2 buttonPos(line.x + avail - buttonW, line.y + std::round((side - pillH) * 0.5f));
    float nextY = line.y + side + style.ItemSpacing.y;
    if (!sameLine) {
        buttonPos = ImVec2(line.x, nextY);
        nextY += pillH + style.ItemSpacing.y;
    }

    ImGui::SetCursorScreenPos(buttonPos);
    ImGui::BeginDisabled(set.count <= 0);
    const bool clicked = ImGui::InvisibleButton("##all", ImVec2(buttonW, pillH));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const float alpha = held ? 0.5f : (hovered ? 0.38f : 0.22f);
    dl->AddRectFilled(buttonPos, ImVec2(buttonPos.x + buttonW, buttonPos.y + pillH),
                      ImGui::GetColorU32(withAlpha(c.danger, alpha)), pillH * 0.5f);
    dl->PushClipRect(buttonPos, ImVec2(buttonPos.x + buttonW, buttonPos.y + pillH), true);
    dl->AddText(ImVec2(buttonPos.x + std::max(0.0f, std::round((buttonW - ImGui::CalcTextSize(all).x) * 0.5f)),
                       buttonPos.y + std::round((pillH - ImGui::GetTextLineHeight()) * 0.5f)),
                ImGui::GetColorU32(c.danger), all);
    dl->PopClipRect();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tr(Str::SymClearTip));
    if (clicked)
        op = Op{Op::Kind::Clear, -1};
    ImGui::SetCursorScreenPos(ImVec2(line.x, nextY));
}

// The empty list: a rounded, outlined block with the hint centered in it.
void drawEmptyState(const Colors& c) {
    const float em = ImGui::GetFontSize();
    const float pad = std::round(em * 0.9f);
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const char* text = tr(Str::SymEmpty);
    const float wrap = std::max(1.0f, width - pad * 2.0f);
    const ImVec2 size = ImGui::CalcTextSize(text, nullptr, false, wrap);
    const float height = std::round(size.y + pad * 2.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + height);
    ImGui::Dummy(ImVec2(width, height));
    dl->AddRectFilled(min, max, ImGui::GetColorU32(withAlpha(c.row, 0.55f)), std::round(em * 0.7f));
    dl->AddRect(min, max, ImGui::GetColorU32(c.border), std::round(em * 0.7f));
    // A single line is centered; wrapped text keeps its left edge.
    const float x = size.x < wrap ? min.x + std::round((width - size.x) * 0.5f) : min.x + pad;
    dl->AddText(nullptr, 0.0f, ImVec2(x, min.y + pad), ImGui::GetColorU32(ImGuiCol_TextDisabled), text, nullptr, wrap);
}

// The three add buttons: as many per line as fit, wrapping on a narrow panel. A button the list cannot take is
// disabled and its tooltip says why.
void drawAddButtons(SymmetrySet& set, bool busy) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float avail = ImGui::GetContentRegionAvail().x;
    float used = 0.0f;

    struct Button {
        const char* id;
        Icon icon;
        Str label;
        Str tip;
        SymmetryItem item;
    };
    const Button buttons[] = {
        {"addX", Icon::MirrorHorizontal, Str::SymAddMirrorX, Str::OptSymmetryXTip, makeMirrorX()},
        {"addY", Icon::MirrorVertical, Str::SymAddMirrorY, Str::OptSymmetryYTip, makeMirrorY()},
    };
    for (const Button& b : buttons) {
        const char* text = tr(b.label);
        const float width = std::min(iconTextButtonWidth(text), std::max(1.0f, avail));
        if (used > 0.0f) {
            if (used + style.ItemSpacing.x + width <= avail) {
                ImGui::SameLine();
                used += style.ItemSpacing.x;
            } else {
                used = 0.0f;
            }
        }
        used += width;
        const bool can = canAddSymmetryItem(set, b.item);
        ImGui::BeginDisabled(busy || !can);
        const bool clicked = iconTextButton(b.id, b.icon, text, ImVec2(width, 0.0f));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr(can ? b.tip : Str::SymAddLimit));
        if (clicked && can && !busy)
            addSymmetryItem(set, b.item);
    }
}

} // namespace

std::string symmetryItemName(const SymmetrySet& set, int index) {
    if (index < 0 || index >= set.count || index >= kMaxSymmetryItems)
        return {};
    const std::string_view custom = symmetryNameView(set.items[static_cast<std::size_t>(index)].name);
    return custom.empty() ? autoName(set, index) : std::string(custom);
}

unsigned symmetryEmphasisMask() {
    unsigned mask = 0;
    if (g_list.hover >= 0 && g_list.hover < kMaxSymmetryItems && ImGui::GetFrameCount() - g_list.hoverFrame <= 1)
        mask |= bitOf(g_list.hover);
    return mask;
}

bool toggleMirrorPreset(SymmetrySet& set, int preset) {
    const auto matches = [preset](const SymmetryItem& it) {
        return it.kind == SymmetryKind::Mirror && (it.preset == 1 ? 1 : 0) == preset;
    };
    const int count = std::clamp(set.count, 0, kMaxSymmetryItems);
    bool any = false;
    bool anyEnabled = false;
    for (int i = 0; i < count; ++i) {
        const SymmetryItem& it = set.items[static_cast<std::size_t>(i)];
        if (matches(it)) {
            any = true;
            anyEnabled = anyEnabled || it.enabled;
        }
    }
    if (!any)
        return addSymmetryItem(set, preset == 1 ? makeMirrorY() : makeMirrorX());

    SymmetrySet next = set;
    for (int i = 0; i < count; ++i) {
        SymmetryItem& it = next.items[static_cast<std::size_t>(i)];
        if (matches(it))
            it.enabled = !anyEnabled;
    }
    if (!anyEnabled && symmetryCopyEstimate(next) > kMaxSymmetryCopies)
        return false;
    set = next;
    return true;
}

void drawSymmetrySection(Toolbox& tools, int canvasWidth, int canvasHeight) {
    SymmetrySet& set = tools.settings().symmetry;
    set.count = std::clamp(set.count, 0, kMaxSymmetryItems);
    g_list.hover = -1;

    sectionHeader(tr(Str::PanelSymmetry));

    const bool busy = tools.gestureActive();
    // A rename ends when its row is gone, was not drawn last frame (panel collapsed) or a gesture began.
    if (g_list.editIndex >= set.count || busy || ImGui::GetFrameCount() - g_list.editSeenFrame > 1)
        g_list.editIndex = -1;
    const Colors c = makeColors();
    const float em = ImGui::GetFontSize();
    const float pad = std::round(em * 0.45f);
    Op op;

    ImGui::BeginDisabled(busy);
    drawAddButtons(set, busy);

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, std::round(em * 0.8f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, c.panel);
    ImGui::PushStyleColor(ImGuiCol_Border, c.border);
    const bool visible = ImGui::BeginChild("##symList", ImVec2(0.0f, 0.0f),
                                           ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders |
                                               ImGuiChildFlags_AlwaysUseWindowPadding,
                                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    if (visible) {
        drawHeader(c, set, op);
        if (set.count == 0) {
            drawEmptyState(c);
        } else {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, std::round(em * 0.5f)));
            for (int i = 0; i < set.count; ++i)
                drawRow(c, set, i, canvasWidth, canvasHeight, busy, op);
            ImGui::PopStyleVar();
        }
    }
    ImGui::EndChild();

    // List changes are applied after the rows are drawn, so a row never loses its item mid-frame.
    if (!busy)
        applyOp(set, op);
    ImGui::EndDisabled();

    if (set.count > 0)
        hintWrapped(tr(Str::OptSymmetryHint));
    for (int i = 0; i < set.count; ++i) {
        if (set.items[static_cast<std::size_t>(i)].kind == SymmetryKind::Rotate) {
            hintWrapped(tr(Str::OptRotateHint));
            break;
        }
    }
}

} // namespace trace
