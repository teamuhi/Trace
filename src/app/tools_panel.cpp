#include "app/tools_panel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "app/font_library.hpp"
#include "app/icon_widgets.hpp"
#include "app/keybinds.hpp"
#include "app/symmetry_panel.hpp"
#include "app/theme.hpp"
#include "app/tool_icons.hpp"
#include "core/i18n.hpp"
#include "core/tools_layout.hpp"
#include "tools/tool_text.hpp"
#include "tools/toolbox.hpp"

namespace trace {

namespace {

constexpr const char* kEditorPopupId = "##colorEditor";
constexpr const char* kBrushPopupId = "##brushStyle";

constexpr Rgba8 kPalette[] = {
    {0, 0, 0, 255},       {64, 64, 64, 255},    {127, 127, 127, 255}, {195, 195, 195, 255}, {255, 255, 255, 255},
    {120, 0, 0, 255},     {136, 0, 21, 255},    {237, 28, 36, 255},   {255, 127, 39, 255},  {255, 201, 14, 255},
    {255, 242, 0, 255},   {181, 230, 29, 255},  {34, 177, 76, 255},   {0, 128, 128, 255},   {0, 162, 232, 255},
    {153, 217, 234, 255}, {63, 72, 204, 255},   {0, 0, 128, 255},     {163, 73, 164, 255},  {255, 0, 255, 255},
    {255, 174, 201, 255}, {185, 122, 87, 255},  {239, 228, 176, 255}, {128, 128, 0, 255},   {112, 146, 190, 255},
    {200, 191, 231, 255}, {135, 206, 235, 255}, {0, 0, 0, 0},
};
constexpr int kBarPaletteColumns = 14; // two rows of the 28 colors in a bar

constexpr ImGuiColorEditFlags kSwatchFlags = ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoBorder |
                                             ImGuiColorEditFlags_NoTooltip;

ImVec4 toImVec4(Rgba8 c) {
    return ImVec4(static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
                  static_cast<float>(c.b) / 255.0f, static_cast<float>(c.a) / 255.0f);
}

std::uint8_t quantize(float v) {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255));
}

Rgba8 fromFloats(const float rgba[4]) {
    return Rgba8{quantize(rgba[0]), quantize(rgba[1]), quantize(rgba[2]), quantize(rgba[3])};
}

void toFloats(Rgba8 c, float out[4]) {
    const ImVec4 v = toImVec4(c);
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
    out[3] = v.w;
}

ImVec4 mixColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
}

ImU32 withAlpha(const ImVec4& c, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

// Swatches carry no border of their own, so near-white colors would vanish against a light window. The
// border color comes from the active ImGui theme.
void drawSwatchBorder() {
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                        ImGui::GetColorU32(ImGuiCol_Border));
}

// --- look ----------------------------------------------------------------------------------------------

// Sizes that follow the font (so the UI scale), in whole pixels so icons stay sharp.
struct Metrics {
    float em;       // the font size
    float icon;     // side of a tool icon
    float cell;     // side of a tool button
    float gap;      // between buttons
    float pad;      // inside a card
    float rounding; // corners of cards and buttons
};

Metrics metrics() {
    Metrics m;
    m.em = ImGui::GetFontSize();
    m.icon = std::round(m.em * 1.8f);
    m.cell = m.icon + std::round(m.em * 0.7f);
    m.gap = std::round(m.em * 0.25f);
    m.pad = std::round(m.em * 0.5f);
    m.rounding = std::round(m.em * 0.4f);
    return m;
}

// The card's fill and outline are the window color nudged toward the text color, so they read as a raised area on
// both the light and the dark theme.
ImVec4 cardFill() {
    const ThemeTokens& t = tokens();
    return mixColor(t.windowBg, t.text, isDark() ? 0.06f : 0.035f);
}

ImVec4 cardOutline() {
    const ThemeTokens& t = tokens();
    return mixColor(t.windowBg, t.text, isDark() ? 0.16f : 0.14f);
}

// A small title with a rule running to the right edge.
void cardHeader(const char* text) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float right = p.x + ImGui::GetContentRegionAvail().x;
    ImGui::TextDisabled("%s", text);
    const float textW = ImGui::CalcTextSize(text).x;
    const float x0 = p.x + textW + std::round(ImGui::GetFontSize() * 0.5f);
    if (x0 < right) {
        const float y = std::round(p.y + ImGui::GetTextLineHeight() * 0.5f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, y), ImVec2(right, y), ImGui::GetColorU32(ImGuiCol_Separator));
    }
    ImGui::Dummy(ImVec2(0.0f, std::round(ImGui::GetStyle().ItemSpacing.y * 0.25f)));
}

