#include "app/tools_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "app/keybinds.hpp"
#include "app/theme.hpp"
#include "app/tool_icons.hpp"
#include "core/i18n.hpp"
#include "tools/tool_text.hpp"
#include "tools/toolbox.hpp"

namespace trace {

namespace {

constexpr const char* kEditorPopupId = "##colorEditor";

constexpr Rgba8 kPalette[] = {
    {0, 0, 0, 255},       {64, 64, 64, 255},    {127, 127, 127, 255}, {195, 195, 195, 255}, {255, 255, 255, 255},
    {120, 0, 0, 255},     {136, 0, 21, 255},    {237, 28, 36, 255},   {255, 127, 39, 255},  {255, 201, 14, 255},
    {255, 242, 0, 255},   {181, 230, 29, 255},  {34, 177, 76, 255},   {0, 128, 128, 255},   {0, 162, 232, 255},
    {153, 217, 234, 255}, {63, 72, 204, 255},   {0, 0, 128, 255},     {163, 73, 164, 255},  {255, 0, 255, 255},
    {255, 174, 201, 255}, {185, 122, 87, 255},  {239, 228, 176, 255}, {128, 128, 0, 255},   {112, 146, 190, 255},
    {200, 191, 231, 255}, {135, 206, 235, 255}, {0, 0, 0, 0},
};

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

// Swatches carry no border of their own, so near-white colors would vanish against a light window. The
// border color comes from the active ImGui theme.
void drawSwatchBorder() {
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                        ImGui::GetColorU32(ImGuiCol_Border));
}

void sectionLabel(Str text) {
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr(text));
}

// Icon buttons in a fixed two-column grid, like a classic toolbar. They have no boxes: the icon itself shows
// the state (selected = accent color, hovered = full text color, otherwise a little dimmer). The size follows
// the font and is whole pixels so the icons stay sharp; a panel too narrow for two columns falls back to one.
void drawToolButtons(Toolbox& tools, const Keymap& keymap) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ThemeTokens& t = tokens();
    // Roomy cells: two columns share the panel width, and they are tall enough for a large icon.
    const float height = std::round(ImGui::GetFontSize() * 3.0f);
    const float gap = std::round(style.ItemSpacing.x * 0.5f);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int columns = avail >= height * 2.0f + gap ? 2 : 1;
    const float width = std::floor((avail - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
    const float iconSize = std::round(height * 0.68f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    for (int i = 0; i < kToolCount; ++i) {
        const ToolId id = static_cast<ToolId>(i);
        const bool selected = tools.current() == id;
        if (i % columns != 0)
            ImGui::SameLine();

        ImGui::PushID(i);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + width, min.y + height);
        if (ImGui::InvisibleButton("##tool", ImVec2(width, height)))
            tools.select(id);
        const bool hovered = ImGui::IsItemHovered();

        ImVec4 color = t.text;
        if (selected)
            color = t.accent;
        else if (!hovered)
            color.w *= 0.72f;
        if (ImGui::IsItemFocused()) // keyboard navigation: a thin outline, never a filled box
            dl->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_NavHighlight), style.FrameRounding, 0, 1.0f);
        drawToolIcon(dl, id,
                     ImVec2(std::round(min.x) + std::round((width - iconSize) * 0.5f),
                            std::round(min.y) + std::round((height - iconSize) * 0.5f)),
                     iconSize,
                     ImGui::ColorConvertFloat4ToU32(color));

        const std::string shortcut = displayShortcut(keymap, toolAction(id));
        const char* name = tr(toolString(id));
        if (shortcut.empty())
            ImGui::SetItemTooltip("%s", name);
        else
            ImGui::SetItemTooltip("%s", trf(Str::OptToolTip, {name, shortcut}).c_str());
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
}

void sizeSlider(Str label, int& value, int maxValue) {
    ImGui::TextUnformatted(tr(label));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt((std::string("##") + stringKey(label)).c_str(), &value, ToolSettings::kMinSize, maxValue, "%d px",
                     ImGuiSliderFlags_AlwaysClamp);
}

void drawToolOptions(Toolbox& tools) {
    ToolSettings& s = tools.settings();

    switch (tools.current()) {
    case ToolId::Pencil:
        ImGui::TextDisabled("%s", tr(Str::OptPencil));
        break;
    case ToolId::Brush:
        sizeSlider(Str::OptSize, s.brushSize, ToolSettings::kMaxSize);
        ImGui::Checkbox(tr(Str::WordAntialias), &s.antialias);
        break;
    case ToolId::Eraser:
        sizeSlider(Str::OptSize, s.brushSize, ToolSettings::kMaxSize);
        ImGui::Checkbox(tr(Str::WordAntialias), &s.antialias);
        ImGui::Checkbox(tr(Str::OptEraseToTransparent), &s.eraseToTransparent);
        break;
    case ToolId::Line:
        sizeSlider(Str::OptWidth, s.strokeWidth, ToolSettings::kMaxStrokeWidth);
        ImGui::Checkbox(tr(Str::WordAntialias), &s.antialias);
        ImGui::TextDisabled("%s", tr(Str::OptShift45));
        break;
    case ToolId::Rectangle:
    case ToolId::Ellipse: {
        sizeSlider(Str::WordOutline, s.strokeWidth, ToolSettings::kMaxStrokeWidth);
        int mode = static_cast<int>(s.shapeMode);
        ImGui::RadioButton(tr(Str::WordOutline), &mode, static_cast<int>(ShapeMode::Outline));
        ImGui::RadioButton(tr(Str::WordFilled), &mode, static_cast<int>(ShapeMode::Filled));
        ImGui::RadioButton(tr(Str::WordOutlineAndFill), &mode, static_cast<int>(ShapeMode::Both));
        s.shapeMode = static_cast<ShapeMode>(mode);
        ImGui::TextDisabled("%s", tr(tools.current() == ToolId::Rectangle ? Str::OptShiftSquare : Str::OptShiftCircle));
        break;
    }
    case ToolId::Fill:
        ImGui::TextUnformatted(tr(Str::OptTolerance));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##tolerance", &s.fillTolerance, 0, 255, "%d", ImGuiSliderFlags_AlwaysClamp);
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
    }
}

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

void drawColorSwatches(Toolbox& tools, const Keymap& keymap) {
    ToolSettings& s = tools.settings();
    const float size = ImGui::GetFrameHeight() * 1.6f;
    const float offset = size * 0.55f;
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

void drawPalette(Toolbox& tools) {
    ToolSettings& s = tools.settings();
    const float size = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x * 0.5f;
    const int perRow = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (size + spacing)));

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

} // namespace

void drawToolsPanel(Toolbox& tools, const Keymap& keymap) {
    drawToolButtons(tools, keymap);

    sectionLabel(Str::PanelOptions);
    drawToolOptions(tools);

    sectionLabel(Str::PanelColors);
    drawColorSwatches(tools, keymap);

    sectionLabel(Str::PanelPalette);
    drawPalette(tools);
}

} // namespace trace
