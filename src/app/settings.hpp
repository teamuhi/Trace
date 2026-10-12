#pragma once

#include <string>

#include "app/fonts.hpp"
#include "app/mode.hpp"
#include "app/theme.hpp"
#include "core/pattern.hpp"
#include "core/keymap.hpp"
#include "tools/tool.hpp"

namespace trace {

inline constexpr int kMinCanvasSize = 1;
inline constexpr int kMaxCanvasSize = 16384;
inline constexpr int kMinUiScalePercent = 75;
inline constexpr int kMaxUiScalePercent = 250;
inline constexpr int kSettingsPageCount = 8;
inline constexpr int kMinHistoryLimitMb = 16;
inline constexpr int kMaxHistoryLimitMb = 4096;
inline constexpr int kMinPixelGridZoomPercent = 100;
inline constexpr int kMaxPixelGridZoomPercent = 3200;

// The Pixel Art workspace's own settings ([pixel] in settings.ini).
struct PixelSettings {
    int newWidth = 64;   // File > New in Pixel Art mode; 1..kMaxPixelCanvasSide
    int newHeight = 64;
    int gridZoomPercent = 400; // the pixel grid shows from this zoom
    bool pixelPerfectDefault = true; // pixel-perfect drawing is on when the app starts
    bool showToolsPanel = true;
    bool showLayersPanel = true;
    // Colors, sizes and options of the pixel tools, kept as the user left them. Antialiasing is never on here.
    ToolSettings tools = defaultTools();

    static ToolSettings defaultTools() {
        ToolSettings t;
        t.brushSize = 1;
        t.strokeWidth = 1;
        t.antialias = false;
        t.eraseToTransparent = true;
        t.pixelPerfect = true;
        return t;
    }
};

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

    // Pixel Art
    PixelSettings pixel;

    // Theme: colors the user changed on top of the built-in light and dark palettes
    ThemeOverrides themeOverrides;

    // Keybinds
    Keymap keymap;

    // UI state
    int lastPage = 0; // Settings window category
    Mode mode = Mode::Paint; // the workspace shown, restored at the next start

    static Settings defaults() { return Settings(); }
};

// A missing file leaves `out` at its defaults and succeeds. Invalid or out-of-range fields keep their
// default one by one. Returns false only when an existing file cannot be read.
bool loadSettings(const std::string& path, Settings& out);

// Writes through a temporary file and renames it over the target, so a crash never leaves a half-written
// settings.ini. Failures are logged, never thrown.
bool saveSettings(const std::string& path, const Settings& settings);

} // namespace trace
