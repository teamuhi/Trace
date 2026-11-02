#include "app/settings_window.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "io/path_utf8.hpp"

#include "app/app_icon.hpp"
#include "app/keybinds.hpp"
#include "app/modal_ui.hpp"
#include "app/languages.hpp"
#include "app/native_dialogs.hpp"
#include "app/theme_io.hpp"
#include "app/pattern_draw.hpp"
#include "app/panel_ui.hpp"
#include "app/theme.hpp"
#include "app/tool_icons.hpp"
#include "core/action_text.hpp"
#include "core/color_text.hpp"
#include "core/i18n.hpp"
#include "tools/tool_text.hpp"
#include "tools/toolbox.hpp"


namespace trace {

namespace {

constexpr float kDefaultWidth = 980.0f;
constexpr float kDefaultHeight = 660.0f;
constexpr float kMinWidth = 640.0f;
constexpr float kMinHeight = 440.0f;
constexpr float kViewportFill = 0.9f; // never open larger than this share of the viewport

constexpr std::array<Str, kSettingsPageCount> kPageNames = {
    Str::SettingsPageGeneral,  Str::SettingsPageTheme,    Str::SettingsPageView,  Str::SettingsPageTools,
    Str::SettingsPagePixelArt, Str::SettingsPageFlowchart, Str::SettingsPageScripts, Str::SettingsPageKeybinds,
    Str::SettingsPageAbout};
constexpr std::array<const char*, 8> kGroups = {"File", "Edit", "Image", "View", "Tools", "Modes", "Pixel Art", "Flowchart"};
constexpr std::array<int, 5> kScalePresets = {75, 100, 125, 150, 200};

// Visible titles are translated; the part after ### keeps the window's and popup's identity (and so its saved
// position) the same in every language. The window grew from 760 x 480 to 980 x 660; its size is only applied
// the first time, so the id was bumped (Settings -> Settings2) to reset a size an older imgui.ini remembers.
std::string settingsTitle() {
    return std::string(tr(Str::SettingsTitle)) + "###Settings2";
}
std::string restoreTitle() {
    return std::string(tr(Str::SettingsRestoreTitle)) + "###RestoreDefaults";
}

ImVec2 scaled(float x, float y) {
    return ImVec2(x * uiScale(), y * uiScale());
}

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

// The color the open picker started from, for its "Original" swatch. Only one popup is open at a time.
ImVec4 g_pickerOriginal;

// A color swatch that opens a picker popup (with hex and RGBA fields). Keeps its float state between frames,
// so dragging through gray does not lose the hue to 8-bit rounding, and follows `target` when something else
// changes it. `original` is what the picker's "Original" swatch restores (the built-in color); null means
// the color the picker was opened with. Returns true while the picker edits `target`. Call inside a PushID.
bool colorSwatch(float rgba[4], Rgba8& target, const ImVec4* original) {
    if (fromFloats(rgba) != target)
        toFloats(target, rgba);

    const float height = ImGui::GetFrameHeight();
    if (ImGui::ColorButton("##swatch", ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]),
                           ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip,
                           ImVec2(std::round(height * 1.6f), height))) {
        g_pickerOriginal = toImVec4(target);
        ImGui::OpenPopup("##picker");
    }
    ImGui::SetItemTooltip("%s", tr(Str::ThemeSwatchTip));

    bool edited = false;
    if (ImGui::BeginPopup("##picker")) {
        if (fromFloats(rgba) != target) // reset or imported while open: follow it
            toFloats(target, rgba);
        constexpr ImGuiColorEditFlags flags = ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_AlphaBar |
                                              ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_DisplayRGB |
                                              ImGuiColorEditFlags_AlphaPreviewHalf;
        const ImVec4& ref = original ? *original : g_pickerOriginal;
        if (ImGui::ColorPicker4("##colorpicker", rgba, flags, &ref.x)) {
            target = fromFloats(rgba);
            edited = true;
        }
        ImGui::EndPopup();
    }
    return edited;
}

// Headings and hints --------------------------------------------------------------------------------------

// A heading with air above it, so groups on a page do not run together: a short accent bar, the title and a rule
// that runs to the right edge.
void section(Str text) {
    const float em = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0.0f, std::round(em * 0.5f)));
    const ThemeTokens& t = tokens();
    const panel::Colors c = panel::colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float right = start.x + ImGui::GetContentRegionAvail().x;
    const float barWidth = std::max(2.0f, std::round(em * 0.22f));
    const float lineHeight = ImGui::GetTextLineHeight();
    dl->AddRectFilled(ImVec2(start.x, start.y + 1.0f), ImVec2(start.x + barWidth, start.y + lineHeight - 1.0f),
                      ImGui::ColorConvertFloat4ToU32(t.accent), barWidth * 0.5f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + barWidth + std::round(em * 0.55f));
    ImGui::TextUnformatted(tr(text));
    const float from = ImGui::GetItemRectMax().x + std::round(em * 0.7f);
    if (from < right - em * 0.5f) {
        const float midY = std::round(start.y + lineHeight * 0.5f);
        dl->AddLine(ImVec2(from, midY), ImVec2(right, midY), ImGui::ColorConvertFloat4ToU32(c.rule), 1.0f);
    }
    ImGui::Dummy(ImVec2(0.0f, std::round(em * 0.1f)));
}

