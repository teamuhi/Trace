#include "app/app.hpp"

#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>

#include "app/keybinds.hpp"
#include "app/theme.hpp"
#include "app/theme_io.hpp"
#include "core/i18n.hpp"
#include "io/path_utf8.hpp"
#include "io/project_io.hpp"

namespace trace {

namespace {

constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 800;
constexpr int kIdleTimeoutMs = 1000; // also bounds the CPU/RAM refresh latency while idle
constexpr int kFramesPerEvent = 3;   // ImGui needs a few frames to settle after input
constexpr auto kStatsInterval = std::chrono::seconds(1);
constexpr float kBaseFontSize = 14.0f;

} // namespace

App::App()
    : workspaceContext_{settings_, settingsDirty_, toasts_, dialogs_,
                        [this](const std::filesystem::path& path) { return routeOpen(path); }},
      paint_(workspaceContext_),
      flow_(workspaceContext_, Mode::Flowchart),
      pixel_(workspaceContext_),
      workspaces_{&paint_, &flow_, &pixel_} {}

bool App::init() {
    SDL_SetAppMetadata("Trace", TRACE_VERSION, "com.trace.paint");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    sdlInitialized_ = true;

    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (!(scale > 0.0f))
        scale = 1.0f;

    window_ = SDL_CreateWindow("Trace", static_cast<int>(kInitialWidth * scale),
                               static_cast<int>(kInitialHeight * scale),
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window_, 640, 400);

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }
    if (!SDL_SetRenderVSync(renderer_, 1))
        SDL_Log("SDL_SetRenderVSync failed: %s", SDL_GetError());

    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext()) {
        SDL_Log("ImGui::CreateContext failed");
        return false;
    }
    imguiContext_ = true;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

    // Keep the window layout and settings in the per-user data directory instead of the working directory.
    if (char* prefPath = SDL_GetPrefPath("Trace", "Trace")) {
        iniPath_ = std::string(prefPath) + "imgui.ini";
        settingsPath_ = std::string(prefPath) + "settings.ini";
        SDL_free(prefPath);
        io.IniFilename = iniPath_.c_str();
    } else {
        io.IniFilename = nullptr;
    }
    loadSettings(settingsPath_, settings_); // falls back to defaults field by field
    activeMode_ = settings_.mode;

    fonts_.init();

    displayScale_ = scale;
    setUiScale(scale);
    applyAppearance();

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_)) {
        SDL_Log("ImGui_ImplSDL3_InitForSDLRenderer failed");
        return false;
    }
    sdlBackend_ = true;

    if (!ImGui_ImplSDLRenderer3_Init(renderer_)) {
        SDL_Log("ImGui_ImplSDLRenderer3_Init failed");
        return false;
    }
    rendererBackend_ = true;

    dialogs_.init(window_);
    {
        const char* base = SDL_GetBasePath();
        constexpr std::size_t kSettingsFileNameLength = sizeof("settings.ini") - 1;
        languages_.init(settingsPath_.size() < kSettingsFileNameLength
                            ? std::string()
                            : settingsPath_.substr(0, settingsPath_.size() - kSettingsFileNameLength) + "languages",
                        base ? std::string(base) + "languages" : std::string());
    }
    applyLanguage();
    applyFont();
    for (Workspace* ws : workspaces_)
        ws->init(window_, renderer_); // Paint starts from a blank canvas
    settingsReady_ = true;

    running_ = true;
    framesToRender_ = kFramesPerEvent;
    lastStatsTime_ = Clock::now();
    return true;
}

void App::run() {
    while (running_) {
        SDL_Event event;
        const bool gotEvent =
            framesToRender_ > 0 ? SDL_PollEvent(&event) : SDL_WaitEventTimeout(&event, kIdleTimeoutMs);
        if (gotEvent) {
            do {
                handleEvent(event);
            } while (SDL_PollEvent(&event));
        }
        if (!running_ || exitApproved_)
            break;

        updateStats();

        if (framesToRender_ > 0) {
            renderFrame();
            --framesToRender_;
        }
        if (toasts_.active() && framesToRender_ < 1)
            framesToRender_ = 1; // keep drawing until the last toast has faded out
        if (exitApproved_)
            running_ = false;
    }
}

