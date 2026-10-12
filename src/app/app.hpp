#pragma once

#include <chrono>
#include <string>

#include "app/canvas_view.hpp"
#include "app/file_session.hpp"
#include "app/fonts.hpp"
#include "app/image_dialogs.hpp"
#include "app/languages.hpp"
#include "app/native_dialogs.hpp"
#include "app/settings.hpp"
#include "app/settings_window.hpp"
#include "app/toasts.hpp"
#include "app/theme.hpp"
#include "core/document.hpp"
#include "platform/sysstats.hpp"
#include "tools/toolbox.hpp"

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

namespace trace {

class App {
public:
    App() = default;
    ~App() { shutdown(); }

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init();
    void run();
    void shutdown();

private:
    using Clock = std::chrono::steady_clock;

    void handleEvent(const SDL_Event& event);
    void updateStats();
    void renderFrame();
    void drawUi();
    void drawMenuBar();
    void drawStatusBar();
    void handleShortcuts();
    void copySelection(bool cut);
    void pasteClipboard();
    void deleteSelection();
    void selectAll();
    void cropToSelection();
    Rgba8 clearColor() const;
    void applyAppearance();
    void applyFont();
    void applyLanguage();
    void onNativeDialog(const NativeDialogs::Result& result);
    void saveSettingsNow();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool sdlInitialized_ = false;
    bool imguiContext_ = false;
    bool sdlBackend_ = false;
    bool rendererBackend_ = false;

    bool running_ = false;
    int framesToRender_ = 3;

    Document document_;
    CanvasView canvas_;
    Toolbox toolbox_;
    std::string windowTitle_;
    std::string iniPath_; // ImGui keeps a pointer to this, so it must outlive the context

    Settings settings_;
    FileSession files_{document_, settings_, {}};
    SettingsWindow settingsWindow_;
    ImageDialogs imageDialogs_;
    Toasts toasts_;
    NativeDialogs dialogs_;
    Fonts fonts_;
    LanguageManager languages_;
    std::string appliedLanguage_;
    bool languageApplied_ = false;
    std::string settingsPath_; // empty when there is no per-user data directory
    bool settingsReady_ = false; // loaded and applied, so exiting may save
    bool settingsDirty_ = false;
    float displayScale_ = 1.0f;
    int appliedUiScale_ = -1;
    ThemeMode appliedTheme_ = ThemeMode::System;
    ThemeOverrides appliedOverrides_;
    int appliedFontSize_ = -1;
    FontChoice appliedFont_ = FontChoice::Classic;
    std::string appliedFontPath_;
    bool fontApplied_ = false;

    SysStatsSampler sampler_;
    Clock::time_point lastStatsTime_ = Clock::now();
    std::string statsText_ = "CPU 0.0% | RAM 0.0 MB";
};

} // namespace trace