// Muted help text that wraps at the right edge, so long lines and translations never run off the page. With
// `inlineAfterPrevious` it continues the previous widget's line when there is room (at least about eight ems),
// else it starts a new one.
void hint(const char* text, bool inlineAfterPrevious = false) {
    if (inlineAfterPrevious) {
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        const float room = right - ImGui::GetItemRectMax().x - ImGui::GetStyle().ItemSpacing.x;
        if (room >= std::min(ImGui::CalcTextSize(text).x, ImGui::GetFontSize() * 8.0f))
            ImGui::SameLine();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

// Width of a combo that shows every one of `labels` without clipping: the widest text, the frame padding and
// the arrow button; never narrower than `minWidth`.
float fitWidth(std::span<const char* const> labels, float minWidth = 0.0f) {
    float widest = 0.0f;
    for (const char* label : labels)
        widest = std::max(widest, ImGui::CalcTextSize(label).x);
    return std::max(minWidth, widest + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight());
}

float fitWidth(std::initializer_list<const char*> labels, float minWidth = 0.0f) {
    return fitWidth(std::span<const char* const>(labels.begin(), labels.size()), minWidth);
}

// Width of a button that fits `label`.
float buttonWidth(const char* label) {
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

// Space left on the current line after the previous item, up to the right edge of the content area.
float roomRight() {
    return ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x - ImGui::GetItemRectMax().x;
}

// Form rows -----------------------------------------------------------------------------------------------
// Every page lays its settings out the same way: a label column on the left, the control in a second column,
// and any help text wrapped under the control. A checkbox carries its own label and its hint sits under the text.

// Width of a page's label column: the widest label plus an em and a half, never more than 45% of the page.
float labelColumn(std::initializer_list<const char*> labels) {
    const float em = ImGui::GetFontSize();
    float widest = 0.0f;
    for (const char* label : labels)
        widest = std::max(widest, ImGui::CalcTextSize(label).x);
    return std::min(widest + em * 1.5f, std::max(em * 8.0f, ImGui::GetContentRegionAvail().x * 0.45f));
}

// The row's label, wrapped inside the label column, then the cursor moves to the control column.
void fieldLabel(const char* text, float column) {
    const float x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::PushTextWrapPos(x + column - ImGui::GetFontSize() * 0.5f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::SameLine(x + column);
}

// Help text under the control column.
void fieldHint(const char* text, float column) {
    ImGui::Indent(column);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
    ImGui::Unindent(column);
}

// Help text under a checkbox's label.
void checkHint(const char* text) {
    const float indent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    fieldHint(text, indent);
}

// A checkbox with an optional hint under it.
bool checkField(Str label, bool& value, const char* hintText = nullptr) {
    const bool changed = ImGui::Checkbox(tr(label), &value);
    if (hintText)
        checkHint(hintText);
    return changed;
}

// A labelled combo: the label in the label column, then the combo `width` wide. Close with ImGui::EndCombo().
bool beginComboField(const char* label, float column, const char* preview, float width) {
    fieldLabel(label, column);
    ImGui::SetNextItemWidth(width);
    return ImGui::BeginCombo((std::string("##") + label).c_str(), preview);
}

// The number being typed into a numeric field. The text is only read back into the setting once the field loses
// focus (Enter, Tab or a click elsewhere), so the value can pass through out-of-range numbers while typing (a
// minimum of 16 would otherwise make "5" jump to 16 before the "12" is finished). Only one field is edited at a time.
struct NumberDraft {
    ImGuiID id = 0;
    int* target = nullptr;
    int lo = 0;
    int hi = 0;
    int value = 0;
};
NumberDraft g_draft;

// Writes a finished draft into its setting, clamped to the field's range. True when the setting changed. Also
// called at the top of every frame, so a field that stops being drawn (another page chosen, the window closed)
// still gets its text applied.
bool commitDraft() {
    if (g_draft.id == 0 || ImGui::GetActiveID() == g_draft.id)
        return false;
    const int value = std::clamp(g_draft.value, g_draft.lo, g_draft.hi);
    const bool changed = *g_draft.target != value;
    *g_draft.target = value;
    g_draft = NumberDraft{};
    return changed;
}

// A labelled numeric text box: no slider and no +/- buttons, only the number, followed by its range and unit
// ("1-16384 px"). `value` is clamped to lo..hi when the box is left.
bool numberField(const char* label, float column, int& value, int lo, int hi, const char* unit = nullptr) {
    fieldLabel(label, column);
    ImGui::PushID(label);
    const ImGuiID id = ImGui::GetID("##number");
    int shown = g_draft.id == id ? g_draft.value : value;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    if (ImGui::InputInt("##number", &shown, 0, 0, ImGuiInputTextFlags_AutoSelectAll))
        g_draft = NumberDraft{id, &value, lo, hi, shown};
    bool changed = false;
    if (g_draft.id == id)
        changed = commitDraft(); // no-op while the box still has focus
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%d-%d%s%s", lo, hi, unit ? " " : "", unit ? unit : "");
    ImGui::PopID();
    return changed;
}

// The label of token `index`, in the order of tokenInfos().
Str tokenString(std::size_t index) {
    static constexpr Str kLabels[kTokenCount] = {
        Str::TokenAccent,        Str::TokenButton,      Str::TokenText,        Str::TokenWindowBg,
        Str::TokenStatusBarBg,   Str::TokenCanvasBackdrop, Str::TokenPattern,  Str::TokenCheckerA,
        Str::TokenCheckerB,      Str::TokenCanvasBorder, Str::TokenPixelGrid,  Str::TokenRulerBg,
        Str::TokenRulerText,     Str::TokenGuide,       Str::TokenSelection,   Str::TokenWarning,
        Str::TokenSymmetryAxis,  Str::TokenTileDim,     Str::TokenOnionPrev,    Str::TokenOnionNext,
        Str::TokenFlowBoard,     Str::TokenFlowGrid,    Str::TokenFlowHandle,   Str::TokenFlowInkAuto,
        Str::TokenFlowPaperAuto};
    return kLabels[index < static_cast<std::size_t>(kTokenCount) ? index : 0];
}

Rgba8 fromVec4(const ImVec4& v) {
    const float rgba[4] = {v.x, v.y, v.z, v.w};
    return fromFloats(rgba);
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Percent-encodes everything except unreserved characters and the path separators.
std::string fileUrl(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string url = !path.empty() && path.front() == '/' ? "file://" : "file:///";
    for (const char c : path) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') {
            url += c;
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", u);
            url += buf;
        }
    }
    return url;
}

// A muted label and a value in a second column that starts `labelWidth` after the left edge.
void labeledText(float labelWidth, const char* label, const char* fmt, ...) IM_FMTARGS(3);
void labeledText(float labelWidth, const char* label, const char* fmt, ...) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(ImGui::GetCursorPosX() + labelWidth);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

} // namespace

void SettingsWindow::open() {
    open_ = true;
    editInit_ = false;
    focusRequested_ = true;
}

void SettingsWindow::startCapture(SlotRef target) {
    endCapture();
    capture_ = target;
    ImGuiIO& io = ImGui::GetIO();
    // Arrow keys, Tab and Enter would otherwise move focus while the user is trying to bind them.
    navWasEnabled_ = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    setCapturing(true);
}

void SettingsWindow::endCapture() {
    if (capture_ && navWasEnabled_ && ImGui::GetCurrentContext())
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    capture_.reset();
    conflict_.reset();
    setCapturing(false);
}

bool SettingsWindow::draw(Settings& settings, Toolbox& toolbox, const std::string& settingsPath, NativeDialogs& dialogs,
                          LanguageManager& languages) {
    dialogs_ = &dialogs;
    languages_ = &languages;
    // A number typed into a box that lost focus last frame (another page picked, window closed) is applied first.
    const bool flushed = commitDraft();
    if (!open_) {
        endCapture(); // closed by the title bar button or File > Settings
        wasOpen_ = false;
        return flushed;
    }
    if (!wasOpen_)
        languages.refresh(); // packs may have been added to the folder since the last time
    wasOpen_ = true;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 size(std::min(kDefaultWidth * uiScale(), viewport->Size.x * kViewportFill),
                      std::min(kDefaultHeight * uiScale(), viewport->Size.y * kViewportFill));
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(scaled(kMinWidth, kMinHeight), ImVec2(FLT_MAX, FLT_MAX));
    if (focusRequested_) {
        ImGui::SetNextWindowFocus();
        focusRequested_ = false;
    }

    bool changed = flushed;
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin(settingsTitle().c_str(), &open_, flags)) {
        // Losing focus (alt-tab, clicking the canvas) abandons a pending key capture.
        if ((capture_ || conflict_) && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            endCapture();

        // Roomier than the rest of the app: everything here is derived from the (already scaled) style, so the
        // interface scale still applies. Read the style before pushing: pushes change it in place.
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 itemSpacing = style.ItemSpacing;
        const ImVec2 framePadding = style.FramePadding;
        const ImVec2 innerSpacing = style.ItemInnerSpacing;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing.x * 1.25f, itemSpacing.y * 1.7f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(framePadding.x * 1.25f, framePadding.y * 1.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(innerSpacing.x * 1.5f, innerSpacing.y));

        const float footer = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f;
        settings.lastPage = std::clamp(settings.lastPage, 0, kSettingsPageCount - 1);

        changed |= drawCategories(settings, footer);

        ImGui::SameLine();
        const float em = ImGui::GetFontSize();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(em * 1.1f), std::round(em * 0.9f)));
        const bool pageVisible = ImGui::BeginChild("##page", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders);
        ImGui::PopStyleVar(); // only the page itself; popups opened from it keep the normal padding
        if (pageVisible) {
            if (settings.lastPage < kSettingsPageCount - 1)
                drawPageHeader(settings.lastPage); // About has its own title block
            switch (settings.lastPage) {
            case 0:
                changed |= drawGeneral(settings);
                break;
            case 1:
                changed |= drawTheme(settings);
                break;
            case 2:
                changed |= drawView(settings);
                break;
            case 3:
                changed |= drawTools(settings, toolbox);
                break;
            case 4:
                changed |= drawPixelArt(settings);
                break;
            case 5:
                changed |= drawFlowchart(settings);
                break;
            case 6:
                drawScripts();
                break;
            case 7:
                changed |= drawKeybinds(settings);
                break;
            default:
                drawAbout(settingsPath);
                break;
            }
        }
        ImGui::EndChild();

        // Restore defaults on the left, Close on the right; both fit their text.
        const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        if (ImGui::Button(tr(Str::SettingsRestoreDefaults)))
            ImGui::OpenPopup(restoreTitle().c_str());
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightEdge - buttonWidth(tr(Str::WordClose))));
        if (ImGui::Button(tr(Str::WordClose)))
            open_ = false;

        if (modal::begin(restoreTitle().c_str(), 22.0f)) {
            modal::paragraph(tr(Str::SettingsRestoreQuestion));
            // Resetting everything is not undoable, so Enter and Escape both keep the settings.
            const int pressed = modal::footer({{tr(Str::WordCancel), modal::ButtonKind::Secondary, true, true, true},
                                               {tr(Str::SettingsRestore), modal::ButtonKind::Danger}});
            if (pressed >= 0) {
                if (pressed == 1) {
                    const int page = settings.lastPage;
                    settings = Settings::defaults();
                    settings.lastPage = page;
                    endCapture();
                    changed = true;
                }
                ImGui::CloseCurrentPopup();
            }
            modal::end();
        }
        ImGui::PopStyleVar(3);
    }
    ImGui::End();

    if (!open_)
        endCapture();
    return changed;
}

