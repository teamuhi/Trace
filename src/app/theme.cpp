#include "app/theme.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace trace {

namespace {

ImVec4 rgb(int r, int g, int b, int a = 255) {
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                  static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f);
}

ImVec4 withAlpha(ImVec4 c, float alpha) {
    c.w = alpha;
    return c;
}

// Opaque blend of two colors: t = 0 gives a, t = 1 gives b.
ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
}

const ThemeTokens kDark{
    /*windowBg*/ rgb(30, 31, 34),
    /*canvasBackdrop*/ rgb(22, 23, 25),
    /*checkerA*/ rgb(74, 74, 78),
    /*checkerB*/ rgb(98, 98, 102),
    /*rulerBg*/ rgb(40, 42, 46),
    /*rulerText*/ rgb(170, 174, 180),
    /*guide*/ rgb(0, 200, 220),
    /*selection*/ rgb(110, 170, 255),
    /*statusBarBg*/ rgb(36, 38, 42),
    /*text*/ rgb(225, 227, 230),
    /*accent*/ rgb(66, 133, 244),
    /*pixelGrid*/ rgb(150, 150, 150, 110),
    /*canvasBorder*/ rgb(70, 72, 78),
    /*warning*/ rgb(240, 180, 70),
    /*button*/ rgb(44, 72, 118),
    /*pattern*/ rgb(150, 154, 162, 60),
};

const ThemeTokens kLight{
    /*windowBg*/ rgb(246, 247, 249),
    /*canvasBackdrop*/ rgb(222, 225, 230),
    /*checkerA*/ rgb(255, 255, 255),
    /*checkerB*/ rgb(204, 204, 204),
    /*rulerBg*/ rgb(236, 238, 241),
    /*rulerText*/ rgb(86, 90, 96),
    /*guide*/ rgb(0, 150, 170),
    /*selection*/ rgb(25, 103, 210),
    /*statusBarBg*/ rgb(232, 234, 237),
    /*text*/ rgb(32, 33, 36),
    /*accent*/ rgb(25, 103, 210),
    /*pixelGrid*/ rgb(90, 90, 90, 110),
    /*canvasBorder*/ rgb(160, 164, 172),
    /*warning*/ rgb(180, 110, 0),
    /*button*/ rgb(158, 189, 231),
    /*pattern*/ rgb(90, 96, 106, 60),
};

constexpr TokenInfo kTokenInfos[kTokenCount] = {
    {"accent", "Accent", &ThemeTokens::accent},
    {"button", "Buttons", &ThemeTokens::button},
    {"text", "Text", &ThemeTokens::text},
    {"window_bg", "Window background", &ThemeTokens::windowBg},
    {"status_bar_bg", "Status bar and menu bar", &ThemeTokens::statusBarBg},
    {"canvas_backdrop", "Canvas backdrop", &ThemeTokens::canvasBackdrop},
    {"pattern", "Backdrop pattern marks", &ThemeTokens::pattern},
    {"checker_a", "Transparency, light squares", &ThemeTokens::checkerA},
    {"checker_b", "Transparency, dark squares", &ThemeTokens::checkerB},
    {"canvas_border", "Canvas border", &ThemeTokens::canvasBorder},
    {"pixel_grid", "Pixel grid", &ThemeTokens::pixelGrid},
    {"ruler_bg", "Ruler background", &ThemeTokens::rulerBg},
    {"ruler_text", "Ruler text", &ThemeTokens::rulerText},
    {"guide", "Guides", &ThemeTokens::guide},
    {"selection", "Selection outline", &ThemeTokens::selection},
    {"warning", "Warnings", &ThemeTokens::warning},
};

ThemeMode g_mode = ThemeMode::System;
bool g_dark = true;
float g_scale = 1.0f;     // OS content scale
float g_userScale = 1.0f; // multiplier from the Settings window
float g_fontPixels = 14.0f; // base font size before scaling
ThemeOverrides g_overrides;
ThemeTokens g_merged[2] = {kLight, kDark}; // [dark ? 1 : 0]

void rebuildMerged() {
    for (const bool dark : {false, true})
        g_merged[dark ? 1 : 0] = resolveTokens(dark, g_overrides);
}

bool sameColor(const ImVec4& a, const ImVec4& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

} // namespace

std::span<const TokenInfo> tokenInfos() {
    return kTokenInfos;
}

ThemeTokens resolveTokens(bool dark, const ThemeOverrides& overrides) {
    ThemeTokens merged = dark ? kDark : kLight;
    const auto& side = overrides.side(dark);
    for (int i = 0; i < kTokenCount; ++i) {
        if (side[static_cast<std::size_t>(i)])
            merged.*(kTokenInfos[i].member) = *side[static_cast<std::size_t>(i)];
    }
    return merged;
}

bool ThemeOverrides::empty() const noexcept {
    for (const bool darkMode : {false, true}) {
        for (const auto& value : side(darkMode)) {
            if (value)
                return false;
        }
    }
    return true;
}

bool ThemeOverrides::operator==(const ThemeOverrides& other) const noexcept {
    for (const bool darkMode : {false, true}) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(kTokenCount); ++i) {
            const auto& a = side(darkMode)[i];
            const auto& b = other.side(darkMode)[i];
            if (a.has_value() != b.has_value() || (a && !sameColor(*a, *b)))
                return false;
        }
    }
    return true;
}