void App::shutdown() {
    if (settingsReady_ && settings_.restoreLastToolSettings)
        settings_.toolDefaults = paint_.toolbox().settings();
    saveSettingsNow();
    settingsReady_ = false;
    if (rendererBackend_) {
        ImGui_ImplSDLRenderer3_Shutdown();
        rendererBackend_ = false;
    }
    if (sdlBackend_) {
        ImGui_ImplSDL3_Shutdown();
        sdlBackend_ = false;
    }
    if (imguiContext_) {
        ImGui::DestroyContext();
        imguiContext_ = false;
    }
    for (Workspace* ws : workspaces_)
        ws->release();
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (sdlInitialized_) {
        SDL_Quit();
        sdlInitialized_ = false;
    }
    running_ = false;
}

void App::handleEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    framesToRender_ = kFramesPerEvent;

    NativeDialogs::Result dialogResult;
    if (dialogs_.handleEvent(event, dialogResult)) {
        onNativeDialog(dialogResult);
        return;
    }
    if (workspace().fileSession().handleEvent(event))
        return; // a file dialog result or a dropped file

    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        beginExit(); // asks about unsaved changes first; the loop stops once every prompt is settled
        break;
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
        handleSystemThemeChanged();
        break;
    default:
        break;
    }
}

void App::updateStats() {
    if (!settings_.showStatusBar || !settings_.showSysStats)
        return; // not shown, so do not sample or wake the render loop
    const auto now = Clock::now();
    if (now - lastStatsTime_ < kStatsInterval)
        return;
    lastStatsTime_ = now;

    const SysStats stats = sampler_.sample();
    char cpu[24];
    char ram[24];
    std::snprintf(cpu, sizeof(cpu), "%.1f", static_cast<double>(stats.cpuPercent));
    std::snprintf(ram, sizeof(ram), "%.1f", static_cast<double>(stats.rssBytes) / (1024.0 * 1024.0));
    statsText_ = trf(Str::StatusStats, {cpu, ram});
    if (framesToRender_ < 1)
        framesToRender_ = 1;
}

void App::renderFrame() {
    // Theme and interface scale restyle everything, so they are applied between frames, never mid-frame.
    if (settings_.uiScalePercent != appliedUiScale_ || settings_.theme != appliedTheme_ ||
        settings_.fontSize != appliedFontSize_ || !(settings_.themeOverrides == appliedOverrides_))
        applyAppearance();
    if (!languageApplied_ || settings_.language != appliedLanguage_)
        applyLanguage();
    if (!fontApplied_ || settings_.font != appliedFont_ || settings_.customFontPath != appliedFontPath_)
        applyFont();

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    drawUi();

    // Persist changes once the user lets go of the mouse, so dragging a slider does not write every frame.
    if (settingsDirty_ && settingsReady_ && !ImGui::IsAnyMouseDown() && !ImGui::IsAnyItemActive())
        saveSettingsNow();

    ImGui::Render();

    const ImGuiIO& io = ImGui::GetIO();
    if (!SDL_SetRenderScale(renderer_, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y))
        SDL_Log("SDL_SetRenderScale failed: %s", SDL_GetError());

    const ImVec4& clear = tokens().windowBg;
    SDL_SetRenderDrawColorFloat(renderer_, clear.x, clear.y, clear.z, clear.w);
    if (!SDL_RenderClear(renderer_))
        SDL_Log("SDL_RenderClear failed: %s", SDL_GetError());

    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    if (!SDL_RenderPresent(renderer_))
        SDL_Log("SDL_RenderPresent failed: %s", SDL_GetError());
}

void App::drawUi() {
    drawMenuBar();
    if (settings_.showStatusBar)
        drawStatusBar(); // must precede the dockspace so it reserves its work-area strip first

    // Only the active mode dockspace is submitted; the other modes keep their layout in the ImGui settings.
    // The mode may just have changed in the menu bar, so look it up now.
    Workspace& ws = workspace();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockId = ws.dockSpaceId();
    const bool buildLayout = ImGui::DockBuilderGetNode(dockId) == nullptr || ws.layoutStale(dockId); // no usable saved layout
    ImGui::DockSpaceOverViewport(dockId, viewport);
    if (buildLayout)
        ws.buildDefaultLayout(dockId, viewport->WorkSize);

    ws.drawWindows();

    settingsDirty_ |= settingsWindow_.draw(settings_, paint_.toolbox(), settingsPath_, dialogs_, languages_);
    ws.drawDialogs();
    toasts_.draw();

    const std::string title = ws.fileSession().hasDocument()
                                  ? ws.fileSession().windowTitle()
                                  : std::string(tr(modeString(activeMode_))) + " - Trace";
    if (title != windowTitle_) {
        windowTitle_ = title;
        SDL_SetWindowTitle(window_, windowTitle_.c_str());
    }

    handleShortcuts();
    tickExit();
}