// A framed group of controls. A `width` of 0 spans the available width (a column); a bar gives every card its width.
// Always pair with endCard, whatever this returns.
bool beginCard(const char* id, float width, bool framed = true) {
    const Metrics m = metrics();
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, m.rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, framed ? 1.0f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, framed ? ImVec2(m.pad, m.pad) : ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, framed ? cardFill() : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, cardOutline());
    const bool visible = ImGui::BeginChild(id, ImVec2(width, 0.0f),
                                           ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders |
                                               ImGuiChildFlags_AlwaysUseWindowPadding,
                                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return visible;
}

void endCard() {
    ImGui::EndChild();
}

// Lays the cards out: stacked in a column, or side by side in a bar, wrapping to a new row when the next one does not
// fit in what is left of the width.
class CardFlow {
public:
    explicit CardFlow(bool bar) : bar_(bar), right_(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x) {}

    bool bar() const noexcept { return bar_; }

    bool begin(const char* id, float width, bool framed = true) {
        if (bar_ && started_) {
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            if (previousRight_ + spacing + width <= right_ + 1.0f)
                ImGui::SameLine(0.0f, spacing);
        }
        started_ = true;
        return beginCard(id, bar_ ? width : 0.0f, framed);
    }

    void end() {
        endCard();
        previousRight_ = ImGui::GetItemRectMax().x;
    }

private:
    bool bar_;
    float right_;
    bool started_ = false;
    float previousRight_ = 0.0f;
};

// --- small widgets -------------------------------------------------------------------------------------

// A row of mutually exclusive choices sharing the width, the chosen one on the accent. Returns true when it changed.
bool segmented(const char* id, std::span<const Str> labels, int& value) {
    const ThemeTokens& t = tokens();
    const int count = static_cast<int>(labels.size());
    const float gap = std::round(ImGui::GetStyle().ItemSpacing.x * 0.5f);
    const float width = std::floor((ImGui::GetContentRegionAvail().x - gap * static_cast<float>(count - 1)) /
                                   static_cast<float>(std::max(count, 1)));
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        if (i > 0)
            ImGui::SameLine(0.0f, gap);
        const bool on = value == i;
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent);
            ImGui::PushStyleColor(ImGuiCol_Text, contrastText(t.accent));
        }
        ImGui::PushID(i);
        if (ImGui::Button(tr(labels[static_cast<std::size_t>(i)]), ImVec2(width, 0.0f)) && !on) {
            value = i;
            changed = true;
        }
        ImGui::PopID();
        if (on)
            ImGui::PopStyleColor(4);
    }
    ImGui::PopID();
    return changed;
}

