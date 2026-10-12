#include "app/settings.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>

#include "app/theme_io.hpp"
#include "core/color_text.hpp"
#include "core/i18n.hpp"
#include "core/ini.hpp"
#include "tools/toolbox.hpp"

namespace trace {

namespace {

namespace fs = std::filesystem;

// The path is UTF-8 (from SDL_GetPrefPath); std::string would be read as the ANSI code page on Windows.
fs::path toPath(const std::string& utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

constexpr std::array<const char*, 3> kThemeNames = {"system", "light", "dark"};
constexpr std::array<const char*, 2> kNewDocZoomNames = {"fit", "actual"};
constexpr const std::array<const char*, kPatternStyleCount>& kPatternNames = kPatternStyleKeys;
constexpr const std::array<const char*, kFontChoiceCount>& kFontNames = kFontChoiceKeys;
constexpr std::array<const char*, 3> kShapeModeNames = {"outline", "filled", "both"};

void readInt(const IniFile& ini, const char* section, const char* key, int& target, int lo, int hi) {
    if (const std::string* value = ini.get(section, key)) {
        if (const auto parsed = parseInt(*value, lo, hi))
            target = *parsed;
    }
}

void readBool(const IniFile& ini, const char* section, const char* key, bool& target) {
    if (const std::string* value = ini.get(section, key)) {
        if (const auto parsed = parseBool(*value))
            target = *parsed;
    }
}

// Enums are stored by name; an unknown name leaves the default.
template <class E, std::size_t N>
void readEnum(const IniFile& ini, const char* section, const char* key, E& target,
              const std::array<const char*, N>& names) {
    const std::string* value = ini.get(section, key);
    if (!value)
        return;
    for (std::size_t i = 0; i < N; ++i) {
        if (equalsIgnoreCase(*value, names[i])) {
            target = static_cast<E>(i);
            return;
        }
    }
}

void readToolId(const IniFile& ini, const char* key, ToolId& target) {
    const std::string* value = ini.get("tools", key);
    if (!value)
        return;
    for (int i = 0; i < kToolCount; ++i) {
        if (equalsIgnoreCase(*value, Toolbox::name(static_cast<ToolId>(i)))) {
            target = static_cast<ToolId>(i);
            return;
        }
    }
}

void readColor(const IniFile& ini, const char* key, Rgba8& target) {
    if (const std::string* value = ini.get("tools", key)) {
        if (const auto parsed = parseColor(*value))
            target = *parsed;
    }
}

void setBool(IniFile& ini, const char* section, const char* key, bool value) {
    ini.set(section, key, value ? "true" : "false");
}

} // namespace

bool loadSettings(const std::string& path, Settings& out) {
    out = Settings::defaults();
    if (path.empty())
        return true;

    std::error_code ec;
    const fs::path file = toPath(path);
    if (!fs::exists(file, ec))
        return true; // first run
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        SDL_Log("Cannot read settings file %s", path.c_str());
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const IniFile ini = IniFile::parse(text);

    readEnum(ini, "general", "theme", out.theme, kThemeNames);
    readInt(ini, "general", "ui_scale", out.uiScalePercent, 0, kMaxUiScalePercent);
    if (out.uiScalePercent != 0 && out.uiScalePercent < kMinUiScalePercent)
        out.uiScalePercent = Settings::defaults().uiScalePercent;
    readInt(ini, "general", "new_width", out.newWidth, kMinCanvasSize, kMaxCanvasSize);
    readInt(ini, "general", "new_height", out.newHeight, kMinCanvasSize, kMaxCanvasSize);
    readInt(ini, "general", "jpeg_quality", out.jpegQuality, 1, 100);
    readInt(ini, "general", "history_limit_mb", out.historyLimitMb, kMinHistoryLimitMb, kMaxHistoryLimitMb);
    if (const std::string* language = ini.get("general", "language")) {
        if (language->empty() || isValidLanguageCode(*language))
            out.language = *language;
    }
    readEnum(ini, "general", "font", out.font, kFontNames);
    readInt(ini, "general", "font_size", out.fontSize, kMinFontSize, kMaxFontSize);
    if (const std::string* fontPath = ini.get("general", "font_path"))
        out.customFontPath = *fontPath;

    readBool(ini, "view", "pixel_grid", out.pixelGrid);
    readEnum(ini, "view", "backdrop_pattern", out.backdropPattern, kPatternNames);
    readEnum(ini, "view", "transparency_pattern", out.transparencyPattern, kPatternNames);
    readInt(ini, "view", "pattern_spacing", out.patternSpacing, kMinPatternSpacing, kMaxPatternSpacing);
    readBool(ini, "view", "rulers", out.showRulers);
    readBool(ini, "view", "guides", out.showGuides);
    readBool(ini, "view", "snap_to_guides", out.snapToGuides);
    readBool(ini, "view", "tools_panel", out.showToolsPanel);
    readBool(ini, "view", "status_bar", out.showStatusBar);
    readBool(ini, "view", "sys_stats", out.showSysStats);
    readEnum(ini, "view", "new_doc_zoom", out.newDocZoom, kNewDocZoomNames);

    readBool(ini, "tools", "restore_last", out.restoreLastToolSettings);
    readToolId(ini, "start_tool", out.startTool);
    ToolSettings& t = out.toolDefaults;
    readColor(ini, "primary", t.primary);
    readColor(ini, "secondary", t.secondary);
    readInt(ini, "tools", "brush_size", t.brushSize, ToolSettings::kMinSize, ToolSettings::kMaxSize);
    readInt(ini, "tools", "stroke_width", t.strokeWidth, ToolSettings::kMinSize, ToolSettings::kMaxStrokeWidth);
    readBool(ini, "tools", "antialias", t.antialias);
    readBool(ini, "tools", "erase_to_transparent", t.eraseToTransparent);
    readEnum(ini, "tools", "shape_mode", t.shapeMode, kShapeModeNames);
    readInt(ini, "tools", "fill_tolerance", t.fillTolerance, 0, 255);

    PixelSettings& px = out.pixel;
    readInt(ini, "pixel", "new_width", px.newWidth, 1, kMaxPixelCanvasSide);
    readInt(ini, "pixel", "new_height", px.newHeight, 1, kMaxPixelCanvasSide);
    readInt(ini, "pixel", "grid_zoom", px.gridZoomPercent, kMinPixelGridZoomPercent, kMaxPixelGridZoomPercent);
    readBool(ini, "pixel", "pixel_perfect", px.pixelPerfectDefault);
    readBool(ini, "pixel", "tools_panel", px.showToolsPanel);
    readBool(ini, "pixel", "layers_panel", px.showLayersPanel);
    ToolSettings& pt = px.tools;
    if (const std::string* value = ini.get("pixel", "primary")) {
        if (const auto parsed = parseColor(*value))
            pt.primary = *parsed;
    }
    if (const std::string* value = ini.get("pixel", "secondary")) {
        if (const auto parsed = parseColor(*value))
            pt.secondary = *parsed;
    }
    readInt(ini, "pixel", "brush_size", pt.brushSize, ToolSettings::kMinSize, ToolSettings::kMaxPixelSize);
    readInt(ini, "pixel", "stroke_width", pt.strokeWidth, ToolSettings::kMinSize, ToolSettings::kMaxPixelSize);
    readBool(ini, "pixel", "square_brush", pt.squareBrush);
    readEnum(ini, "pixel", "shape_mode", pt.shapeMode, kShapeModeNames);
    readInt(ini, "pixel", "fill_tolerance", pt.fillTolerance, 0, 255);
    readBool(ini, "pixel", "fill_global", pt.fillGlobal);
    pt.pixelPerfect = px.pixelPerfectDefault;
    pt.antialias = false;
    pt.eraseToTransparent = true;

    out.keymap.load(ini);

    readThemeOverrides(ini, out.themeOverrides);

    readInt(ini, "ui", "last_page", out.lastPage, 0, kSettingsPageCount - 1);
    if (const std::string* mode = ini.get("ui", "mode")) {
        if (const auto parsed = modeFromKey(*mode))
            out.mode = *parsed;
    }
    return true;
}

bool saveSettings(const std::string& path, const Settings& s) {
    if (path.empty())
        return false;

    IniFile ini;
    ini.set("general", "theme", kThemeNames[static_cast<std::size_t>(s.theme)]);
    ini.set("general", "ui_scale", std::to_string(s.uiScalePercent));
    ini.set("general", "new_width", std::to_string(s.newWidth));
    ini.set("general", "new_height", std::to_string(s.newHeight));
    ini.set("general", "jpeg_quality", std::to_string(s.jpegQuality));
    ini.set("general", "history_limit_mb", std::to_string(s.historyLimitMb));
    if (!s.language.empty())
        ini.set("general", "language", s.language);
    ini.set("general", "font", kFontNames[static_cast<std::size_t>(s.font)]);
    ini.set("general", "font_size", std::to_string(s.fontSize));
    if (!s.customFontPath.empty())
        ini.set("general", "font_path", s.customFontPath);

    setBool(ini, "view", "pixel_grid", s.pixelGrid);
    ini.set("view", "backdrop_pattern", kPatternNames[static_cast<std::size_t>(s.backdropPattern)]);
    ini.set("view", "transparency_pattern", kPatternNames[static_cast<std::size_t>(s.transparencyPattern)]);
    ini.set("view", "pattern_spacing", std::to_string(s.patternSpacing));
    setBool(ini, "view", "rulers", s.showRulers);
    setBool(ini, "view", "guides", s.showGuides);
    setBool(ini, "view", "snap_to_guides", s.snapToGuides);
    setBool(ini, "view", "tools_panel", s.showToolsPanel);
    setBool(ini, "view", "status_bar", s.showStatusBar);
    setBool(ini, "view", "sys_stats", s.showSysStats);
    ini.set("view", "new_doc_zoom", kNewDocZoomNames[static_cast<std::size_t>(s.newDocZoom)]);

    const ToolSettings& t = s.toolDefaults;
    setBool(ini, "tools", "restore_last", s.restoreLastToolSettings);
    ini.set("tools", "start_tool", Toolbox::name(s.startTool));
    ini.set("tools", "primary", formatColor(t.primary));
    ini.set("tools", "secondary", formatColor(t.secondary));
    ini.set("tools", "brush_size", std::to_string(t.brushSize));
    ini.set("tools", "stroke_width", std::to_string(t.strokeWidth));
    setBool(ini, "tools", "antialias", t.antialias);
    setBool(ini, "tools", "erase_to_transparent", t.eraseToTransparent);
    ini.set("tools", "shape_mode", kShapeModeNames[static_cast<std::size_t>(t.shapeMode)]);
    ini.set("tools", "fill_tolerance", std::to_string(t.fillTolerance));

    const PixelSettings& px = s.pixel;
    ini.set("pixel", "new_width", std::to_string(px.newWidth));
    ini.set("pixel", "new_height", std::to_string(px.newHeight));
    ini.set("pixel", "grid_zoom", std::to_string(px.gridZoomPercent));
    setBool(ini, "pixel", "pixel_perfect", px.pixelPerfectDefault);
    setBool(ini, "pixel", "tools_panel", px.showToolsPanel);
    setBool(ini, "pixel", "layers_panel", px.showLayersPanel);
    ini.set("pixel", "primary", formatColor(px.tools.primary));
    ini.set("pixel", "secondary", formatColor(px.tools.secondary));
    ini.set("pixel", "brush_size", std::to_string(px.tools.brushSize));
    ini.set("pixel", "stroke_width", std::to_string(px.tools.strokeWidth));
    setBool(ini, "pixel", "square_brush", px.tools.squareBrush);
    ini.set("pixel", "shape_mode", kShapeModeNames[static_cast<std::size_t>(px.tools.shapeMode)]);
    ini.set("pixel", "fill_tolerance", std::to_string(px.tools.fillTolerance));
    setBool(ini, "pixel", "fill_global", px.tools.fillGlobal);

    s.keymap.save(ini);

    writeThemeOverrides(ini, s.themeOverrides);

    ini.set("ui", "last_page", std::to_string(s.lastPage));
    ini.set("ui", "mode", modeKey(s.mode));

    const fs::path target = toPath(path);
    fs::path temp = target;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << ini.serialize();
        out.flush();
        if (!out) {
            SDL_Log("Cannot write settings file %s", path.c_str());
            return false;
        }
    }

    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        SDL_Log("Cannot replace settings file %s: %s", path.c_str(), ec.message().c_str());
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

} // namespace trace