const ThemeTokens& tokens() {
    return g_merged[g_dark ? 1 : 0];
}

const ThemeTokens& baseTokens(bool dark) {
    return dark ? kDark : kLight;
}

void setThemeOverrides(const ThemeOverrides& overrides) {
    g_overrides = overrides;
    rebuildMerged();
}

bool isDark() {
    return g_dark;
}

ImVec4 contrastText(const ImVec4& background) {
    // Relative luminance with the usual sRGB weights; the switch point sits a little above the middle
    // because white text stays readable on mid tones better than black does.
    const float luminance = 0.2126f * background.x + 0.7152f * background.y + 0.0722f * background.z;
    return luminance > 0.6f ? rgb(16, 17, 20) : rgb(255, 255, 255);
}

bool resolveDark(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Light:
        return false;
    case ThemeMode::Dark:
        return true;
    case ThemeMode::System:
        break;
    }
    return SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
}

void applyTheme(bool dark) {
    g_dark = dark;
    const ThemeTokens& t = tokens();

    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    if (dark)
        ImGui::StyleColorsDark(&style);
    else
        ImGui::StyleColorsLight(&style);

    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    // Everything that ImGui colors blue by default is derived from the accent, so a custom accent recolors
    // the whole interface instead of leaving stock blue title bars and tabs behind.
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = t.text;
    c[ImGuiCol_WindowBg] = t.windowBg;
    c[ImGuiCol_ChildBg] = t.windowBg;
    c[ImGuiCol_PopupBg] = t.windowBg;
    c[ImGuiCol_MenuBarBg] = t.statusBarBg;
    c[ImGuiCol_CheckMark] = t.accent;
    c[ImGuiCol_SliderGrab] = t.accent;
    c[ImGuiCol_SliderGrabActive] = t.accent;
    c[ImGuiCol_Header] = withAlpha(t.accent, 0.30f);
    c[ImGuiCol_HeaderHovered] = withAlpha(t.accent, 0.45f);
    c[ImGuiCol_HeaderActive] = withAlpha(t.accent, 0.60f);
    c[ImGuiCol_Button] = t.button;
    c[ImGuiCol_ButtonHovered] = withAlpha(t.accent, 0.55f);
    c[ImGuiCol_ButtonActive] = withAlpha(t.accent, 0.75f);
    c[ImGuiCol_FrameBgHovered] = withAlpha(t.accent, 0.30f);
    c[ImGuiCol_FrameBgActive] = withAlpha(t.accent, 0.50f);
    c[ImGuiCol_TextSelectedBg] = withAlpha(t.accent, 0.35f);
    c[ImGuiCol_NavHighlight] = t.accent;
    c[ImGuiCol_DockingPreview] = withAlpha(t.accent, 0.70f);
    c[ImGuiCol_DockingEmptyBg] = t.canvasBackdrop;
    c[ImGuiCol_TitleBg] = mix(t.windowBg, t.text, 0.06f);
    c[ImGuiCol_TitleBgActive] = mix(t.windowBg, t.accent, 0.55f);
    c[ImGuiCol_TitleBgCollapsed] = withAlpha(mix(t.windowBg, t.text, 0.06f), 0.6f);
    c[ImGuiCol_Tab] = mix(t.windowBg, t.accent, 0.25f);
    c[ImGuiCol_TabHovered] = withAlpha(t.accent, 0.80f);
    c[ImGuiCol_TabSelected] = mix(t.windowBg, t.accent, 0.55f);
    c[ImGuiCol_TabSelectedOverline] = t.accent;
    c[ImGuiCol_TabDimmed] = mix(t.windowBg, t.text, 0.05f);
    c[ImGuiCol_TabDimmedSelected] = mix(t.windowBg, t.accent, 0.30f);
    c[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(t.accent, 0.5f);
    c[ImGuiCol_ResizeGrip] = withAlpha(t.accent, 0.25f);
    c[ImGuiCol_ResizeGripHovered] = withAlpha(t.accent, 0.67f);
    c[ImGuiCol_ResizeGripActive] = withAlpha(t.accent, 0.95f);
    c[ImGuiCol_SeparatorHovered] = withAlpha(t.accent, 0.78f);
    c[ImGuiCol_SeparatorActive] = t.accent;

    style.ScaleAllSizes(uiScale());
    style.FontSizeBase = std::round(g_fontPixels * g_scale); // whole pixels keep text sharp; the user scale multiplies on top
    style.FontScaleMain = g_userScale;
}

void setUiScale(float scale) {
    g_scale = scale > 0.0f ? scale : 1.0f;
}

void setFontPixels(float pixels) {
    g_fontPixels = pixels > 0.0f ? std::clamp(pixels, 6.0f, 64.0f) : 14.0f;
}

void setUserScale(float scale) {
    g_userScale = scale > 0.0f ? std::clamp(scale, 0.25f, 4.0f) : 1.0f;
}

float uiScale() {
    return g_scale * g_userScale;
}

void setThemeMode(ThemeMode mode) {
    g_mode = mode;
    applyTheme(resolveDark(mode));
}

ThemeMode themeMode() {
    return g_mode;
}

void handleSystemThemeChanged() {
    if (g_mode == ThemeMode::System)
        applyTheme(resolveDark(ThemeMode::System));
}

} // namespace trace
