#pragma once

#include <imgui.h>

#include <array>
#include <optional>
#include <span>

namespace trace {

enum class ThemeMode { System, Light, Dark };

struct ThemeTokens {
    ImVec4 windowBg;
    ImVec4 canvasBackdrop;
    ImVec4 checkerA;
    ImVec4 checkerB;
    ImVec4 rulerBg;
    ImVec4 rulerText;
    ImVec4 guide;
    ImVec4 selection;
    ImVec4 statusBarBg;
    ImVec4 text;
    ImVec4 accent;
    ImVec4 pixelGrid;
    ImVec4 canvasBorder;
    ImVec4 warning;
    ImVec4 button;  // resting color of buttons
    ImVec4 pattern; // marks of the backdrop and transparency patterns (grid lines, dots, ...)
};

// One entry per token: the key used in settings.ini and theme files, the label on the Theme page, and the
// member it edits. Order is the order of the Theme page.
struct TokenInfo {
    const char* key;
    const char* label;
    ImVec4 ThemeTokens::* member;
};
inline constexpr int kTokenCount = 16;
std::span<const TokenInfo> tokenInfos();

// Colors the user changed, per theme. A token without a value keeps the built-in color.
struct ThemeOverrides {
    std::array<std::optional<ImVec4>, kTokenCount> dark;
    std::array<std::optional<ImVec4>, kTokenCount> light;

    auto& side(bool darkMode) noexcept { return darkMode ? dark : light; }
    const auto& side(bool darkMode) const noexcept { return darkMode ? dark : light; }
    bool empty() const noexcept;
    bool operator==(const ThemeOverrides& other) const noexcept;
};

// Tokens for the currently applied theme: the built-in colors with the overrides on top.
const ThemeTokens& tokens();

// The built-in colors of a theme, without overrides (what "reset" returns to).
const ThemeTokens& baseTokens(bool dark);

// The built-in colors of a theme with `overrides` applied (what the Theme page previews).
ThemeTokens resolveTokens(bool dark, const ThemeOverrides& overrides);

// Replaces the overrides. Takes effect on the next applyTheme / setThemeMode, so call it between frames.
void setThemeOverrides(const ThemeOverrides& overrides);

// True while the dark palette is applied.
bool isDark();

// White or near black, whichever reads better on `background` (used for text and icons on the accent).
ImVec4 contrastText(const ImVec4& background);

// Resolves System via SDL_GetSystemTheme (unknown is treated as dark).
bool resolveDark(ThemeMode mode);

// Resets the ImGui style, applies the palette for `dark` and scales sizes by the UI scale.
void applyTheme(bool dark);

// The OS content scale; fixed for the session.
void setUiScale(float scale);

// A multiplier on top of the OS scale, set from the Settings window. Takes effect on the next theme apply,
// so call it between frames, before setThemeMode / applyTheme.
void setUserScale(float scale);

// The base font size in pixels at 100% scale (the Settings "font size"). Takes effect on the next
// applyTheme, like the scale.
void setFontPixels(float pixels);

// Combined scale (OS scale times user scale) that sizes and window defaults should follow.
float uiScale();

void setThemeMode(ThemeMode mode);
ThemeMode themeMode();

// Call on SDL_EVENT_SYSTEM_THEME_CHANGED; re-applies the theme only when the mode is System.
void handleSystemThemeChanged();

} // namespace trace