void App::handleShortcuts() {
    Workspace& ws = workspace();
    if (ws.busy() || dialogs_.busy())
        return; // a dialog or prompt is open: shortcuts must not act behind it

    const Keymap& keys = settings_.keymap;
    const ModeContext context = modeContext(activeMode_);
    // Only actions that are live in the active mode can fire, so one chord never acts twice.
    const auto fire = [&](Action action, bool repeat = false) { return triggeredIn(keys, action, context, repeat); };

    for (const Mode mode : kAllModes) {
        if (fire(modeAction(mode))) {
            switchMode(mode);
            return; // this frame keys were meant for the old mode
        }
    }

    // File and history shortcuts wait until the current gesture is finished.
    if (!ws.gestureActive()) {
        FileSession& files = ws.fileSession();
        if (fire(Action::NewCanvas))
            files.requestNew();
        if (fire(Action::OpenFile))
            files.requestOpen();
        if (fire(Action::SaveFile))
            files.requestSave();
        if (fire(Action::SaveFileAs))
            files.requestSaveAs();
        if (fire(Action::Export) && ws.canExport())
            ws.requestExport();
        if (fire(Action::Undo))
            ws.undo();
        if (fire(Action::Redo))
            ws.redo();
        if (fire(Action::DeleteSelection))
            ws.deleteSelection();
        // Ctrl+C, V, X and A belong to a focused text field (a hex color, a number) when there is one.
        if (!ImGui::GetIO().WantTextInput) {
            if (fire(Action::Cut))
                ws.cut();
            if (fire(Action::Copy))
                ws.copy();
            if (fire(Action::Paste))
                ws.paste();
            if (fire(Action::SelectAll))
                ws.selectAll();
        }
    }
    if (fire(Action::OpenSettings))
        settingsWindow_.open();
    if (fire(Action::ZoomIn))
        ws.zoomIn();
    if (fire(Action::ZoomOut))
        ws.zoomOut();
    if (fire(Action::FitToWindow))
        ws.fit();
    if (fire(Action::ActualSize))
        ws.actualSize();

    ws.handleShortcuts(keys);
}

// --- modes and exit ----------------------------------------------------------------------------

bool App::canSwitchMode() noexcept {
    return workspace().canLeave() && !dialogs_.busy();
}

bool App::switchMode(Mode target, bool force) {
    if (target == activeMode_)
        return true;
    if (!force && !canSwitchMode())
        return false;
    activeMode_ = target;
    settings_.mode = target;
    settingsDirty_ = true;
    framesToRender_ = std::max(framesToRender_, kFramesPerEvent); // a first-time dock layout needs a few frames
    return true;
}

bool App::routeOpen(const std::filesystem::path& path) {
    Mode target = Mode::Paint; // anything that is not a project is an image
    if (const std::optional<std::uint32_t> kind = sniffProjectKind(path)) {
        FileSession& session = workspace().fileSession();
        if (*kind == kProjectFlow) {
            target = Mode::Flowchart;
        } else if (*kind == kProjectPixel) {
            target = Mode::PixelArt;
        } else {
            session.report(trf(Str::ErrProjectUnknown, {utf8FromPath(path.filename())}), true);
            return true;
        }
        if (!workspace(target).opensProjects()) {
            session.report(trf(Str::ErrModeNotReady, {tr(modeString(target))}), true);
            return true;
        }
    }
    if (target == activeMode_)
        return false; // the active workspace opens it
    if (!switchMode(target))
        return true; // cannot leave right now; the request is dropped like any request made while busy
    workspace(target).fileSession().requestOpenPath(path);
    return true;
}

void App::beginExit() {
    if (exitPending_ || workspace().busy())
        return;
    exitPending_ = true;
    exitStep_ = 0;
    exitFirst_ = activeMode_;
    exitCurrent_ = nullptr;
    tickExit();
}