// The category list on the left: tall rows with an icon and a label. The selected row gets an accent bar and
// an accent icon; the others are a little dimmer until hovered. The column is as wide as the longest label
// needs (so translations fit), within limits.
bool SettingsWindow::drawCategories(Settings& settings, float footer) {
    const float em = ImGui::GetFontSize();
    const float rowHeight = std::round(em * 2.9f);
    const float iconSize = std::round(em * 1.7f);
    const float pad = std::round(em * 0.9f);
    const float gap = std::round(em * 0.8f);
    const float rounding = std::round(em * 0.35f);
    const float bar = std::max(2.0f, std::round(3.0f * uiScale()));

    float labelWidth = 0.0f;
    for (const Str name : kPageNames)
        labelWidth = std::max(labelWidth, ImGui::CalcTextSize(tr(name)).x);
    const float width = std::clamp(pad + iconSize + gap + labelWidth + pad + ImGui::GetStyle().WindowPadding.x * 2.0f,
                                   em * 11.0f, em * 18.0f);

    bool changed = false;
    if (ImGui::BeginChild("##categories", ImVec2(width, -footer), ImGuiChildFlags_Borders)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ThemeTokens& t = tokens();
        // The row's look is drawn below (a rounded tile), so the Selectable paints nothing of its own.
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        for (int i = 0; i < kSettingsPageCount; ++i) {
            const bool selected = settings.lastPage == i;
            ImGui::PushID(i);
            if (ImGui::Selectable("##page", selected, 0, ImVec2(0.0f, rowHeight)) && !selected) {
                settings.lastPage = i;
                endCapture();
                changed = true;
            }
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::PopID();

            ImVec4 color = t.text;
            if (!hovered && !selected)
                color.w *= 0.75f;
            const ImVec4 iconColor = selected ? t.accent : color;

            if (selected) {
                ImVec4 tile = t.accent;
                tile.w *= 0.2f;
                dl->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(tile), rounding);
                const float inset = std::round(rowHeight * 0.22f);
                dl->AddRectFilled(ImVec2(min.x, min.y + inset), ImVec2(min.x + bar, max.y - inset),
                                  ImGui::ColorConvertFloat4ToU32(t.accent), bar * 0.5f);
            } else if (hovered) {
                ImVec4 tile = t.text;
                tile.w *= 0.08f;
                dl->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(tile), rounding);
            }
            drawSettingsIcon(dl, i,
                             ImVec2(std::round(min.x + pad), std::round(min.y + (rowHeight - iconSize) * 0.5f)),
                             iconSize, ImGui::ColorConvertFloat4ToU32(iconColor));

            const float textX = std::round(min.x + pad + iconSize + gap);
            dl->PushClipRect(ImVec2(textX, min.y), ImVec2(max.x - pad * 0.5f, max.y), true);
            dl->AddText(ImVec2(textX, std::round(min.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f)),
                        ImGui::ColorConvertFloat4ToU32(color), tr(kPageNames[static_cast<std::size_t>(i)]));
            dl->PopClipRect();
        }
        ImGui::PopStyleColor(3);
    }
    ImGui::EndChild();
    return changed;
}

// The title of the page at its top: its icon on the accent, the name in larger type and a rule below.
void SettingsWindow::drawPageHeader(int page) {
    const float em = ImGui::GetFontSize();
    const float iconSize = std::round(em * 1.9f);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float right = start.x + ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawSettingsIcon(dl, page, start, iconSize, ImGui::ColorConvertFloat4ToU32(tokens().accent));

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.4f);
    const float lineHeight = ImGui::GetTextLineHeight();
    ImGui::SetCursorScreenPos(ImVec2(start.x + iconSize + std::round(em * 0.7f),
                                     std::round(start.y + (iconSize - lineHeight) * 0.5f)));
    ImGui::TextUnformatted(tr(kPageNames[static_cast<std::size_t>(page)]));
    ImGui::PopFont();

    const float ruleY = std::round(start.y + iconSize + em * 0.5f);
    dl->AddLine(ImVec2(start.x, ruleY), ImVec2(right, ruleY),
                ImGui::ColorConvertFloat4ToU32(panel::colors().rule), 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(start.x, ruleY + 1.0f));
    ImGui::Dummy(ImVec2(0.0f, std::round(em * 0.2f)));
}

