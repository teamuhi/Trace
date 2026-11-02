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

#include "app/app_icon.hpp"
#include "app/icon_widgets.hpp"
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

Icon modeIcon(Mode mode) noexcept {
    switch (mode) {
    case Mode::Paint: return Icon::PaintRoller;
    case Mode::Flowchart: return Icon::FlowchartIcon;
    case Mode::PixelArt: return Icon::PixelartIcon;
    }
    return Icon::None;
}

Icon themeIcon(ThemeMode mode) noexcept {
    switch (mode) {
    case ThemeMode::System: return Icon::System;
    case ThemeMode::Light: return Icon::LightTheme;
    case ThemeMode::Dark: return Icon::DarkTheme;
    }
    return Icon::None;
}

} // namespace

App::App()
    : workspaceContext_{settings_, settingsDirty_, toasts_, dialogs_,
                        [this](const std::filesystem::path& path) { return routeOpen(path); }},
      paint_(workspaceContext_),
      flow_(workspaceContext_),
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
    setWindowIcon(window_); // a missing icon only leaves the default one

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
    initSvgIcons(renderer_);
    initAppIcon(renderer_);

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

        // Queued off-screen work needs a rendered frame; a minimized window renders nothing, so it waits for the
        // restore event instead of spinning.
        if (framesToRender_ < 1 && workspace().wantsFrame() && !(SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED))
            framesToRender_ = 1;

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
    releaseSvgIcons();
    releaseAppIcon();
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
        return; // a file dialog result
    if (handleDrop(event))
        return;

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