void App::tickExit() {
    if (!exitPending_)
        return;

    if (exitCurrent_) {
        FileSession& session = exitCurrent_->fileSession();
        if (session.busy())
            return; // its prompt, or the save it started, is still going
        const bool approved = session.exitApproved();
        session.cancelExit();
        exitCurrent_ = nullptr;
        if (!approved) { // cancelled (or the save failed): stay
            exitPending_ = false;
            return;
        }
    }

    while (exitStep_ < kModeCount) {
        const Mode mode = static_cast<Mode>((static_cast<int>(exitFirst_) + exitStep_++) % kModeCount);
        Workspace& ws = workspace(mode);
        if (!ws.fileSession().hasUnsavedChanges())
            continue;
        switchMode(mode, true); // show what is being asked about
        exitCurrent_ = &ws;
        ws.fileSession().requestExit();
        return;
    }
    exitPending_ = false;
    exitApproved_ = true;
}

void App::applyAppearance() {
    // Auto (0) keeps the OS content scale; an explicit percentage is absolute, so divide the OS scale out.
    const float user = settings_.uiScalePercent == 0
                           ? 1.0f
                           : static_cast<float>(settings_.uiScalePercent) / 100.0f / displayScale_;
    setUserScale(user);
    setFontPixels(static_cast<float>(settings_.fontSize));
    appliedFontSize_ = settings_.fontSize;
    setThemeOverrides(settings_.themeOverrides);
    appliedOverrides_ = settings_.themeOverrides;
    setThemeMode(settings_.theme);
    appliedUiScale_ = settings_.uiScalePercent;
    appliedTheme_ = settings_.theme;
}

// Chooses the interface font. A custom font that cannot be used falls back to Inter and is forgotten, so the
// same broken file is not retried every frame.
void App::applyFont() {
    std::string error;
    FontChoice used = settings_.font;
    fonts_.apply(settings_.font, settings_.customFontPath, error, used);
    if (!error.empty()) {
        toasts_.push(error, Toasts::Kind::Error);
        settings_.customFontPath.clear();
        settingsDirty_ = true;
    }
    if (used != settings_.font) {
        settings_.font = used;
        settingsDirty_ = true;
    }
    appliedFont_ = settings_.font;
    appliedFontPath_ = settings_.customFontPath;
    fontApplied_ = true;
}

// Switches the interface language. A pack that cannot be loaded falls back to English and is forgotten.
void App::applyLanguage() {
    const std::string error = languages_.apply(settings_.language);
    if (!error.empty()) {
        toasts_.push(error, Toasts::Kind::Error);
        settings_.language.clear();
        settingsDirty_ = true;
    }
    appliedLanguage_ = settings_.language;
    languageApplied_ = true;
    statsText_.clear(); // rebuilt in the new language at the next sample
    lastStatsTime_ = Clock::time_point{};
}

void App::onNativeDialog(const NativeDialogs::Result& result) {
    if (!result.error.empty()) {
        toasts_.push(result.error, Toasts::Kind::Error);
        return;
    }
    if (result.cancelled || result.path.empty())
        return;
    if (!NativeDialogs::isSettingsTag(result.tag)) {
        workspace().onNativeDialog(result); // an import, export or palette of the active mode
        return;
    }

    switch (result.tag) {
    case NativeDialogs::Tag::FontFile:
        settings_.customFontPath = result.path;
        settings_.font = FontChoice::Custom;
        settingsDirty_ = true; // renderFrame loads it, and reports if it cannot
        break;
    case NativeDialogs::Tag::ThemeImport: {
        ThemeOverrides loaded;
        const std::string error = loadThemeFile(result.path, loaded);
        if (!error.empty()) {
            toasts_.push(error, Toasts::Kind::Error);
        } else {
            settings_.themeOverrides = loaded;
            settingsDirty_ = true;
            toasts_.push(tr(Str::ToastThemeImported));
        }
        break;
    }
    case NativeDialogs::Tag::ThemeExport: {
        const std::string error = saveThemeFile(result.path, settings_.themeOverrides);
        if (!error.empty())
            toasts_.push(error, Toasts::Kind::Error);
        else
            toasts_.push(tr(Str::ToastThemeExported));
        break;
    }
    case NativeDialogs::Tag::LanguageImport: {
        std::string code;
        std::string name;
        const std::string error = languages_.importFile(result.path, code, name);
        if (!error.empty()) {
            toasts_.push(error, Toasts::Kind::Error);
        } else {
            settings_.language = code; // switch to it straight away; applyLanguage runs before the next frame
            settingsDirty_ = true;
            toasts_.push(trf(Str::ToastLanguageImported, {name}));
        }
        break;
    }
    case NativeDialogs::Tag::LanguageTemplate: {
        const std::string error = languages_.exportTemplate(result.path);
        if (!error.empty())
            toasts_.push(error, Toasts::Kind::Error);
        else
            toasts_.push(tr(Str::ToastLanguageTemplate));
        break;
    }
    default:
        break; // the other tags belong to a workspace and were handled above
    }
}