// The label in a column of its own, so the controls of consecutive rows line up. Call right before the control; the
// control is placed to the right of the label. In a narrow panel (less than kMinControlEm of room left for the control
// beside the column) the label sits on a line of its own and the control takes the full width below it, so sliders
// and button groups are never squeezed or cropped.
void rowLabel(Str label) {
    constexpr float kMinControlEm = 9.0f;
    const float unit = ImGui::GetFontSize();
    const float column = std::round(unit * 5.4f);
    const char* text = tr(label);
    const float x0 = ImGui::GetCursorPosX();
    const float labelWidth = std::max(column, ImGui::CalcTextSize(text).x + ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::GetContentRegionAvail().x - labelWidth < unit * kMinControlEm) {
        ImGui::TextUnformatted(text); // stacked: the control starts on the next line
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(x0 + labelWidth);
}

bool labeledSlider(Str label, const char* id, int& value, int minValue, int maxValue, const char* format = "%d px") {
    rowLabel(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    return ImGui::SliderInt(id, &value, minValue, maxValue, format, ImGuiSliderFlags_AlwaysClamp);
}

// A plain number field: drag it left or right to change the value (Shift is faster, Alt slower), or double-click or
// Ctrl+click to type one. Always kept inside the range, also when typed. For values where a slider is too coarse,
// such as a font size.
bool labeledNumber(Str label, const char* id, int& value, int minValue, int maxValue, const char* format = "%d px") {
    rowLabel(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    return ImGui::DragInt(id, &value, 0.5f, minValue, maxValue, format, ImGuiSliderFlags_AlwaysClamp);
}

// A numeric text box: type the number, no slider and no +/- buttons, followed by its unit. The value is kept inside
// minValue..maxValue while typing (an empty or too small entry counts as the minimum); the box itself shows what was
// typed until it is left, then the clamped number.
bool labeledNumberBox(Str label, const char* id, int& value, int minValue, int maxValue, const char* unit = "px") {
    rowLabel(label);
    const float unitWidth = ImGui::CalcTextSize(unit).x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 3.0f, ImGui::GetContentRegionAvail().x - unitWidth));
    int shown = value;
    bool changed = false;
    if (ImGui::InputInt(id, &shown, 0, 0, ImGuiInputTextFlags_AutoSelectAll)) {
        const int next = std::clamp(shown, minValue, maxValue);
        changed = next != value;
        value = next;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", unit);
    return changed;
}

bool sameName(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool containsIgnoreCase(const std::string& text, const char* needle) {
    const std::string n(needle);
    if (n.empty())
        return true;
    const auto it = std::search(text.begin(), text.end(), n.begin(), n.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
    return it != text.end();
}

// The Text tool's font: a family list with a search box (the built-in fonts first, then the ones installed on the
// system, found in the background), and a style list when the family has more than one.
void drawFontPicker(ToolSettings& s) {
    FontLibrary& library = fontLibrary();
    library.startScan();
    const bool scanned = library.ready();
    const float unit = ImGui::GetFontSize();

    rowLabel(Str::OptTextFont);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SetNextWindowSizeConstraints(ImVec2(unit * 16.0f, 0.0f), ImVec2(FLT_MAX, unit * 22.0f));
    if (ImGui::BeginCombo("##textFont", s.textFontFamily.c_str())) {
        static char filter[64] = "";
        if (ImGui::IsWindowAppearing()) {
            filter[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##fontSearch", tr(Str::OptFontSearch), filter, sizeof(filter));
        ImGui::Separator();

        const std::vector<FontFamily>& families = library.families();
        std::vector<int> shown;
        shown.reserve(families.size());
        for (int i = 0; i < static_cast<int>(families.size()); ++i) {
            if (containsIgnoreCase(families[static_cast<std::size_t>(i)].name, filter))
                shown.push_back(i);
        }

        if (ImGui::BeginChild("##fontList", ImVec2(0.0f, unit * 14.0f))) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(shown.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const FontFamily& family = families[static_cast<std::size_t>(shown[static_cast<std::size_t>(row)])];
                    const bool selected = sameName(family.name, s.textFontFamily);
                    if (ImGui::Selectable(family.name.c_str(), selected) && !selected) {
                        s.textFontFamily = family.name;
                        s.textFontStyle = FontLibrary::pickStyle(family, s.textFontStyle).style;
                    }
                    if (selected && ImGui::IsWindowAppearing())
                        ImGui::SetScrollHereY();
                    // A rule between the built-in fonts and the installed ones.
                    if (family.builtIn && row + 1 < static_cast<int>(shown.size()) &&
                        !families[static_cast<std::size_t>(shown[static_cast<std::size_t>(row) + 1])].builtIn)
                        ImGui::Separator();
                }
            }
            if (!scanned)
                ImGui::TextDisabled("%s", tr(Str::OptFontLoading));
            else if (shown.empty())
                ImGui::TextDisabled("%s", tr(Str::OptFontNoMatch));
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }

    // The style, only when the family offers a choice.
    const FontFamily* family = library.find(s.textFontFamily);
    if (family != nullptr && family->styles.size() > 1) {
        rowLabel(Str::OptFontStyle);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const FontStyleEntry& current = FontLibrary::pickStyle(*family, s.textFontStyle);
        if (ImGui::BeginCombo("##textFontStyle", current.style.c_str())) {
            for (const FontStyleEntry& entry : family->styles) {
                const bool selected = &entry == &current;
                if (ImGui::Selectable(entry.style.c_str(), selected) && !selected)
                    s.textFontStyle = entry.style;
            }
            ImGui::EndCombo();
        }
    }
}

// Muted text that wraps at the panel's right edge, for hints (translations can be longer).
void hintWrapped(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

// A tool's name with its shortcut, as the tooltip of its button reads it.
std::string nameWithShortcut(const Keymap& keymap, ToolId id) {
    const std::string shortcut = displayShortcut(keymap, toolAction(id));
    const char* name = tr(toolString(id));
    return shortcut.empty() ? std::string(name) : trf(Str::OptToolTip, {name, shortcut});
}

// --- tool buttons ----------------------------------------------------------------------------------------

bool hasBrushStyle(ToolId id) {
    return id == ToolId::Brush || id == ToolId::Eraser;
}

// One icon-only button per tool, in a grid that fills the width (or a single row in a bar). The state shows on a
// soft tile: the chosen tool on the accent, the hovered one a little lighter. The name and shortcut are the tooltip
// (the brush and the eraser add where to find their style), and a right click on those two asks for the style menu.
void drawToolButtons(Toolbox& tools, const Keymap& keymap, std::span<const ToolId> toolset, bool bar,
                     bool& openBrushMenu) {
    const ThemeTokens& t = tokens();
    const Metrics m = metrics();
    const int count = static_cast<int>(toolset.size());
    const int columns = bar ? std::max(count, 1)
                            : toolslayout::gridColumns(ImGui::GetContentRegionAvail().x, m.cell, m.gap, count);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(m.gap, m.gap));
    for (int i = 0; i < count; ++i) {
        const ToolId id = toolset[static_cast<std::size_t>(i)];
        const bool selected = tools.current() == id;
        if (i % columns != 0)
            ImGui::SameLine();

        ImGui::PushID(i);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + m.cell, min.y + m.cell);
        if (ImGui::InvisibleButton("##tool", ImVec2(m.cell, m.cell)))
            tools.select(id);
        const bool hovered = ImGui::IsItemHovered();
        if (hasBrushStyle(id) && ImGui::IsItemClicked(ImGuiMouseButton_Right) && tools.select(id))
            openBrushMenu = true;

        if (selected) {
            dl->AddRectFilled(min, max, withAlpha(t.accent, 0.22f), m.rounding);
            dl->AddRect(min, max, withAlpha(t.accent, 0.9f), m.rounding);
        } else if (hovered) {
            dl->AddRectFilled(min, max, withAlpha(t.text, 0.09f), m.rounding);
        }
        if (ImGui::IsItemFocused()) // keyboard navigation: a thin outline
            dl->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_NavHighlight), m.rounding, 0, 1.0f);

        ImVec4 color = selected ? t.accent : t.text;
        if (!selected && !hovered)
            color.w *= 0.82f;
        drawToolIcon(dl, id,
                     ImVec2(std::round(min.x) + std::round((m.cell - m.icon) * 0.5f),
                            std::round(min.y) + std::round((m.cell - m.icon) * 0.5f)),
                     m.icon, ImGui::ColorConvertFloat4ToU32(color));

        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted(nameWithShortcut(keymap, id).c_str());
            if (hasBrushStyle(id))
                ImGui::TextDisabled("%s", tr(Str::OptBrushRightClick));
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
}

// --- brush style -------------------------------------------------------------------------------------------

Str brushStyleName(BrushStyle style) {
    switch (style) {
    case BrushStyle::Square:
        return Str::OptSquare;
    case BrushStyle::Soft:
        return Str::OptSoft;
    case BrushStyle::Round:
        break;
    }
    return Str::OptRound;
}

// The tip of a brush as a picture: a disc, a square, or a disc whose edge fades by `hardness` (0..1). The soft one is
// stacked translucent discs, each layer adding what the one under it lacks of the wanted coverage.
void drawBrushTip(ImDrawList* dl, ImVec2 center, float radius, BrushStyle style, float hardness, const ImVec4& color) {
    switch (style) {
    case BrushStyle::Round:
        dl->AddCircleFilled(center, radius, withAlpha(color, 1.0f), 32);
        break;
    case BrushStyle::Square: {
        const float half = std::round(radius * 0.9f);
        dl->AddRectFilled(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half),
                          withAlpha(color, 1.0f));
        break;
    }
    case BrushStyle::Soft: {
        constexpr int kRings = 14;
        const float ramp = 0.08f + (1.0f - std::clamp(hardness, 0.0f, 1.0f)) * 0.92f;
        float done = 0.0f;
        for (int k = 0; k < kRings; ++k) {
            const float outer = 1.0f - static_cast<float>(k) / static_cast<float>(kRings);
            const float mid = outer - 0.5f / static_cast<float>(kRings);
            const float wanted = std::clamp((1.0f - mid) / ramp, 0.0f, 1.0f);
            if (wanted <= done || done >= 1.0f)
                continue;
            const float layer = (wanted - done) / (1.0f - done);
            done = wanted;
            dl->AddCircleFilled(center, radius * outer, withAlpha(color, layer), 32);
        }
        break;
    }
    }
}

// The brush and eraser size: a typed number box in Paint, a slider in Pixel Art (where sizes stop at 16 px).
void brushSizeControl(ToolSettings& s, bool pixel) {
    if (pixel)
        labeledSlider(Str::OptSize, "##brushSize", s.brushSize, ToolSettings::kMinSize, ToolSettings::kMaxPixelSize);
    else
        labeledNumberBox(Str::OptSize, "##brushSize", s.brushSize, ToolSettings::kMinSize, ToolSettings::kMaxSize);
}

// The brush style menu: the tip (round, square, soft where the mode has antialiasing), its size, how soft the edge is
// and antialiasing. It is what a right click on the Brush or Eraser button opens, and what the style button of the
// options opens.
void drawBrushPopup(Toolbox& tools, bool pixel) {
    ToolSettings& s = tools.settings();
    const Metrics m = metrics();
    const ThemeTokens& t = tokens();
    const float width = std::round(m.em * 17.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
    if (!ImGui::BeginPopup(kBrushPopupId))
        return;

    cardHeader(tr(Str::OptBrushStyle));

    constexpr std::array<BrushStyle, 3> kStyles = {BrushStyle::Round, BrushStyle::Square, BrushStyle::Soft};
    const int count = pixel ? 2 : 3; // Pixel Art is whole pixels: no soft edge
    const BrushStyle current = brushStyleOf(s);
    const float gap = m.gap * 2.0f;
    const float tileW = std::floor((ImGui::GetContentRegionAvail().x - gap * static_cast<float>(count - 1)) /
                                   static_cast<float>(count));
    const float tileH = std::round(m.em * 3.9f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < count; ++i) {
        const BrushStyle style = kStyles[static_cast<std::size_t>(i)];
        const bool softBlocked = style == BrushStyle::Soft && !s.antialias;
        if (i > 0)
            ImGui::SameLine(0.0f, gap);
        ImGui::PushID(i);
        ImGui::BeginDisabled(softBlocked);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + tileW, min.y + tileH);
        if (ImGui::InvisibleButton("##style", ImVec2(tileW, tileH)))
            setBrushStyle(s, style);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::EndDisabled();
        if (softBlocked)
            ImGui::SetItemTooltip("%s", tr(Str::OptSoftNeedsAA));

        const bool on = current == style;
        const float fade = softBlocked ? ImGui::GetStyle().DisabledAlpha : 1.0f;
        if (on) {
            dl->AddRectFilled(min, max, withAlpha(t.accent, 0.20f), m.rounding);
            dl->AddRect(min, max, withAlpha(t.accent, 0.9f), m.rounding);
        } else {
            if (hovered)
                dl->AddRectFilled(min, max, withAlpha(t.text, 0.08f), m.rounding);
            dl->AddRect(min, max, withAlpha(cardOutline(), 1.0f), m.rounding);
        }
        const ImVec4 ink = on ? t.accent : t.text;
        const float hardness = style == BrushStyle::Soft && on ? static_cast<float>(s.brushHardness) / 100.0f : 0.5f;
        drawBrushTip(dl, ImVec2(std::round((min.x + max.x) * 0.5f), std::round(min.y + tileH * 0.4f)),
                     std::round(m.em * 0.95f), style, hardness, ImVec4(ink.x, ink.y, ink.z, ink.w * fade));
        const char* name = tr(brushStyleName(style));
        dl->AddText(ImVec2(std::round((min.x + max.x - ImGui::CalcTextSize(name).x) * 0.5f),
                           std::round(max.y - ImGui::GetTextLineHeight() - m.em * 0.3f)),
                    withAlpha(ink, fade), name);
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0.0f, m.gap));
    brushSizeControl(s, pixel);
    if (!pixel) {
        ImGui::BeginDisabled(brushStyleOf(s) != BrushStyle::Soft || !s.antialias);
        labeledSlider(Str::OptHardness, "##brushHardness", s.brushHardness, ToolSettings::kMinHardness, 99, "%d%%");
        ImGui::EndDisabled();
        ImGui::Checkbox(tr(Str::WordAntialias), &s.antialias);
    }
    ImGui::EndPopup();
}

// The row of the options that opens the brush style menu: the current tip beside its name.
void brushStyleButton(const ToolSettings& s, bool& openBrushMenu) {
    if (iconTextButton("##brushStyleButton", Icon::SlidersHorizontal, tr(brushStyleName(brushStyleOf(s))),
                       ImVec2(-FLT_MIN, 0.0f)))
        openBrushMenu = true;
    ImGui::SetItemTooltip("%s", tr(Str::OptBrushStyle));
}

// --- options -----------------------------------------------------------------------------------------------

// Options of the current tool. `pixel` is the Pixel Art variant: sizes stop at 16 px, there is no
// antialiasing (everything is whole pixels), the eraser always clears, and the pencil and fill get the
// pixel art options. The brush and eraser keep a size slider here; the rest of their tip lives in the brush style menu.
void drawToolOptions(Toolbox& tools, bool pixel, bool& openBrushMenu) {
    ToolSettings& s = tools.settings();
    const int maxStroke = pixel ? ToolSettings::kMaxPixelSize : ToolSettings::kMaxStrokeWidth;

    switch (tools.current()) {
    case ToolId::Pencil:
        if (pixel) {
            ImGui::Checkbox(tr(Str::OptPixelPerfect), &s.pixelPerfect);
            ImGui::SetItemTooltip("%s", tr(Str::OptPixelPerfectTip));
        }
        ImGui::TextDisabled("%s", tr(Str::OptPencil));
        break;
    case ToolId::Brush:
        brushSizeControl(s, pixel);
        brushStyleButton(s, openBrushMenu);
        break;
    case ToolId::Eraser:
        brushSizeControl(s, pixel);
        brushStyleButton(s, openBrushMenu);
        if (pixel)
            hintWrapped(tr(Str::OptEraserAlways));
        else
            ImGui::Checkbox(tr(Str::OptEraseToTransparent), &s.eraseToTransparent);
        break;
    case ToolId::Line:
        labeledSlider(Str::OptWidth, "##strokeWidth", s.strokeWidth, ToolSettings::kMinSize, maxStroke);
        if (!pixel)
            ImGui::Checkbox(tr(Str::WordAntialias), &s.antialias);
        ImGui::TextDisabled("%s", tr(Str::OptShift45));
        break;
    case ToolId::Rectangle:
    case ToolId::Ellipse: {
        labeledSlider(Str::WordOutline, "##strokeWidth", s.strokeWidth, ToolSettings::kMinSize, maxStroke);
        int mode = static_cast<int>(s.shapeMode);
        ImGui::RadioButton(tr(Str::WordOutline), &mode, static_cast<int>(ShapeMode::Outline));
        ImGui::RadioButton(tr(Str::WordFilled), &mode, static_cast<int>(ShapeMode::Filled));
        ImGui::RadioButton(tr(Str::WordOutlineAndFill), &mode, static_cast<int>(ShapeMode::Both));
        s.shapeMode = static_cast<ShapeMode>(mode);
        ImGui::TextDisabled("%s", tr(tools.current() == ToolId::Rectangle ? Str::OptShiftSquare : Str::OptShiftCircle));
        break;
    }
    case ToolId::Fill:
        labeledSlider(Str::OptTolerance, "##tolerance", s.fillTolerance, 0, 255, "%d");
        if (pixel) {
            int global = s.fillGlobal ? 1 : 0;
            constexpr std::array<Str, 2> kModes = {Str::OptFillContiguous, Str::OptFillGlobal};
            segmented("##fillMode", kModes, global);
            ImGui::SetItemTooltip("%s", tr(Str::OptFillGlobalTip));
            s.fillGlobal = global != 0;
        }
        break;
    case ToolId::Select:
        ImGui::TextDisabled("%s", tr(Str::OptSelectDrag));
        ImGui::TextDisabled("%s", tr(Str::OptSelectMove));
        ImGui::TextDisabled("%s", tr(Str::OptShiftSquare));
        break;
    case ToolId::Picker:
        ImGui::TextDisabled("%s", tr(Str::OptPickerPrimary));
        ImGui::TextDisabled("%s", tr(Str::OptPickerSecondary));
        break;
    case ToolId::Text: {
        labeledNumber(Str::OptSize, "##textSize", s.textSize, ToolSettings::kMinTextSize, ToolSettings::kMaxTextSize);
        drawFontPicker(s);

        // Alignment of the lines of a several-line text, as three icon buttons sharing the row.
        rowLabel(Str::OptTextAlign);
        constexpr std::array<Icon, 3> kAlignIcons = {Icon::TextJustifyLeft, Icon::TextJustifyCenter,
                                                     Icon::TextJustifyRight};
        constexpr std::array<Str, 3> kAlignTips = {Str::OptTextAlignLeft, Str::OptTextAlignCenter,
                                                   Str::OptTextAlignRight};
        const float gap = std::round(ImGui::GetStyle().ItemSpacing.x * 0.5f);
        const float width = std::floor((ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f);
        for (int i = 0; i < 3; ++i) {
            if (i > 0)
                ImGui::SameLine(0.0f, gap);
            const TextAlign align = static_cast<TextAlign>(i);
            if (iconTextButton(("##align" + std::to_string(i)).c_str(), kAlignIcons[static_cast<std::size_t>(i)], nullptr,
                               ImVec2(width, 0.0f), s.textAlign == align))
                s.textAlign = align;
            ImGui::SetItemTooltip("%s", tr(kAlignTips[static_cast<std::size_t>(i)]));
        }

        ImGui::Checkbox(tr(Str::WordAntialias), &s.antialias);
        hintWrapped(tr(Str::OptTextClick));
        hintWrapped(tr(Str::OptTextApply));
        hintWrapped(tr(Str::OptTextCancel));
        break;
    }
    }
}

// --- colors and palette ------------------------------------------------------------------------------------

// Picker state. The float copy is kept while the popup is open: round-tripping through 8-bit every frame
// would make the hue jump whenever the color passes through gray.
struct EditorState {
    bool editSecondary = false;
    float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};
EditorState g_editor;

void openEditor(bool secondary, const ToolSettings& s) {
    g_editor.editSecondary = secondary;
    toFloats(secondary ? s.secondary : s.primary, g_editor.rgba);
    ImGui::OpenPopup(kEditorPopupId);
}

// Side of the primary and secondary swatches, and the width of the whole cluster with the Swap button.
float swatchSize() {
    return std::round(ImGui::GetFrameHeight() * 1.6f);
}

float colorsWidth() {
    const float size = swatchSize();
    return std::round(size * 1.55f) + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(tr(Str::ColorSwap)).x +
           ImGui::GetStyle().FramePadding.x * 2.0f;
}

void drawColorSwatches(Toolbox& tools, const Keymap& keymap) {
    ToolSettings& s = tools.settings();
    const float size = swatchSize();
    const float offset = std::round(size * 0.55f);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 origin = ImGui::GetCursorPos();

    // The secondary swatch sits behind and below the primary, as in classic paint programs.
    ImGui::SetCursorPos(ImVec2(origin.x + offset, origin.y + offset));
    if (ImGui::ColorButton("##secondary", toImVec4(s.secondary), kSwatchFlags, ImVec2(size, size)))
        openEditor(true, s);
    drawSwatchBorder();
    ImGui::SetItemTooltip("%s", tr(Str::ColorSecondaryTip));

    ImGui::SetCursorPos(origin);
    if (ImGui::ColorButton("##primary", toImVec4(s.primary), kSwatchFlags, ImVec2(size, size)))
        openEditor(false, s);
    drawSwatchBorder();
    ImGui::SetItemTooltip("%s", tr(Str::ColorPrimaryTip));

    ImGui::SetCursorPos(ImVec2(origin.x + offset + size + gap, origin.y));
    if (ImGui::Button(tr(Str::ColorSwap)))
        tools.swapColors();
    const std::string swapShortcut = displayShortcut(keymap, Action::SwapColors);
    if (swapShortcut.empty())
        ImGui::SetItemTooltip("%s", tr(Str::ColorSwapTip));
    else
        ImGui::SetItemTooltip("%s", trf(Str::ColorSwapTipKey, {swapShortcut}).c_str());

    ImGui::SetCursorPos(ImVec2(origin.x, origin.y + offset + size));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    if (ImGui::BeginPopup(kEditorPopupId)) {
        Rgba8& target = g_editor.editSecondary ? s.secondary : s.primary;
        if (fromFloats(g_editor.rgba) != target) // changed elsewhere (picker tool, swap): follow it
            toFloats(target, g_editor.rgba);

        ImGui::TextUnformatted(tr(g_editor.editSecondary ? Str::WordSecondaryColor : Str::WordPrimaryColor));
        constexpr ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
        if (ImGui::ColorPicker4("##picker", g_editor.rgba, flags))
            target = fromFloats(g_editor.rgba);
        ImGui::EndPopup();
    }
}

float paletteSwatch() {
    return ImGui::GetFrameHeight();
}

float paletteSpacing() {
    return std::round(ImGui::GetStyle().ItemSpacing.x * 0.5f);
}

// Width of a palette with `perRow` swatches in a row.
float paletteWidth(int perRow) {
    return static_cast<float>(perRow) * (paletteSwatch() + paletteSpacing()) - paletteSpacing();
}

void drawPalette(Toolbox& tools, int fixedPerRow) {
    ToolSettings& s = tools.settings();
    const float size = paletteSwatch();
    const float spacing = paletteSpacing();
    const int perRow = fixedPerRow > 0
                           ? fixedPerRow
                           : std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (size + spacing)));

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    int index = 0;
    for (const Rgba8& color : kPalette) {
        if (index % perRow != 0)
            ImGui::SameLine();

        ImGui::PushID(index);
        if (ImGui::ColorButton("##swatch", toImVec4(color), kSwatchFlags, ImVec2(size, size)))
            s.primary = color;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            s.secondary = color;
        drawSwatchBorder();
        if (color.a == 255) {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", color.r, color.g, color.b);
            ImGui::SetItemTooltip("%s", trf(Str::PaletteTip, {hex}).c_str());
        } else {
            ImGui::SetItemTooltip("%s", tr(Str::PaletteTransparentTip));
        }
        ImGui::PopID();
        ++index;
    }
    ImGui::PopStyleVar();
}

