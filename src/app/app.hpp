#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <string>

#include "app/fonts.hpp"
#include "app/languages.hpp"
#include "app/mode.hpp"
#include "app/native_dialogs.hpp"
#include "app/paint_workspace.hpp"
#include "app/pixel_workspace.hpp"
#include "app/placeholder_workspace.hpp"
#include "app/settings.hpp"
#include "app/settings_window.hpp"
#include "app/theme.hpp"
#include "app/toasts.hpp"
#include "app/workspace.hpp"
#include "platform/sysstats.hpp"

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

namespace trace {

// The shell around the workspaces: the SDL window and ImGui backend, settings, appearance, the main menu bar
// with the mode switcher, the status bar, and the global shortcuts. Everything about a document lives in a
// Workspace; the App shows the active one and forwards the generic File, Edit and View commands to it.
class App {
public:
    App();
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
    void drawModeSwitcher();
    void drawStatusBar();
    void handleShortcuts();
    void applyAppearance();
    void applyFont();
    void applyLanguage();
    void onNativeDialog(const NativeDialogs::Result& result);
    void saveSettingsNow();

    Workspace& workspace() noexcept { return *workspaces_[static_cast<std::size_t>(activeMode_)]; }
    Workspace& workspace(Mode mode) noexcept { return *workspaces_[static_cast<std::size_t>(mode)]; }
    const Workspace& workspace() const noexcept { return *workspaces_[static_cast<std::size_t>(activeMode_)]; }

    // Whether the mode can change right now: not in the middle of a gesture, a modal or a file dialog.
    bool canSwitchMode() noexcept;
    // Shows another workspace. Refused (false) while canSwitchMode() is false unless `force`.
    bool switchMode(Mode target, bool force = false);
    // Opens a file in the workspace it belongs to, switching to it. Returns false when the active workspace
    // should open it itself.
    bool routeOpen(const std::filesystem::path& path);

    // Quitting walks the workspaces that have unsaved changes one by one (the active one first), switches to
    // each and lets its own "Save changes?" prompt settle it. Cancelling any prompt abandons the exit.
    void beginExit();
    void tickExit();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool sdlInitialized_ = false;
    bool imguiContext_ = false;
    bool sdlBackend_ = false;
    bool rendererBackend_ = false;

    bool running_ = false;
    int framesToRender_ = 3;

    std::string windowTitle_;
    std::string iniPath_; // ImGui keeps a pointer to this, so it must outlive the context

    Settings settings_;
    bool settingsDirty_ = false;
    SettingsWindow settingsWindow_;
    Toasts toasts_;
    NativeDialogs dialogs_;

    WorkspaceContext workspaceContext_;
    PaintWorkspace paint_;
    PlaceholderWorkspace flow_;
    PixelWorkspace pixel_;
    std::array<Workspace*, kModeCount> workspaces_; // indexed by Mode
    Mode activeMode_ = Mode::Paint;

    bool exitPending_ = false;  // walking the workspaces with unsaved changes
    bool exitApproved_ = false; // every prompt is settled: leave
    int exitStep_ = 0;          // how many workspaces of the walk have been visited
    Mode exitFirst_ = Mode::Paint;
    Workspace* exitCurrent_ = nullptr; // the workspace whose prompt is showing

    Fonts fonts_;
    LanguageManager languages_;
    std::string appliedLanguage_;
    bool languageApplied_ = false;
    std::string settingsPath_; // empty when there is no per-user data directory
    bool settingsReady_ = false; // loaded and applied, so exiting may save
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