void App::saveSettingsNow() {
    settingsDirty_ = false;
    if (!settingsReady_ || settingsPath_.empty())
        return;
    saveSettings(settingsPath_, settings_);
}

void App::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar())
        return;
    Workspace& ws = workspace();
    const Keymap& keys = settings_.keymap;
    FileSession& files = ws.fileSession();

    if (ImGui::BeginMenu(tr(Str::MenuFile))) {
        const bool document = files.hasDocument();
        if (actionMenuItem(keys, tr(Str::MenuNew), Action::NewCanvas, nullptr, document))
            files.requestNew();
        if (actionMenuItem(keys, tr(Str::MenuOpen), Action::OpenFile))
            files.requestOpen();
        ImGui::Separator();
        if (actionMenuItem(keys, tr(Str::MenuSave), Action::SaveFile, nullptr, document))
            files.requestSave();
        if (actionMenuItem(keys, tr(Str::MenuSaveAs), Action::SaveFileAs, nullptr, document))
            files.requestSaveAs();
        if (actionMenuItem(keys, tr(Str::MenuExport), Action::Export, nullptr, ws.canExport()))
            ws.requestExport();
        ImGui::Separator();
        if (actionMenuItem(keys, tr(Str::MenuSettings), Action::OpenSettings))
            settingsWindow_.open();
        ImGui::Separator();
        if (ImGui::MenuItem(tr(Str::MenuExit)))
            beginExit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr(Str::MenuEdit))) {
        if (actionMenuItem(keys, tr(Str::MenuUndo), Action::Undo, nullptr, ws.canUndo()))
            ws.undo();
        if (actionMenuItem(keys, tr(Str::MenuRedo), Action::Redo, nullptr, ws.canRedo()))
            ws.redo();
        ImGui::Separator();
        if (actionMenuItem(keys, tr(Str::MenuCut), Action::Cut, nullptr, ws.canCut()))
            ws.cut();
        if (actionMenuItem(keys, tr(Str::MenuCopy), Action::Copy, nullptr, ws.canCopy()))
            ws.copy();
        if (actionMenuItem(keys, tr(Str::MenuPaste), Action::Paste, nullptr, ws.canPaste()))
            ws.paste();
        if (actionMenuItem(keys, tr(Str::MenuDelete), Action::DeleteSelection, nullptr, ws.canDeleteSelection()))
            ws.deleteSelection();
        ImGui::Separator();
        if (actionMenuItem(keys, tr(Str::MenuSelectAll), Action::SelectAll, nullptr, ws.canSelectAll()))
            ws.selectAll();
        if (actionMenuItem(keys, tr(Str::MenuDeselect), Action::Deselect, nullptr, ws.canDeselect()))
            ws.deselect();
        ws.drawEditMenuExtras();
        ImGui::EndMenu();
    }

    ws.drawModeMenus();

    if (ImGui::BeginMenu(tr(Str::MenuView))) {
        const bool zoom = ws.canZoom();
        if (actionMenuItem(keys, tr(Str::MenuZoomIn), Action::ZoomIn, nullptr, zoom))
            ws.zoomIn();
        if (actionMenuItem(keys, tr(Str::MenuZoomOut), Action::ZoomOut, nullptr, zoom))
            ws.zoomOut();
        if (actionMenuItem(keys, tr(Str::MenuFitToWindow), Action::FitToWindow, nullptr, zoom))
            ws.fit();
        if (actionMenuItem(keys, tr(Str::MenuActualSize), Action::ActualSize, nullptr, zoom))
            ws.actualSize();
        ImGui::Separator();

        ws.drawViewMenuItems();
        if (ImGui::MenuItem(tr(Str::MenuStatusBar), nullptr, &settings_.showStatusBar))
            settingsDirty_ = true;
        ImGui::Separator();

        if (ImGui::BeginMenu(tr(Str::MenuTheme))) {
            struct Item {
                Str label;
                ThemeMode mode;
            };
            static constexpr Item items[] = {
                {Str::WordSystem, ThemeMode::System},
                {Str::WordLight, ThemeMode::Light},
                {Str::WordDark, ThemeMode::Dark},
            };
            for (const Item& item : items) {
                if (ImGui::MenuItem(tr(item.label), nullptr, settings_.theme == item.mode)) {
                    settings_.theme = item.mode;
                    settingsDirty_ = true;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    drawModeSwitcher();
    ImGui::EndMainMenuBar();
}

// Paint | Flowchart | Pixel Art, right-aligned in the menu bar. The shown mode is filled with the accent color
// (and the text color that reads on it); the others use the button colors, so it follows the theme. While the
// mode cannot change (a gesture, a dialog) it is drawn dimmed and ignores clicks.
void App::drawModeSwitcher() {
    const ThemeTokens& t = tokens();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    const float inset = std::round(style.FramePadding.y * 0.5f);
    const float padX = std::round(ImGui::GetFontSize() * 0.9f);
    const float rounding = std::round((height - 2.0f * inset) * 0.3f);
    const bool enabled = canSwitchMode();
    const float alpha = enabled ? 1.0f : style.DisabledAlpha;

    float widths[kModeCount];
    float total = 0.0f;
    for (const Mode mode : kAllModes) {
        const float w = std::round(ImGui::CalcTextSize(tr(modeString(mode))).x + 2.0f * padX);
        widths[static_cast<int>(mode)] = w;
        total += w;
    }

    const ImVec2 menuEnd = ImGui::GetCursorScreenPos();
    const float rightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - std::round(ImGui::GetFontSize() * 0.5f);
    const float startX = std::max(rightEdge - total, menuEnd.x + style.ItemSpacing.x); // never over the menus
    ImGui::SetCursorScreenPos(ImVec2(startX, menuEnd.y));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto withAlpha = [alpha](ImVec4 c) {
        c.w *= alpha;
        return ImGui::ColorConvertFloat4ToU32(c);
    };
    const ImVec4 onAccent = contrastText(t.accent);
    const ImVec2 first = ImGui::GetCursorScreenPos();
    float x = first.x;

    for (const Mode mode : kAllModes) {
        const int index = static_cast<int>(mode);
        ImGui::PushID(index);
        ImGui::SetCursorScreenPos(ImVec2(x, first.y));
        ImGui::InvisibleButton("##mode", ImVec2(widths[index], height));
        x += widths[index];
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();

        const bool selected = mode == activeMode_;
        ImVec4 fill = t.button;
        ImVec4 text = t.text;
        if (selected) {
            fill = t.accent;
            text = onAccent;
        } else if (enabled && held) {
            fill = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        } else if (enabled && hovered) {
            fill = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        }

        const ImDrawFlags corners = index == 0                  ? ImDrawFlags_RoundCornersLeft
                                    : index == kModeCount - 1 ? ImDrawFlags_RoundCornersRight
                                                                : ImDrawFlags_RoundCornersNone;
        const ImVec2 a(p0.x, p0.y + inset);
        const ImVec2 b(p1.x, p1.y - inset);
        dl->AddRectFilled(a, b, withAlpha(fill), rounding, corners);
        if (index > 0)
            dl->AddLine(ImVec2(a.x, a.y), ImVec2(a.x, b.y), withAlpha(ImGui::GetStyleColorVec4(ImGuiCol_Border)));

        const char* label = tr(modeString(mode));
        const ImVec2 size = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(std::round(p0.x + (widths[index] - size.x) * 0.5f), std::round(p0.y + (height - size.y) * 0.5f)),
                    withAlpha(text), label);

        if (hovered) {
            const std::string shortcut = displayShortcut(settings_.keymap, modeAction(mode));
            const std::string name = label;
            ImGui::SetTooltip("%s", (shortcut.empty() ? trf(Str::ModeSwitchTip, {name})
                                                      : trf(Str::ModeSwitchTipKey, {name, shortcut}))
                                        .c_str());
        }
        if (clicked && enabled)
            switchMode(mode);
        ImGui::PopID();
    }
    dl->AddRect(ImVec2(first.x, first.y + inset), ImVec2(first.x + total, first.y + height - inset),
                withAlpha(ImGui::GetStyleColorVec4(ImGuiCol_Border)), rounding);
}

void App::drawStatusBar() {
    const float height = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, tokens().statusBarBg);
    ImGui::PushStyleColor(ImGuiCol_Text, tokens().text);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginViewportSideBar("##StatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, height, flags)) {
        const std::string left = workspace().statusLeft();
        ImGui::TextUnformatted(left.c_str());

        if (settings_.showSysStats) {
            const float statsWidth = ImGui::CalcTextSize(statsText_.c_str()).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - statsWidth);
            ImGui::TextUnformatted(statsText_.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

} // namespace trace