// --- docking ----------------------------------------------------------------------------------------------

// One tracker per Tools window (Paint and Pixel Art each have theirs), so the size a panel had survives being moved.
std::map<ImGuiID, toolslayout::DockSizeTracker> g_dockTrackers;

// Watches the node this window is docked in and, when the user moved the panel somewhere else, gives it back the size
// it had in that kind of place instead of the half of the window ImGui hands a newly docked panel. Call at the end of
// the window's content, which is when the height a bar needs is known.
void keepDockSize(float em) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    toolslayout::DockSizeTracker& tracker = g_dockTrackers[window->ID];
    ImGuiDockNode* node = window->DockNode;

    toolslayout::DockObservation obs;
    ImGuiAxis axis = ImGuiAxis_X;
    float barFit = 0.0f;
    if (node != nullptr && node->ParentNode != nullptr && node->Windows.Size == 1) {
        axis = static_cast<ImGuiAxis>(node->ParentNode->SplitAxis);
        obs.docked = true;
        obs.node = node->ID;
        obs.kind = toolslayout::classifyDock(axis == ImGuiAxis_Y, node->Size.x, node->Size.y);
        obs.size = node->Size[axis];
        if (obs.kind == toolslayout::DockKind::Bar) {
            // The node is the window plus the tab bar; the content needs its own height and the padding.
            const ImGuiStyle& style = ImGui::GetStyle();
            const float needed = ImGui::GetCursorPosY() - style.ItemSpacing.y + style.WindowPadding.y;
            barFit = needed + (node->Size.y - window->Size.y);
        }
    }

    const toolslayout::DockCommand command = tracker.update(obs, std::round(em * 15.0f), barFit);
    if (command.apply && node != nullptr) {
        node->Size[axis] = node->SizeRef[axis] = command.size;
        node->WantLockSizeOnce = true;
    }
}

} // namespace

