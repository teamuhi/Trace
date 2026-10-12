#pragma once

#include <string>

#include "app/fonts.hpp"
#include "app/theme.hpp"
#include "core/pattern.hpp"
#include "core/keymap.hpp"
#include "tools/tool.hpp"

namespace trace {

inline constexpr int kMinCanvasSize = 1;
inline constexpr int kMaxCanvasSize = 16384;
inline constexpr int kMinUiScalePercent = 75;
inline constexpr int kMaxUiScalePercent = 250;
inline constexpr int kSettingsPageCount = 7;
inline constexpr int kMinHistoryLimitMb = 16;
inline constexpr int kMaxHistoryLimitMb = 4096;

// Everything the Settings window edits and settings.ini stores.
struct Settings {
    enum class NewDocZoom { Fit, ActualSize };

    // General
    ThemeMode theme = ThemeMode::System;
    int uiScalePercent = 0; // 0 follows the OS scale
    int newWidth = 800;
    int newHeight = 600;
    int jpegQuality = 90; // 1-100, chosen in the JPEG save dialog and reused by Save
    int historyLimitMb = 512; // undo history memory; the oldest steps are dropped beyond it
    std::string language; // pack code; empty is English
    FontChoice font = FontChoice::Classic;
    int fontSize = kDefaultFontSize; // pixels at 100% interface scale
    std::string customFontPath;      // UTF-8; used when font is Custom

    // View
    bool pixelGrid = true;
    PatternStyle backdropPattern = PatternStyle::Solid;
    PatternStyle transparencyPattern = PatternStyle::Checker;
    int patternSpacing = kDefaultPatternSpacing;
    bool showRulers = true;
    bool showGuides = true;
    bool snapToGuides = true;
    bool showToolsPanel = true;
    bool showStatusBar = true;
    bool showSysStats = true;
    NewDocZoom newDocZoom = NewDocZoom::Fit;

    // Tools. `toolDefaults` is what the app starts with; while restoreLastToolSettings is on it is
    // overwritten with the live tool settings on exit.
    bool restoreLastToolSettings = true;
    ToolId startTool = ToolId::Pencil;
    ToolSettings toolDefaults;

    // Theme: colors the user changed on top of the built-in light and dark palettes
    ThemeOverrides themeOverrides;

    // Keybinds
    Keymap keymap;

    // UI state
    int lastPage = 0; // Settings window category

    static Settings defaults() { return Settings(); }
};

// A missing file leaves `out` at its defaults and succeeds. Invalid or out-of-range fields keep their
// default one by one. Returns false only when an existing file cannot be read.
bool loadSettings(const std::string& path, Settings& out);

// Writes through a temporary file and renames it over the target, so a crash never leaves a half-written
// settings.ini. Failures are logged, never thrown.
bool saveSettings(const std::string& path, const Settings& settings);

} // namespace trace