// Files dragged in from outside. A project file (.tflow, .tpix, .tboard) or any file dropped with Shift held opens as
// a document, as File > Open would (only the first of a drop: the rest would replace it). Pictures go onto the canvas
// of the active mode as movable objects, each further file of a drop offset a little from the last.
bool App::handleDrop(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_DROP_BEGIN:
    case SDL_EVENT_DROP_COMPLETE:
        dropIndex_ = 0;
        dropOpened_ = false;
        dropNoticeShown_ = false;
        return true;
    case SDL_EVENT_DROP_POSITION:
        return true;
    case SDL_EVENT_DROP_FILE:
        break;
    default:
        return false;
    }
    if (!event.drop.data)
        return true;

    const std::filesystem::path path = pathFromUtf8(event.drop.data);
    Workspace& ws = workspace();
    const auto notice = [&](Str text, Toasts::Kind kind) {
        if (!dropNoticeShown_) {
            dropNoticeShown_ = true; // once per drop, however many files came with it
            toasts_.push(tr(text), kind);
        }
    };

    const bool asDocument = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 || sniffProjectKind(path).has_value();
    if (asDocument) {
        if (dropOpened_) {
            notice(Str::ToastDropSkipped, Toasts::Kind::Info);
        } else if (exitPending_ || ws.busy()) {
            notice(Str::ToastDropBusy, Toasts::Kind::Error);
        } else {
            dropOpened_ = true;
            ws.fileSession().requestOpenPath(path);
        }
        return true;
    }

    if (exitPending_ || ws.busy()) {
        notice(Str::ToastDropBusy, Toasts::Kind::Error);
        return true;
    }
    ws.commitPending(); // text still being typed goes into the picture first
    if (ws.gestureActive()) {
        notice(Str::ToastDropBusy, Toasts::Kind::Error);
        return true;
    }
    const ImVec2 base = ImGui::GetMainViewport()->Pos;
    ws.dropImage(path, ImVec2(base.x + event.drop.x, base.y + event.drop.y), dropIndex_++);
    return true;
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
    // Off-screen work the workspace recorded during this frame (it needed the font atlas this frame's draw data
    // just brought up to date), e.g. a Flowchart PNG export. Runs before the present; it restores the renderer.
    workspace().afterRender(renderer_);
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
    const bool resetRequested = ws.takeLayoutReset(); // View > Reset Layout, or the Settings button
    const bool buildLayout = resetRequested || ImGui::DockBuilderGetNode(dockId) == nullptr ||
                             ws.layoutStale(dockId); // asked to start over, or no usable saved layout
    ImGui::DockSpaceOverViewport(dockId, viewport);
    if (buildLayout)
        ws.buildDefaultLayout(dockId, viewport->WorkSize);

    ws.drawWindows();

    settingsDirty_ |= settingsWindow_.draw(settings_, paint_.toolbox(), settingsPath_, dialogs_, languages_);
    if (settingsWindow_.takeResetLayoutRequest()) {
        for (Workspace* each : workspaces_)
            each->resetLayout(); // every mode starts over; each rebuilds when it is next shown
        framesToRender_ = std::max(framesToRender_, kFramesPerEvent);
    }
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

    // Text still being typed is not in the picture yet: a file command must see it (and it is what lets the file
    // shortcuts below run at all, since an open text box counts as a gesture).
    if (fire(Action::NewCanvas) || fire(Action::OpenFile) || fire(Action::SaveFile) || fire(Action::SaveFileAs) ||
        (fire(Action::Export) && ws.canExport()))
        ws.commitPending();

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
    workspace().commitPending(); // what is being typed goes into the picture before the mode is left
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
        } else if (*kind == kProjectBoard) {
            target = Mode::Paint;
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
    workspace().commitPending(); // so the unsaved-changes check sees it
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
        if (actionMenuItem(keys, Icon::File, tr(Str::MenuNew), Action::NewCanvas, nullptr, document))
            files.requestNew();
        ws.drawFileMenuExtras();
        if (actionMenuItem(keys, Icon::Folder, tr(Str::MenuOpen), Action::OpenFile))
            files.requestOpen();
        ImGui::Separator();
        if (actionMenuItem(keys, Icon::Save, tr(Str::MenuSave), Action::SaveFile, nullptr, document))
            files.requestSave();
        if (actionMenuItem(keys, Icon::Save, tr(Str::MenuSaveAs), Action::SaveFileAs, nullptr, document))
            files.requestSaveAs();
        if (actionMenuItem(keys, Icon::Export, tr(Str::MenuExport), Action::Export, nullptr, ws.canExport()))
            ws.requestExport();
        ImGui::Separator();
        if (actionMenuItem(keys, Icon::Settings, tr(Str::MenuSettings), Action::OpenSettings))
            settingsWindow_.open();
        ImGui::Separator();
        if (iconMenuItem(Icon::Exit, tr(Str::MenuExit)))
            beginExit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr(Str::MenuEdit))) {
        if (actionMenuItem(keys, Icon::Undo, tr(Str::MenuUndo), Action::Undo, nullptr, ws.canUndo()))
            ws.undo();
        if (actionMenuItem(keys, Icon::Redo, tr(Str::MenuRedo), Action::Redo, nullptr, ws.canRedo()))
            ws.redo();
        ImGui::Separator();
        if (actionMenuItem(keys, Icon::Cut, tr(Str::MenuCut), Action::Cut, nullptr, ws.canCut()))
            ws.cut();
        if (actionMenuItem(keys, Icon::Duplicate, tr(Str::MenuCopy), Action::Copy, nullptr, ws.canCopy()))
            ws.copy();
        if (actionMenuItem(keys, Icon::Paste, tr(Str::MenuPaste), Action::Paste, nullptr, ws.canPaste()))
            ws.paste();
        if (actionMenuItem(keys, Icon::Delete, tr(Str::MenuDelete), Action::DeleteSelection, nullptr,
                           ws.canDeleteSelection()))
            ws.deleteSelection();
        ImGui::Separator();
        if (actionMenuItem(keys, Icon::Marquee, tr(Str::MenuSelectAll), Action::SelectAll, nullptr, ws.canSelectAll()))
            ws.selectAll();
        if (actionMenuItem(keys, tr(Str::MenuDeselect), Action::Deselect, nullptr, ws.canDeselect()))
            ws.deselect();
        ws.drawEditMenuExtras();
        ImGui::EndMenu();
    }

    ws.drawModeMenus();

    if (ImGui::BeginMenu(tr(Str::MenuView))) {
        const bool zoom = ws.canZoom();
        if (actionMenuItem(keys, Icon::MagnifyingGlassZoomIn, tr(Str::MenuZoomIn), Action::ZoomIn, nullptr, zoom))
            ws.zoomIn();
        if (actionMenuItem(keys, Icon::MagnifyingGlassZoomOut, tr(Str::MenuZoomOut), Action::ZoomOut, nullptr, zoom))
            ws.zoomOut();
        if (actionMenuItem(keys, Icon::ArrowExpand, tr(Str::MenuFitToWindow), Action::FitToWindow, nullptr, zoom))
            ws.fit();
        if (actionMenuItem(keys, Icon::ArrowCompress, tr(Str::MenuActualSize), Action::ActualSize, nullptr, zoom))
            ws.actualSize();
        ImGui::Separator();

        ws.drawViewMenuItems();
        if (iconMenuItem(Icon::Info, tr(Str::MenuStatusBar), nullptr, &settings_.showStatusBar))
            settingsDirty_ = true;
        if (iconMenuItem(Icon::Reset, tr(Str::MenuResetLayout))) {
            ws.resetLayout();
            framesToRender_ = std::max(framesToRender_, kFramesPerEvent);
        }
        ImGui::Separator();

        if (iconBeginMenu(Icon::Theme, tr(Str::MenuTheme))) {
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
                bool current = settings_.theme == item.mode;
                if (iconMenuItem(themeIcon(item.mode), tr(item.label), nullptr, &current)) {
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

// Paint | Flowchart | Pixel Art as a pill-shaped segmented control, right-aligned in the menu bar: a rounded track
// holds one rounded segment per mode, the shown mode filled with the accent color (and the text color that reads on
// it), the others plain with a soft highlight on hover. When the window is too narrow for all labels, only the shown
// mode keeps its label (every segment keeps its tooltip). While the mode cannot change (a gesture, a dialog) it is
// drawn dimmed and ignores clicks.
void App::drawModeSwitcher() {
    const ThemeTokens& t = tokens();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float em = ImGui::GetFontSize();
    const float height = ImGui::GetFrameHeight();
    const float inset = std::max(1.0f, std::round(style.FramePadding.y * 0.35f));
    const float trackH = height - 2.0f * inset;
    const float margin = std::max(2.0f, std::round(trackH * 0.1f)); // track edge to segment
    const float segH = trackH - 2.0f * margin;
    const float padX = std::round(em * 0.85f);
    const bool enabled = canSwitchMode();
    const float alpha = enabled ? 1.0f : style.DisabledAlpha;
    const bool dark = isDark();

    const float iconSize = std::min(std::round(em * 1.25f), std::max(8.0f, std::floor(segH - 2.0f)));
    const float iconGap = std::round(em * 0.45f);

    // Widths with every label, and without the labels of the modes that are not shown.
    float full[kModeCount];
    float compact[kModeCount];
    float fullTotal = 0.0f;
    float compactTotal = 0.0f;
    for (const Mode mode : kAllModes) {
        const int i = static_cast<int>(mode);
        full[i] = std::round(ImGui::CalcTextSize(tr(modeString(mode))).x + iconSize + iconGap + 2.0f * padX);
        compact[i] = mode == activeMode_ ? full[i] : std::round(iconSize + 2.0f * padX * 0.8f);
        fullTotal += full[i];
        compactTotal += compact[i];
    }

    const ImVec2 menuEnd = ImGui::GetCursorScreenPos();
    const float rightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - std::round(em * 0.5f);
    const float leftLimit = menuEnd.x + style.ItemSpacing.x; // never over the menus
    const bool showAll = rightEdge - (fullTotal + 2.0f * margin) >= leftLimit;
    const float* widths = showAll ? full : compact;
    const float total = showAll ? fullTotal : compactTotal;
    const float trackW = total + 2.0f * margin;
    const float startX = std::max(rightEdge - trackW, leftLimit);
    ImGui::SetCursorScreenPos(ImVec2(startX, menuEnd.y));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto mix = [](ImVec4 a, const ImVec4& b, float f) {
        return ImVec4(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f, 1.0f);
    };
    const auto withAlpha = [alpha](ImVec4 c, float extra = 1.0f) {
        c.w *= alpha * extra;
        return ImGui::ColorConvertFloat4ToU32(c);
    };
    const ImVec4 onAccent = contrastText(t.accent);
    const ImVec4 trackFill = dark ? mix(t.windowBg, ImVec4(0, 0, 0, 1), 0.35f) : mix(t.windowBg, t.text, 0.10f);
    const ImVec2 first = ImGui::GetCursorScreenPos();
    const float top = first.y + inset;

    // Track: a pill behind the segments, with a faint outline so it holds up on any window color.
    dl->AddRectFilled(ImVec2(first.x, top), ImVec2(first.x + trackW, top + trackH), withAlpha(trackFill), trackH * 0.5f);
    dl->AddRect(ImVec2(first.x, top), ImVec2(first.x + trackW, top + trackH), withAlpha(t.text, 0.16f), trackH * 0.5f);

    float x = first.x + margin;
    for (const Mode mode : kAllModes) {
        const int index = static_cast<int>(mode);
        const bool selected = mode == activeMode_;
        const char* label = tr(modeString(mode));
        const bool showLabel = showAll || selected;

        ImGui::PushID(index);
        ImGui::SetCursorScreenPos(ImVec2(x, top));
        ImGui::InvisibleButton("##mode", ImVec2(widths[index], trackH));
        x += widths[index];
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool hovered = enabled && ImGui::IsItemHovered();
        const bool held = enabled && ImGui::IsItemActive();
        const bool focused = enabled && ImGui::IsItemFocused();
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();
        const ImVec2 a(p0.x, p0.y + margin);
        const ImVec2 b(p1.x, p1.y - margin);
        const float segRound = segH * 0.5f;

        ImVec4 text = t.text;
        float textAlpha = hovered ? 1.0f : 0.78f;
        if (selected) {
            ImVec4 fill = t.accent;
            if (held)
                fill = mix(fill, ImVec4(0, 0, 0, 1), 0.15f);
            else if (hovered)
                fill = mix(fill, ImVec4(1, 1, 1, 1), 0.10f);
            dl->AddRectFilled(a, b, withAlpha(fill), segRound);
            dl->AddRect(a, b, withAlpha(mix(fill, onAccent, 0.22f)), segRound); // a soft edge, so the fill is not harsh
            text = onAccent;
            textAlpha = 1.0f;
        } else if (hovered || held) {
            dl->AddRectFilled(a, b, withAlpha(mix(trackFill, t.text, held ? 0.20f : 0.12f)), segRound);
        }
        if (focused)
            dl->AddRect(a, b, withAlpha(t.accent, 0.9f), segRound, 0, 2.0f);

        const ImVec2 size = ImGui::CalcTextSize(label);
        const float contentWidth = iconSize + (showLabel ? iconGap + size.x : 0.0f);
        const float contentX = std::round(p0.x + (widths[index] - contentWidth) * 0.5f);
        drawIcon(dl, modeIcon(mode), ImVec2(contentX, std::round(p0.y + (trackH - iconSize) * 0.5f)), iconSize,
                 withAlpha(text, textAlpha));
        if (showLabel)
            dl->AddText(ImVec2(contentX + iconSize + iconGap, std::round(p0.y + (trackH - size.y) * 0.5f)),
                        withAlpha(text, textAlpha), label);

        if (ImGui::IsItemHovered()) {
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