void sectionHeader(const char* text) {
    cardHeader(text);
}

void drawToolsPanel(Toolbox& tools, const Keymap& keymap) {
    drawToolsPanel(tools, keymap, kPaintTools, ToolOptions::Paint);
}

void drawToolsPanel(Toolbox& tools, const Keymap& keymap, std::span<const ToolId> toolset, ToolOptions options,
                    PixelCanvasSize canvas) {
    const Metrics m = metrics();
    const bool pixel = options == ToolOptions::Pixel;
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const bool bar = toolslayout::barShape(windowSize.x, windowSize.y, m.em);
    bool openBrushMenu = false;

    CardFlow flow(bar);
    // A bar shows the tools in one row; one too narrow for that wraps them like a column does.
    const float toolsWidth = static_cast<float>(toolset.size()) * (m.cell + m.gap) - m.gap + m.pad * 2.0f + 2.0f;
    const float availWidth = ImGui::GetContentRegionAvail().x;
    const bool singleRow = bar && toolsWidth <= availWidth;
    if (flow.begin("##toolsCard", bar && !singleRow ? availWidth : toolsWidth)) {
        cardHeader(tr(Str::PanelTools));
        drawToolButtons(tools, keymap, toolset, singleRow, openBrushMenu);
    }
    flow.end();

    if (flow.begin("##optionsCard", std::round(m.em * 18.5f))) {
        cardHeader(tr(Str::PanelOptions));
        drawToolOptions(tools, pixel, openBrushMenu);
    }
    flow.end();

    if (pixel) {
        // The symmetry list draws its own heading and framed list.
        if (flow.begin("##symmetryCard", std::round(m.em * 21.0f), false))
            drawSymmetrySection(tools, canvas.width, canvas.height);
        flow.end();
    }

    if (flow.begin("##colorsCard", colorsWidth() + m.pad * 2.0f + 2.0f)) {
        cardHeader(tr(Str::PanelColors));
        drawColorSwatches(tools, keymap);
    }
    flow.end();

    // Pixel Art has its own Palette window (the project's palette); only Paint keeps the fixed one here.
    if (!pixel) {
        if (flow.begin("##paletteCard", paletteWidth(kBarPaletteColumns) + m.pad * 2.0f + 2.0f)) {
            cardHeader(tr(Str::PanelPalette));
            drawPalette(tools, bar ? kBarPaletteColumns : 0);
        }
        flow.end();
    }

    if (openBrushMenu)
        ImGui::OpenPopup(kBrushPopupId);
    drawBrushPopup(tools, pixel);

    keepDockSize(m.em);
}

} // namespace trace