bool SettingsWindow::drawGeneral(Settings& s) {
    bool changed = false;
    const float em = ImGui::GetFontSize();
    const float column = labelColumn({tr(Str::GeneralInterfaceScale), tr(Str::GeneralInterfaceFont), tr(Str::GeneralFontSize),
                                      tr(Str::GeneralLanguageLabel), tr(Str::WordWidthPx), tr(Str::WordHeightPx),
                                      tr(Str::GeneralMemoryLimit)});

    section(Str::ThemeGroupInterface);
    {
        char preview[16];
        if (s.uiScalePercent == 0)
            std::snprintf(preview, sizeof(preview), "%s", tr(Str::WordAuto));
        else
            std::snprintf(preview, sizeof(preview), "%d%%", s.uiScalePercent);
        if (beginComboField(tr(Str::GeneralInterfaceScale), column, preview, fitWidth({tr(Str::WordAuto), "200%"}, em * 10.0f))) {
            if (ImGui::Selectable(tr(Str::WordAuto), s.uiScalePercent == 0)) {
                s.uiScalePercent = 0;
                changed = true;
            }
            for (const int percent : kScalePresets) {
                char label[16];
                std::snprintf(label, sizeof(label), "%d%%", percent);
                if (ImGui::Selectable(label, s.uiScalePercent == percent)) {
                    s.uiScalePercent = percent;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        fieldHint(tr(Str::GeneralScaleHint), column);
    }

    section(Str::GeneralFont);
    {
        const std::string customLabel =
            s.customFontPath.empty()
                ? std::string(tr(Str::GeneralFontCustom))
                : trf(Str::GeneralFontCustomNamed, {utf8FromPath(pathFromUtf8(s.customFontPath).filename())});
        const char* classicLabel = tr(Str::GeneralFontClassic);
        const auto label = [&](int i) -> const char* {
            switch (static_cast<FontChoice>(i)) {
            case FontChoice::Classic:
                return classicLabel;
            case FontChoice::Inter:
                return "Inter"; // font names are not translated
            case FontChoice::Mono:
                return "JetBrains Mono";
            case FontChoice::Custom:
                return customLabel.c_str();
            }
            return "";
        };

        const float fontComboWidth = fitWidth({classicLabel, "Inter", "JetBrains Mono", customLabel.c_str()}, em * 14.0f);
        if (beginComboField(tr(Str::GeneralInterfaceFont), column, label(static_cast<int>(s.font)),
                            std::min(fontComboWidth, std::max(em * 14.0f, ImGui::GetContentRegionAvail().x - column - em * 8.0f)))) {
            for (int i = 0; i < kFontChoiceCount; ++i) {
                const bool isCustom = i == static_cast<int>(FontChoice::Custom);
                if (isCustom && s.customFontPath.empty())
                    continue; // nothing to pick until a file has been loaded
                if (ImGui::Selectable(label(i), static_cast<int>(s.font) == i)) {
                    s.font = static_cast<FontChoice>(i);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(dialogs_ == nullptr || dialogs_->busy());
        if (ImGui::Button(tr(Str::GeneralLoadFont)) && dialogs_)
            dialogs_->open(NativeDialogs::Tag::FontFile);
        ImGui::EndDisabled();

        changed |= numberField(tr(Str::GeneralFontSize), column, s.fontSize, kMinFontSize, kMaxFontSize, "px");
        fieldHint(tr(Str::GeneralFontSample), column);
        fieldHint(tr(Str::GeneralFontHint), column);
    }

    section(Str::GeneralLanguage);
    if (languages_) {
        const auto& packs = languages_->available();
        std::string current = tr(Str::GeneralLanguageEnglish);
        if (!s.language.empty()) {
            current = s.language;
            for (const auto& pack : packs) {
                if (pack.info.code == s.language)
                    current = pack.info.name;
            }
        }
        std::vector<std::string> labels;
        labels.reserve(packs.size());
        for (const auto& pack : packs)
            labels.push_back(pack.info.name + " (" + pack.info.code + ")");
        std::vector<const char*> widths = {tr(Str::GeneralLanguageEnglish), current.c_str()};
        for (const std::string& label : labels)
            widths.push_back(label.c_str());
        if (beginComboField(tr(Str::GeneralLanguageLabel), column, current.c_str(), fitWidth(widths, em * 14.0f))) {
            if (ImGui::Selectable(tr(Str::GeneralLanguageEnglish), s.language.empty())) {
                s.language.clear();
                changed = true;
            }
            for (std::size_t i = 0; i < packs.size(); ++i) {
                if (ImGui::Selectable(labels[i].c_str(), s.language == packs[i].info.code)) {
                    s.language = packs[i].info.code;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Indent(column);
        ImGui::BeginDisabled(dialogs_ == nullptr || dialogs_->busy());
        if (ImGui::Button(tr(Str::GeneralImportLanguage)) && dialogs_)
            dialogs_->open(NativeDialogs::Tag::LanguageImport);
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::GeneralExportTemplate)) && dialogs_)
            dialogs_->save(NativeDialogs::Tag::LanguageTemplate, "trace-language-template.ini");
        ImGui::EndDisabled();
        ImGui::Unindent(column);

        if (const LanguagePack* active = languages_->active()) {
            if (!active->info.author.empty())
                fieldHint(trf(Str::GeneralLanguageAuthor, {active->info.author}).c_str(), column);
            fieldHint(trf(Str::GeneralLanguageCoverage, {std::to_string(active->translated), std::to_string(kStrCount)}).c_str(),
                      column);
        }
        fieldHint(tr(Str::GeneralLanguageHint), column);
    }

    section(Str::GeneralNewCanvas);
    changed |= numberField(tr(Str::WordWidthPx), column, s.newWidth, kMinCanvasSize, kMaxCanvasSize);
    changed |= numberField(tr(Str::WordHeightPx), column, s.newHeight, kMinCanvasSize, kMaxCanvasSize);
    fieldHint(trf(Str::GeneralNewCanvasHint, {std::to_string(kMinCanvasSize), std::to_string(kMaxCanvasSize)}).c_str(), column);

    section(Str::GeneralUndoHistory);
    changed |= numberField(tr(Str::GeneralMemoryLimit), column, s.historyLimitMb, kMinHistoryLimitMb, kMaxHistoryLimitMb);
    fieldHint(tr(Str::GeneralHistoryHint), column);
    return changed;
}

namespace {

struct AccentPreset {
    Str name;
    Rgba8 dark;  // lighter, for dark windows
    Rgba8 light; // deeper, for light windows
};

constexpr AccentPreset kAccentPresets[] = {
    {Str::AccentBlue, {66, 133, 244, 255}, {25, 103, 210, 255}},
    {Str::AccentTeal, {38, 166, 154, 255}, {0, 121, 107, 255}},
    {Str::AccentGreen, {76, 175, 80, 255}, {46, 125, 50, 255}},
    {Str::AccentOrange, {255, 152, 0, 255}, {216, 108, 0, 255}},
    {Str::AccentRed, {239, 83, 80, 255}, {198, 40, 40, 255}},
    {Str::AccentPink, {236, 64, 122, 255}, {194, 24, 91, 255}},
    {Str::AccentPurple, {171, 71, 188, 255}, {123, 31, 162, 255}},
    {Str::AccentSlate, {144, 164, 174, 255}, {84, 110, 122, 255}},
};

} // namespace

namespace {

// The Theme page lists the tokens in these groups; every token index (the order of tokenInfos()) is in
// exactly one of them.
struct TokenGroup {
    Str title;
    std::span<const int> tokens;
};

constexpr int kInterfaceTokens[] = {0, 1, 2, 3, 4, 15}; // accent, buttons, text, window, bars, warning
constexpr int kCanvasTokens[] = {5, 6, 7, 8, 9, 10};     // backdrop, pattern, checker A and B, border, pixel grid
constexpr int kOverlayTokens[] = {11, 12, 13, 14, 16, 17, 18, 19}; // ruler background and text, guides, selection, mirror axes, tile fade, onion skin

constexpr int kFlowTokens[] = {20, 21, 22, 23, 24}; // board, grid, handles, automatic ink and paper

constexpr std::array<TokenGroup, 4> kTokenGroups = {{{Str::ThemeGroupInterface, kInterfaceTokens},
                                                     {Str::ThemeGroupCanvas, kCanvasTokens},
                                                     {Str::ThemeGroupOverlays, kOverlayTokens},
                                                     {Str::ThemeGroupFlowchart, kFlowTokens}}};

constexpr bool groupsCoverEveryToken() {
    unsigned seen = 0;
    int count = 0;
    for (const TokenGroup& group : kTokenGroups) {
        for (const int index : group.tokens) {
            seen |= 1u << index;
            ++count;
        }
    }
    return count == kTokenCount && seen == (1u << kTokenCount) - 1u;
}
static_assert(groupsCoverEveryToken(), "every theme token must be in exactly one group");

// The live strip on the Theme page: a mock window drawn with the palette being edited. Sized from the text
// metrics so translated labels fit their pills; everything is clipped to the strip.
void drawThemePreview(const ThemeTokens& preview) {
    const float unit = ImGui::GetFontSize();
    const float lineHeight = ImGui::GetTextLineHeight();
    const char* buttonLabel = tr(Str::ThemePreviewButton);
    const char* selectedLabel = tr(Str::ThemePreviewSelected);
    const char* warningLabel = tr(Str::ThemePreviewWarning);

    const float pillHeight = lineHeight + unit * 0.5f;
    const float pillPad = unit * 0.5f;
    const float buttonPill = ImGui::CalcTextSize(buttonLabel).x + pillPad * 2.0f;
    const float selectedPill = ImGui::CalcTextSize(selectedLabel).x + pillPad * 2.0f;
    const float left = unit * 0.6f;
    const float leftNeeded = left + buttonPill + unit * 0.4f + selectedPill + unit * 0.6f;

    const float height = unit * 0.5f + pillHeight + unit * 0.4f + lineHeight + unit * 0.5f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
    ImGui::Dummy(size);
    const ImVec2 max(min.x + size.x, min.y + size.y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto col = [](const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); };
    dl->PushClipRect(min, max, true);
    dl->AddRectFilled(min, max, col(preview.windowBg), 4.0f);
    dl->AddRect(min, max, col(preview.canvasBorder), 4.0f);

    // canvas, with a guide and a selection on it; on a narrow strip it gives way to the buttons
    const ImVec2 cMin(std::max(min.x + size.x * 0.52f, min.x + leftNeeded), min.y + unit * 0.4f);
    const ImVec2 cMax(std::max(cMin.x, max.x - unit * 0.4f), max.y - unit * 0.4f);
    dl->PushClipRect(cMin, cMax, true);
    dl->AddRectFilled(cMin, cMax, col(preview.canvasBackdrop));
    dl->AddRectFilled(ImVec2(cMin.x + unit * 0.5f, cMin.y + unit * 0.4f), ImVec2(cMax.x - unit * 0.5f, cMax.y - unit * 0.4f),
                      col(preview.checkerA));
    dl->AddLine(ImVec2(cMin.x + unit * 2.0f, cMin.y), ImVec2(cMin.x + unit * 2.0f, cMax.y), col(preview.guide));
    dl->AddRect(ImVec2(cMin.x + unit * 2.6f, cMin.y + unit * 0.9f), ImVec2(cMin.x + unit * 4.4f, cMax.y - unit * 0.9f),
                col(preview.selection));
    dl->PopClipRect();

    // a button, an accent button, text
    const auto pill = [&](ImVec2 at, float width, const ImVec4& fill, const ImVec4& text, const char* label) {
        const ImVec2 pillMax(at.x + width, at.y + pillHeight);
        dl->AddRectFilled(at, pillMax, col(fill), 4.0f);
        dl->PushClipRect(at, pillMax, true);
        dl->AddText(ImVec2(at.x + pillPad, at.y + (pillHeight - lineHeight) * 0.5f), col(text), label);
        dl->PopClipRect();
    };
    const ImVec2 b1(min.x + left, min.y + unit * 0.5f);
    pill(b1, buttonPill, preview.button, preview.text, buttonLabel);
    const ImVec2 b2(b1.x + buttonPill + unit * 0.4f, b1.y);
    pill(b2, selectedPill, preview.accent, contrastText(preview.accent), selectedLabel);
    const float textY = b1.y + pillHeight + unit * 0.4f;
    dl->AddText(ImVec2(b1.x, textY), col(preview.warning), warningLabel);
    dl->AddText(ImVec2(b1.x + ImGui::CalcTextSize(warningLabel).x + unit, textY), col(preview.text), tr(Str::ThemePreviewText));
    dl->PopClipRect();
}

} // namespace

bool SettingsWindow::drawTheme(Settings& s) {
    bool changed = false;
    if (!editInit_) {
        editDark_ = isDark();
        editInit_ = true;
    }
    const float em = ImGui::GetFontSize();
    const std::span<const TokenInfo> infos = tokenInfos();

    static constexpr Str kModeLabels[] = {Str::WordSystem, Str::WordLight, Str::WordDark};
    const float column = labelColumn({tr(Str::ThemeAppearance)});
    if (beginComboField(tr(Str::ThemeAppearance), column, tr(kModeLabels[static_cast<int>(s.theme)]),
                        fitWidth({tr(kModeLabels[0]), tr(kModeLabels[1]), tr(kModeLabels[2])}, em * 10.0f))) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(tr(kModeLabels[i]), static_cast<int>(s.theme) == i)) {
                s.theme = static_cast<ThemeMode>(i);
                editDark_ = resolveDark(s.theme);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    fieldHint(tr(Str::ThemeSystemHint), column);

    // Accent presets recolor both palettes at once. The one matching the accent being edited gets a ring.
    section(Str::ThemeAccentColor);
    {
        const float swatch = std::round(ImGui::GetFrameHeight() * 1.2f);
        const std::size_t accentIndex = 0; // "accent" is the first token
        const Rgba8 currentAccent = fromVec4(
            s.themeOverrides.side(editDark_)[accentIndex].value_or(baseTokens(editDark_).*(infos[accentIndex].member)));
        int index = 0;
        for (const AccentPreset& preset : kAccentPresets) {
            if (index++ > 0 && roomRight() >= swatch + ImGui::GetStyle().ItemSpacing.x)
                ImGui::SameLine();
            ImGui::PushID(index);
            const Rgba8 shown = editDark_ ? preset.dark : preset.light;
            if (ImGui::ColorButton("##accent", toImVec4(shown), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha,
                                   ImVec2(swatch, swatch))) {
                s.themeOverrides.dark[accentIndex] = toImVec4(preset.dark);
                s.themeOverrides.light[accentIndex] = toImVec4(preset.light);
                changed = true;
            }
            if (shown == currentAccent) {
                const float grow = std::max(2.0f, std::round(2.0f * uiScale()));
                const ImVec2 rMin = ImGui::GetItemRectMin();
                const ImVec2 rMax = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRect(ImVec2(rMin.x - grow, rMin.y - grow), ImVec2(rMax.x + grow, rMax.y + grow),
                                                    ImGui::GetColorU32(ImGuiCol_Text), ImGui::GetStyle().FrameRounding, 0,
                                                    std::max(1.0f, std::round(1.5f * uiScale())));
            }
            ImGui::SetItemTooltip("%s", tr(preset.name));
            ImGui::PopID();
        }
    }

    section(Str::ThemeColors);
    {
        // Which palette to edit: a two-segment toggle, the active half in the selection colors.
        const auto segment = [](Str label, bool active) {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            }
            const bool clicked = ImGui::Button(tr(label));
            if (active)
                ImGui::PopStyleColor(3);
            return clicked;
        };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(Str::ThemeEditingThe));
        ImGui::SameLine();
        if (segment(Str::WordDark, editDark_))
            editDark_ = true;
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (segment(Str::WordLight, !editDark_))
            editDark_ = false;

        // Import and export share the row when there is room, else they wrap under it.
        const float gap = ImGui::GetFontSize() * 1.5f;
        const float buttons = buttonWidth(tr(Str::ThemeImport)) + buttonWidth(tr(Str::ThemeExport)) + ImGui::GetStyle().ItemSpacing.x;
        if (roomRight() >= buttons + gap)
            ImGui::SameLine(0.0f, gap);
    }

    ImGui::BeginDisabled(dialogs_ == nullptr || dialogs_->busy());
    if (ImGui::Button(tr(Str::ThemeImport)) && dialogs_)
        dialogs_->open(NativeDialogs::Tag::ThemeImport);
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::ThemeExport)) && dialogs_)
        dialogs_->save(NativeDialogs::Tag::ThemeExport, "trace-theme.ini");
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tr(Str::ThemeExportTip));

    // A live strip drawn with the palette being edited, whichever one is applied right now.
    ImGui::Spacing();
    const ThemeTokens preview = resolveTokens(editDark_, s.themeOverrides);
    drawThemePreview(preview);

    auto& overrides = s.themeOverrides.side(editDark_);
    const ThemeTokens& base = baseTokens(editDark_);
    const float swatchColumn = std::round(ImGui::GetFrameHeight() * 1.6f);
    const float hexColumn = std::max(ImGui::CalcTextSize("#DDDDDDDD").x, ImGui::CalcTextSize("#00000000").x);
    const float resetColumn = buttonWidth(tr(Str::WordReset));
    for (std::size_t g = 0; g < kTokenGroups.size(); ++g) {
        const TokenGroup& group = kTokenGroups[g];
        section(group.title);
        ImGui::PushID(static_cast<int>(g));
        constexpr ImGuiTableFlags tableFlags =
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoSavedSettings;
        if (ImGui::BeginTable("##tokens", 4, tableFlags)) {
            ImGui::TableSetupColumn("##swatch", ImGuiTableColumnFlags_WidthFixed, swatchColumn);
            ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##hex", ImGuiTableColumnFlags_WidthFixed, hexColumn);
            ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, resetColumn);
            for (const int token : group.tokens) {
                const std::size_t i = static_cast<std::size_t>(token);
                const ImVec4 builtIn = base.*(infos[i].member);
                Rgba8 current = fromVec4(overrides[i].value_or(builtIn));

                ImGui::PushID(token);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (colorSwatch(themeRows_[i], current, &builtIn)) {
                    overrides[i] = toImVec4(current);
                    changed = true;
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(tr(tokenString(i)));
                if (overrides[i]) { // a dot in the accent color marks a color changed from the built-in one
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, tokens().accent);
                    ImGui::Bullet();
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("%s", tr(Str::ThemeColorChanged));
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::AlignTextToFramePadding();
                const std::string hex = formatColor(current);
                ImGui::TextDisabled("%s", current.a == 255 ? hex.substr(0, 7).c_str() : hex.c_str());

                ImGui::TableSetColumnIndex(3);
                if (overrides[i]) {
                    if (ImGui::SmallButton(tr(Str::WordReset))) {
                        overrides[i].reset();
                        changed = true;
                    }
                } else {
                    ImGui::Dummy(ImVec2(resetColumn, 1.0f)); // keeps the columns from jumping
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    const bool anyEdited = std::any_of(overrides.begin(), overrides.end(), [](const auto& v) { return v.has_value(); });
    ImGui::BeginDisabled(!anyEdited);
    if (ImGui::Button(tr(editDark_ ? Str::ThemeResetDark : Str::ThemeResetLight))) {
        overrides = {};
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.themeOverrides.empty());
    if (ImGui::Button(tr(Str::ThemeResetBoth))) {
        s.themeOverrides = ThemeOverrides{};
        changed = true;
    }
    ImGui::EndDisabled();
    return changed;
}

namespace {

constexpr Str kPatternLabels[kPatternStyleCount] = {Str::PatternSolid, Str::PatternChecker,  Str::PatternGrid,
                                                    Str::PatternDots,  Str::PatternDiagonal, Str::PatternCross};

// A labelled combo of pattern styles with a small preview of the chosen one, drawn with the real pattern code and
// the colors of the applied theme.
bool patternCombo(Str labelText, float column, PatternStyle& style, bool transparency, const Settings& s) {
    bool changed = false;
    const float swatch = ImGui::GetFrameHeight();
    const ThemeTokens& t = tokens();
    const auto col = [](const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); };
    const ImU32 base = col(transparency ? t.checkerA : t.canvasBackdrop);
    const ImU32 marks = col(transparency ? t.checkerB : t.pattern);

    ImGui::PushID(stringKey(labelText));
    fieldLabel(tr(labelText), column);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(swatch, swatch));
    drawPattern(ImGui::GetWindowDrawList(), style, min, ImVec2(min.x + swatch, min.y + swatch), min,
                std::max(6.0f, static_cast<float>(s.patternSpacing) * 0.5f), base, marks, marks);
    ImGui::GetWindowDrawList()->AddRect(min, ImVec2(min.x + swatch, min.y + swatch), col(t.canvasBorder));
    ImGui::SameLine();
    std::array<const char*, kPatternStyleCount> labels{};
    for (int i = 0; i < kPatternStyleCount; ++i)
        labels[static_cast<std::size_t>(i)] = tr(kPatternLabels[i]);
    ImGui::SetNextItemWidth(fitWidth(labels, ImGui::GetFontSize() * 12.0f));
    if (ImGui::BeginCombo("##style", tr(kPatternLabels[static_cast<int>(style)]))) {
        for (int i = 0; i < kPatternStyleCount; ++i) {
            if (ImGui::Selectable(tr(kPatternLabels[i]), static_cast<int>(style) == i)) {
                style = static_cast<PatternStyle>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

} // namespace

bool SettingsWindow::drawView(Settings& s) {
    bool changed = false;
    const float em = ImGui::GetFontSize();
    const float column = labelColumn({tr(Str::ViewBehindImage), tr(Str::ViewBehindTransparent), tr(Str::ViewPatternSpacing)});

    section(Str::ViewSectionPanels);
    changed |= checkField(Str::ViewToolsPanel, s.showToolsPanel);
    changed |= checkField(Str::ViewLayersPanel, s.showLayersPanel);
    changed |= checkField(Str::ViewStatusBar, s.showStatusBar);
    ImGui::Indent(em * 1.5f);
    ImGui::BeginDisabled(!s.showStatusBar);
    changed |= checkField(Str::ViewSysStats, s.showSysStats);
    ImGui::EndDisabled();
    ImGui::Unindent(em * 1.5f);

    // Puts every mode's windows back where they started and shows the ones closed with their X.
    ImGui::Spacing();
    if (ImGui::Button(tr(Str::ViewResetLayout)))
        resetLayoutRequested_ = true;
    hint(tr(Str::ViewResetLayoutHint), true);

    section(Str::ViewSectionAids);
    changed |= checkField(Str::ViewPixelGrid, s.pixelGrid);
    changed |= checkField(Str::ViewRulers, s.showRulers);
    changed |= checkField(Str::ViewGuides, s.showGuides);
    ImGui::Indent(em * 1.5f);
    ImGui::BeginDisabled(!s.showGuides);
    changed |= checkField(Str::ViewSnapToGuides, s.snapToGuides);
    ImGui::EndDisabled();
    ImGui::Unindent(em * 1.5f);

    section(Str::ViewBackgrounds);
    changed |= patternCombo(Str::ViewBehindImage, column, s.backdropPattern, false, s);
    changed |= patternCombo(Str::ViewBehindTransparent, column, s.transparencyPattern, true, s);
    changed |= numberField(tr(Str::ViewPatternSpacing), column, s.patternSpacing, kMinPatternSpacing, kMaxPatternSpacing, "px");

    section(Str::ViewWhiteboards);
    int paper = s.whiteboardDark ? 1 : 0;
    changed |= ImGui::RadioButton(tr(Str::WordWhite), &paper, 0);
    ImGui::SameLine(0.0f, em * 1.5f);
    changed |= ImGui::RadioButton(tr(Str::WordBlack), &paper, 1);
    s.whiteboardDark = paper == 1;

    section(Str::ViewNewCanvasOpensAt);
    int zoom = static_cast<int>(s.newDocZoom);
    changed |= ImGui::RadioButton(tr(Str::ViewFitToWindow), &zoom, static_cast<int>(Settings::NewDocZoom::Fit));
    ImGui::SameLine(0.0f, em * 1.5f);
    changed |= ImGui::RadioButton("100%", &zoom, static_cast<int>(Settings::NewDocZoom::ActualSize));
    s.newDocZoom = static_cast<Settings::NewDocZoom>(zoom);
    return changed;
}

bool SettingsWindow::drawTools(Settings& s, Toolbox& toolbox) {
    bool changed = false;
    const float em = ImGui::GetFontSize();
    ToolSettings& t = s.toolDefaults;
    const float column = labelColumn({tr(Str::ToolsStartTool), tr(Str::ToolsBrushSize), tr(Str::ToolsStrokeWidth),
                                      tr(Str::ToolsShapeMode), tr(Str::ToolsFillTolerance), tr(Str::ToolsTextSize)});

    section(Str::ToolsSectionStartup);
    changed |= checkField(Str::ToolsRestoreLast, s.restoreLastToolSettings, tr(Str::ToolsRestoreHint));
    ImGui::Spacing();
    std::array<const char*, kToolCount> toolLabels{};
    for (int i = 0; i < kToolCount; ++i)
        toolLabels[static_cast<std::size_t>(i)] = tr(toolString(static_cast<ToolId>(i)));
    if (beginComboField(tr(Str::ToolsStartTool), column, tr(toolString(s.startTool)), fitWidth(toolLabels, em * 10.0f))) {
        for (int i = 0; i < kToolCount; ++i) {
            const ToolId id = static_cast<ToolId>(i);
            if (ImGui::Selectable(tr(toolString(id)), s.startTool == id)) {
                s.startTool = id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    // The values below only matter while the last session's settings are not restored.
    ImGui::BeginDisabled(s.restoreLastToolSettings);
    section(Str::ToolsSectionBrush);
    changed |= numberField(tr(Str::ToolsBrushSize), column, t.brushSize, ToolSettings::kMinSize, ToolSettings::kMaxSize, "px");
    changed |= numberField(tr(Str::ToolsStrokeWidth), column, t.strokeWidth, ToolSettings::kMinSize,
                           ToolSettings::kMaxStrokeWidth, "px");
    int mode = static_cast<int>(t.shapeMode);
    static constexpr Str kShapeLabels[] = {Str::WordOutline, Str::WordFilled, Str::WordOutlineAndFill};
    if (beginComboField(tr(Str::ToolsShapeMode), column, tr(kShapeLabels[mode]),
                        fitWidth({tr(kShapeLabels[0]), tr(kShapeLabels[1]), tr(kShapeLabels[2])}, em * 10.0f))) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(tr(kShapeLabels[i]), mode == i)) {
                t.shapeMode = static_cast<ShapeMode>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    changed |= checkField(Str::WordAntialias, t.antialias);
    changed |= checkField(Str::ToolsEraserTransparent, t.eraseToTransparent);

    section(Str::ToolsSectionFill);
    changed |= numberField(tr(Str::ToolsFillTolerance), column, t.fillTolerance, 0, 255);
    changed |= numberField(tr(Str::ToolsTextSize), column, t.textSize, ToolSettings::kMinTextSize, ToolSettings::kMaxTextSize, "px");

    section(Str::ToolsSectionColors);
    const auto colorRow = [&](Str name, float rgba[4], Rgba8& target) {
        ImGui::PushID(stringKey(name));
        const bool edited = colorSwatch(rgba, target, nullptr);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(name));
        ImGui::PopID();
        return edited;
    };
    changed |= colorRow(Str::WordPrimaryColor, primary_, t.primary);
    changed |= colorRow(Str::WordSecondaryColor, secondary_, t.secondary);

    section(Str::ToolsStartingValues);
    if (ImGui::Button(tr(Str::ToolsUseCurrent))) {
        t = toolbox.settings();
        changed = true;
    }
    ImGui::SetItemTooltip("%s", tr(Str::ToolsUseCurrentTip));
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::ToolsApplyNow)))
        toolbox.settings() = t;
    ImGui::SetItemTooltip("%s", tr(Str::ToolsApplyNowTip));
    ImGui::EndDisabled();
    return changed;
}

bool SettingsWindow::drawPixelArt(Settings& s) {
    bool changed = false;
    PixelSettings& px = s.pixel;
    const float column = labelColumn({tr(Str::WordWidthPx), tr(Str::WordHeightPx), tr(Str::PixelGridFrom), tr(Str::PixelTileDim),
                                      tr(Str::PixelOnionBefore), tr(Str::PixelOnionAfter), tr(Str::PixelOnionOpacity)});

    section(Str::GeneralNewCanvas);
    changed |= numberField(tr(Str::WordWidthPx), column, px.newWidth, 1, kMaxPixelCanvasSide);
    changed |= numberField(tr(Str::WordHeightPx), column, px.newHeight, 1, kMaxPixelCanvasSide);
    fieldHint(trf(Str::GeneralNewCanvasHint, {"1", std::to_string(kMaxPixelCanvasSide)}).c_str(), column);

    section(Str::PixelSectionGrid);
    changed |= numberField(tr(Str::PixelGridFrom), column, px.gridZoomPercent, kMinPixelGridZoomPercent, kMaxPixelGridZoomPercent, "%");
    fieldHint(tr(Str::PixelGridHint), column);

    section(Str::PixelSectionDrawing);
    changed |= checkField(Str::PixelPerfectDefault, px.pixelPerfectDefault, tr(Str::PixelPerfectHint));

    section(Str::PixelSectionTile);
    changed |= numberField(tr(Str::PixelTileDim), column, px.tileDimPercent, 0, 100, "%");
    fieldHint(tr(Str::PixelTileHint), column);

    section(Str::PixelSectionOnion);
    changed |= numberField(tr(Str::PixelOnionBefore), column, px.onionBefore, 0, kMaxOnionFrames);
    changed |= numberField(tr(Str::PixelOnionAfter), column, px.onionAfter, 0, kMaxOnionFrames);
    changed |= numberField(tr(Str::PixelOnionOpacity), column, px.onionOpacityPercent, kMinOnionOpacityPercent, 100, "%");
    changed |= checkField(Str::PixelOnionLoop, px.onionLoop);
    fieldHint(tr(Str::PixelOnionHint), column);
    return changed;
}

bool SettingsWindow::drawFlowchart(Settings& s) {
    bool changed = false;
    FlowSettings& fs = s.flow;
    const float em = ImGui::GetFontSize();
    const float column = labelColumn({tr(Str::FlowSettingsGridSize), tr(Str::FlowPropRoute), tr(Str::FlowPropStartArrow),
                                      tr(Str::FlowPropEndArrow)});

    section(Str::FlowSettingsBoard);
    changed |= numberField(tr(Str::FlowSettingsGridSize), column, fs.gridSize, kMinFlowGridSize, kMaxFlowGridSize);
    fieldHint(tr(Str::FlowSettingsGridHint), column);
    ImGui::Spacing();
    changed |= checkField(Str::MenuFlowShowGrid, fs.showGrid);
    changed |= checkField(Str::MenuFlowSnapToGrid, fs.snapToGrid);
    changed |= checkField(Str::MenuFlowSmartGuides, fs.smartGuides);
    hint(tr(Str::FlowSettingsAidsHint));

    section(Str::FlowPropNewConnectors);
    static constexpr std::array<Str, 3> kRoutes = {Str::FlowRouteCurved, Str::FlowRouteElbow, Str::FlowRouteStraight};
    static constexpr std::array<Str, 4> kArrows = {Str::FlowArrowNone, Str::FlowArrowArrow, Str::FlowArrowOpen,
                                                   Str::FlowArrowDot};
    const auto combo = [&](Str label, auto& value, const auto& names) {
        const int count = static_cast<int>(names.size());
        const int index = std::clamp(static_cast<int>(value), 0, count - 1);
        std::array<const char*, 4> shown{};
        for (int i = 0; i < count; ++i)
            shown[static_cast<std::size_t>(i)] = tr(names[static_cast<std::size_t>(i)]);
        const float width = fitWidth(std::span<const char* const>(shown.data(), static_cast<std::size_t>(count)), em * 12.0f);
        if (beginComboField(tr(label), column, shown[static_cast<std::size_t>(index)], width)) {
            for (int i = 0; i < count; ++i) {
                if (ImGui::Selectable(shown[static_cast<std::size_t>(i)], i == index)) {
                    value = static_cast<std::remove_reference_t<decltype(value)>>(i);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    };
    combo(Str::FlowPropRoute, fs.defaultRoute, kRoutes);
    combo(Str::FlowPropStartArrow, fs.defaultArrowStart, kArrows);
    combo(Str::FlowPropEndArrow, fs.defaultArrowEnd, kArrows);
    fieldHint(tr(Str::FlowSettingsConnectorHint), column);
    return changed;
}

void SettingsWindow::drawScripts() {
    ImGui::TextUnformatted(tr(Str::ScriptsPlanned));
}

bool SettingsWindow::pollKeyCapture(Keymap& keymap) {
    if (!capture_)
        return false;

    Chord chord;
    switch (pollCapture(chord)) {
    case CaptureResult::None:
        return false;
    case CaptureResult::Cancelled:
        endCapture();
        return false;
    case CaptureResult::Captured:
        break;
    }

    const SlotRef target = *capture_;
    if (keymap.slots(target.action)[static_cast<std::size_t>(target.slot)] == chord) {
        endCapture();
        return false;
    }
    if (const auto owner = keymap.findConflict(chord, target.action)) {
        // Hold the decision until the user picks Replace or Cancel; shortcuts stay suspended meanwhile.
        const Conflict conflict{target, chord, *owner};
        endCapture();
        conflict_ = conflict;
        setCapturing(true);
        return false;
    }
    const bool applied = keymap.set(target.action, target.slot, chord);
    endCapture();
    return applied;
}

bool SettingsWindow::drawConflictBanner(Keymap& keymap) {
    if (!conflict_)
        return false;

    bool changed = false;
    const Conflict conflict = *conflict_;
    ImGui::PushStyleColor(ImGuiCol_Text, tokens().warning);
    ImGui::TextWrapped("%s", trf(Str::KeysConflict, {displayChord(conflict.chord), tr(actionString(conflict.owner))}).c_str());
    ImGui::PopStyleColor();

    if (ImGui::Button(tr(Str::WordReplace))) {
        const Keymap::Slots& ownerSlots = keymap.slots(conflict.owner);
        for (int i = 0; i < kChordSlots; ++i) {
            if (ownerSlots[static_cast<std::size_t>(i)] == conflict.chord)
                keymap.set(conflict.owner, i, std::nullopt);
        }
        changed = keymap.set(conflict.target.action, conflict.target.slot, conflict.chord);
        endCapture();
    } else {
        ImGui::SameLine();
        if (ImGui::Button(tr(Str::WordCancel)))
            endCapture();
    }
    ImGui::Spacing();
    return changed;
}

bool SettingsWindow::drawKeySlot(Keymap& keymap, Action action, int slot) {
    bool changed = false;
    const auto& chord = keymap.slots(action)[static_cast<std::size_t>(slot)];
    const bool listening = capture_ && capture_->action == action && capture_->slot == slot;

    ImGui::PushID(slot);
    const float clearWidth = ImGui::GetFrameHeight();
    const bool showClear = chord && !listening;
    const float width = ImGui::GetContentRegionAvail().x -
                        (showClear ? clearWidth + ImGui::GetStyle().ItemSpacing.x : 0.0f);

    std::string label = listening ? std::string(tr(Str::KeysPressKeys))
                        : chord   ? displayChord(*chord)
                                  : std::string(tr(Str::KeysUnbound));
    label += "##slot";
    if (listening)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    else if (!chord)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::Button(label.c_str(), ImVec2(width, 0.0f)))
        startCapture(SlotRef{action, slot});
    if (listening || !chord)
        ImGui::PopStyleColor();
    if (chord && ImGui::IsItemClicked(ImGuiMouseButton_Right) && !listening) {
        keymap.set(action, slot, std::nullopt);
        changed = true;
    }

    if (showClear && chord) {
        ImGui::SameLine();
        if (ImGui::Button("x", ImVec2(clearWidth, 0.0f))) {
            keymap.set(action, slot, std::nullopt);
            changed = true;
        }
        ImGui::SetItemTooltip("%s", tr(Str::KeysClearTip));
    }
    ImGui::PopID();
    return changed;
}

bool SettingsWindow::drawKeybinds(Settings& s) {
    Keymap& keymap = s.keymap;
    bool changed = pollKeyCapture(keymap); // before any button, so the click that starts a capture is not read

    const float resetAllWidth = buttonWidth(tr(Str::KeysResetAll));
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - resetAllWidth - ImGui::GetStyle().ItemSpacing.x,
                                     ImGui::GetFontSize() * 6.0f));
    ImGui::InputTextWithHint("##filter", tr(Str::KeysFilter), filter_, sizeof(filter_));
    ImGui::SameLine();
    if (ImGui::Button(tr(Str::KeysResetAll))) {
        keymap.resetToDefaults();
        endCapture();
        changed = true;
    }

    changed |= drawConflictBanner(keymap);

    const std::string needle = lower(filter_);
    const auto matches = [&](Action action) {
        if (needle.empty())
            return true;
        const ActionInfo& info = actionInfo(action);
        std::string haystack = lower(info.label) + ' ' + lower(info.group) + ' ' + lower(tr(actionString(action))) + ' ' +
                               lower(tr(groupString(info.group)));
        for (const Chord& chord : keymap.chords(action))
            haystack += ' ' + lower(formatChord(chord));
        return haystack.find(needle) != std::string::npos;
    };

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                           ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##keybinds", 4, tableFlags)) {
        ImGui::TableSetupColumn(tr(Str::KeysColumnAction), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(tr(Str::KeysColumnShortcut), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn(tr(Str::KeysColumnAlternate), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn(tr(Str::WordReset), ImGuiTableColumnFlags_WidthFixed, buttonWidth(tr(Str::WordReset)));
        ImGui::TableHeadersRow();

        bool any = false;
        for (const char* group : kGroups) {
            bool headerDrawn = false;
            for (int i = 0; i < kActionCount; ++i) {
                const Action action = static_cast<Action>(i);
                const ActionInfo& info = actionInfo(action);
                if (std::string_view(info.group) != group || !matches(action))
                    continue;
                any = true;

                if (!headerDrawn) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", tr(groupString(group)));
                    headerDrawn = true;
                }

                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(tr(actionString(action)));
                ImGui::TableSetColumnIndex(1);
                changed |= drawKeySlot(keymap, action, 0);
                ImGui::TableSetColumnIndex(2);
                changed |= drawKeySlot(keymap, action, 1);
                ImGui::TableSetColumnIndex(3);
                ImGui::BeginDisabled(keymap.isDefault(action));
                if (ImGui::Button(tr(Str::WordReset))) {
                    keymap.reset(action);
                    endCapture();
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
        if (!any)
            hint(tr(Str::KeysNoMatch));
    }
    hint(tr(Str::KeysHelp));
    return changed;
}

void SettingsWindow::drawAbout(const std::string& settingsPath) {
    // The app icon beside the name and tagline. Without a texture (no renderer, decode failure) the text stands alone.
    const float iconSide = std::round(ImGui::GetFontSize() * 4.0f);
    SDL_Texture* iconTexture = appIconTexture();
    if (iconTexture) {
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(iconTexture)), ImVec2(iconSide, iconSide));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted("Trace");
    ImGui::PopFont();
    hint(tr(Str::AboutTagline));
    ImGui::EndGroup();
    ImGui::Spacing();

#ifdef NDEBUG
    constexpr const char* build = "Release";
#else
    constexpr const char* build = "Debug";
#endif
    const int sdl = SDL_GetVersion();
    // The label column is as wide as the longest label (in the current language) plus an em.
    float labelWidth = 0.0f;
    for (const char* label : {tr(Str::AboutVersion), tr(Str::AboutBuild), tr(Str::AboutPlatform), "SDL", "Dear ImGui",
                              tr(Str::AboutLicense)})
        labelWidth = std::max(labelWidth, ImGui::CalcTextSize(label).x);
    labelWidth += ImGui::GetFontSize();
    labeledText(labelWidth, tr(Str::AboutVersion), "%s", TRACE_VERSION);
    labeledText(labelWidth, tr(Str::AboutBuild), "%s", build);
    labeledText(labelWidth, tr(Str::AboutPlatform), "%s", SDL_GetPlatform());
    labeledText(labelWidth, "SDL", "%d.%d.%d", SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl),
                SDL_VERSIONNUM_MICRO(sdl));
    labeledText(labelWidth, "Dear ImGui", "%s", IMGUI_VERSION);
    labeledText(labelWidth, tr(Str::AboutLicense), "MIT");

    section(Str::AboutSettingsFile);
    if (settingsPath.empty()) {
        ImGui::TextWrapped("%s", tr(Str::AboutNoDataDir));
        return;
    }
    ImGui::TextWrapped("%s", settingsPath.c_str());
    if (ImGui::Button(tr(Str::AboutOpenFolder))) {
        const std::string folder = std::filesystem::path(settingsPath).parent_path().string();
        if (!SDL_OpenURL(fileUrl(folder).c_str()))
            SDL_Log("SDL_OpenURL failed: %s", SDL_GetError());
    }
}

} // namespace trace
