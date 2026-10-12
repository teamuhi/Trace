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

#include "app/clipboard.hpp"
#include "app/keybinds.hpp"
#include "app/theme.hpp"
#include "app/theme_io.hpp"
#include "core/i18n.hpp"
#include "tools/tool_text.hpp"
#include "app/tools_panel.hpp"

namespace trace {

namespace {

constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 800;
constexpr int kIdleTimeoutMs = 1000; // also bounds the CPU/RAM refresh latency while idle
constexpr int kFramesPerEvent = 3;   // ImGui needs a few frames to settle after input
constexpr auto kStatsInterval = std::chrono::seconds(1);
constexpr float kBaseFontSize = 14.0f;
constexpr float kToolsPanelFraction = 0.11f;

// Tools on the left, the canvas filling the rest (with its tab bar hidden).
void buildDefaultLayout(ImGuiID dockId, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, size);

    ImGuiID left = 0;
    ImGuiID right = 0;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, kToolsPanelFraction, &left, &right);
    ImGui::DockBuilderDockWindow("Tools", left);
    ImGui::DockBuilderDockWindow("Canvas", right);
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(right))
        node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    ImGui::DockBuilderFinish(dockId);
}

// A menu entry whose shortcut text comes from the keymap, so it follows any rebinding.
bool actionItem(const Keymap& keys, const char* label, Action action, bool* selected = nullptr,
                bool enabled = true) {
    const std::string shortcut = displayShortcut(keys, action);
    return ImGui::MenuItem(label, shortcut.empty() ? nullptr : shortcut.c_str(), selected, enabled);
}

} // namespace

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

    canvas_.init(renderer_);

    FileSession::Hooks hooks;
    hooks.documentReplaced = [this](bool createdBlank) {
        if (createdBlank && settings_.newDocZoom == Settings::NewDocZoom::ActualSize)
            canvas_.actualSize();
        else
            canvas_.fitToWindow();
    };
    hooks.settingsChanged = [this] { settingsDirty_ = true; };
    hooks.saved = [this](const std::string& name) { toasts_.push(trf(Str::ToastSaved, {name})); };
    files_.setHooks(std::move(hooks));
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
    if (!files_.init(window_))
        SDL_Log("Native file dialogs are unavailable: no SDL event type could be registered");

    toolbox_.settings() = settings_.toolDefaults;
    toolbox_.select(settings_.startTool);
    files_.newDocumentNow();
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
        if (!running_ || files_.exitApproved())
            break;

        updateStats();

        if (framesToRender_ > 0) {
            renderFrame();
            --framesToRender_;
        }
        if (toasts_.active() && framesToRender_ < 1)
            framesToRender_ = 1; // keep drawing until the last toast has faded out
        if (files_.exitApproved())
            running_ = false;
    }
}

void App::shutdown() {
    if (settingsReady_ && settings_.restoreLastToolSettings)
        settings_.toolDefaults = toolbox_.settings();
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
    canvas_.release();
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
    if (files_.handleEvent(event))
        return; // a file dialog result or a dropped file

    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        files_.requestExit(); // asks about unsaved changes first; the loop stops once it is approved
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
    canvas_.setPixelGridEnabled(settings_.pixelGrid);
    canvas_.setPatterns(settings_.backdropPattern, settings_.transparencyPattern, settings_.patternSpacing);
    canvas_.setRulersEnabled(settings_.showRulers);
    canvas_.setGuidesVisible(settings_.showGuides);
    canvas_.setSnapToGuides(settings_.snapToGuides);
    document_.setHistoryLimit(static_cast<std::size_t>(settings_.historyLimitMb) * 1024u * 1024u);

    drawMenuBar();
    if (settings_.showStatusBar)
        drawStatusBar(); // must precede the dockspace so it reserves its work-area strip first

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockId = ImHashStr("TraceDockSpace");
    const bool buildLayout = ImGui::DockBuilderGetNode(dockId) == nullptr; // no saved layout yet
    ImGui::DockSpaceOverViewport(dockId, viewport);
    if (buildLayout)
        buildDefaultLayout(dockId, viewport->WorkSize);

    if (settings_.showToolsPanel) {
        if (ImGui::Begin("Tools", &settings_.showToolsPanel))
            drawToolsPanel(toolbox_, settings_.keymap);
        ImGui::End();
        settingsDirty_ |= !settings_.showToolsPanel; // closed with its title bar button
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags canvasFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("Canvas", nullptr, canvasFlags))
        canvas_.draw(document_, toolbox_);
    ImGui::End();
    ImGui::PopStyleVar();

    settingsDirty_ |= settingsWindow_.draw(settings_, toolbox_, settingsPath_, dialogs_, languages_);
    files_.drawModals();
    if (const auto outcome = imageDialogs_.draw(document_, toolbox_.settings().secondary)) {
        if (outcome->error)
            files_.report(outcome->text, true);
        else
            toasts_.push(outcome->text);
    }
    toasts_.draw();

    const std::string title = files_.windowTitle();
    if (title != windowTitle_) {
        windowTitle_ = title;
        SDL_SetWindowTitle(window_, windowTitle_.c_str());
    }

    handleShortcuts();
}

void App::handleShortcuts() {
    if (files_.busy() || imageDialogs_.busy() || dialogs_.busy())
        return; // a dialog or prompt is open: shortcuts must not act behind it

    const Keymap& keys = settings_.keymap;

    // File and history shortcuts wait until the current gesture is finished.
    if (!toolbox_.gestureActive()) {
        if (triggered(keys, Action::NewCanvas))
            files_.requestNew();
        if (triggered(keys, Action::OpenFile))
            files_.requestOpen();
        if (triggered(keys, Action::SaveFile))
            files_.requestSave();
        if (triggered(keys, Action::SaveFileAs))
            files_.requestSaveAs();
        if (triggered(keys, Action::Undo))
            document_.undo();
        if (triggered(keys, Action::Redo))
            document_.redo();
        if (triggered(keys, Action::DeleteSelection))
            deleteSelection();
        if (triggered(keys, Action::ResizeImage))
            imageDialogs_.openResize(document_);
        if (triggered(keys, Action::CanvasSize))
            imageDialogs_.openCanvasSize(document_);
        if (triggered(keys, Action::CropToSelection))
            cropToSelection();
        // Ctrl+C, V, X and A belong to a focused text field (a hex color, a number) when there is one.
        if (!ImGui::GetIO().WantTextInput) {
            if (triggered(keys, Action::Cut))
                copySelection(true);
            if (triggered(keys, Action::Copy))
                copySelection(false);
            if (triggered(keys, Action::Paste))
                pasteClipboard();
            if (triggered(keys, Action::SelectAll))
                selectAll();
            if (triggered(keys, Action::Deselect))
                document_.clearSelection();
        }
    }
    if (triggered(keys, Action::OpenSettings))
        settingsWindow_.open();
    if (triggered(keys, Action::ZoomIn))
        canvas_.zoomIn();
    if (triggered(keys, Action::ZoomOut))
        canvas_.zoomOut();
    if (triggered(keys, Action::FitToWindow))
        canvas_.fitToWindow();
    if (triggered(keys, Action::ActualSize))
        canvas_.actualSize();
    if (triggered(keys, Action::TogglePixelGrid)) {
        settings_.pixelGrid = !settings_.pixelGrid;
        settingsDirty_ = true;
    }
    if (triggered(keys, Action::ToggleRulers)) {
        settings_.showRulers = !settings_.showRulers;
        settingsDirty_ = true;
    }
    if (triggered(keys, Action::ToggleGuides)) {
        settings_.showGuides = !settings_.showGuides;
        settingsDirty_ = true;
    }
    if (triggered(keys, Action::ToggleSnapToGuides)) {
        settings_.snapToGuides = !settings_.snapToGuides;
        settingsDirty_ = true;
    }

    for (int i = 0; i < kToolCount; ++i) {
        const ToolId id = static_cast<ToolId>(i);
        if (triggered(keys, toolAction(id)))
            toolbox_.select(id);
    }
    if (triggered(keys, Action::SwapColors))
        toolbox_.swapColors();

    const int step = (triggered(keys, Action::IncreaseSize, true) ? 1 : 0) -
                     (triggered(keys, Action::DecreaseSize, true) ? 1 : 0);
    if (step != 0) {
        ToolSettings& s = toolbox_.settings();
        const ToolId tool = toolbox_.current();
        const bool shape = tool == ToolId::Line || tool == ToolId::Rectangle || tool == ToolId::Ellipse;
        int& size = shape ? s.strokeWidth : s.brushSize;
        size = std::clamp(size + step, ToolSettings::kMinSize,
                          shape ? ToolSettings::kMaxStrokeWidth : ToolSettings::kMaxSize);
    }
}

Rgba8 App::clearColor() const {
    const ToolSettings& t = toolbox_.settings();
    return t.eraseToTransparent ? Rgba8{} : t.secondary; // what the eraser would leave
}

void App::copySelection(bool cut) {
    if (!document_.selection().active())
        return;
    const Image pixels = document_.copySelection();
    if (pixels.empty())
        return; // the selection is entirely off the image
    if (!setClipboardImage(pixels)) {
        toasts_.push(tr(Str::ErrClipboardCopy), Toasts::Kind::Error);
        return;
    }
    toasts_.push(tr(cut ? Str::ToastCut : Str::ToastCopied));
    if (cut)
        deleteSelection();
}

void App::pasteClipboard() {
    LoadResult result = getClipboardImage();
    if (!result.image) {
        toasts_.push(result.error, Toasts::Kind::Error);
        return;
    }
    // Drop it at the top-left of what is visible, so it always lands where the user is looking.
    const Vec2f origin = canvas_.visibleImageOrigin();
    const int x = std::max(0, static_cast<int>(std::floor(origin.x)));
    const int y = std::max(0, static_cast<int>(std::floor(origin.y)));
    if (!document_.paste(std::move(*result.image), x, y)) {
        files_.report(tr(Str::ErrPasteMemory), true);
        return;
    }
    toolbox_.select(ToolId::Select); // so it can be dragged into place straight away
    document_.commit();
}

void App::deleteSelection() {
    if (!document_.selection().active())
        return;
    document_.deleteSelection(clearColor());
    document_.commit();
}

void App::cropToSelection() {
    switch (document_.cropToSelection()) {
    case Document::TransformResult::NoMemory:
        files_.report(tr(Str::ErrCropMemory), true);
        break;
    case Document::TransformResult::Applied:
        toasts_.push(trf(Str::ToastCropped,
                         {std::to_string(document_.image().width()), std::to_string(document_.image().height())}));
        break;
    case Document::TransformResult::Nothing:
        break;
    }
}

void App::selectAll() {
    toolbox_.select(ToolId::Select);
    document_.selectAll();
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
    const Keymap& keys = settings_.keymap;

    if (ImGui::BeginMenu(tr(Str::MenuFile))) {
        if (actionItem(keys, tr(Str::MenuNew), Action::NewCanvas))
            files_.requestNew();
        if (actionItem(keys, tr(Str::MenuOpen), Action::OpenFile))
            files_.requestOpen();
        ImGui::Separator();
        if (actionItem(keys, tr(Str::MenuSave), Action::SaveFile))
            files_.requestSave();
        if (actionItem(keys, tr(Str::MenuSaveAs), Action::SaveFileAs))
            files_.requestSaveAs();
        ImGui::Separator();
        if (actionItem(keys, tr(Str::MenuSettings), Action::OpenSettings))
            settingsWindow_.open();
        ImGui::Separator();
        if (ImGui::MenuItem(tr(Str::MenuExit)))
            files_.requestExit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr(Str::MenuEdit))) {
        const bool idle = !toolbox_.gestureActive();
        if (actionItem(keys, tr(Str::MenuUndo), Action::Undo, nullptr, idle && document_.canUndo()))
            document_.undo();
        if (actionItem(keys, tr(Str::MenuRedo), Action::Redo, nullptr, idle && document_.canRedo()))
            document_.redo();
        ImGui::Separator();
        const bool selected = idle && document_.selection().active();
        if (actionItem(keys, tr(Str::MenuCut), Action::Cut, nullptr, selected))
            copySelection(true);
        if (actionItem(keys, tr(Str::MenuCopy), Action::Copy, nullptr, selected))
            copySelection(false);
        if (actionItem(keys, tr(Str::MenuPaste), Action::Paste, nullptr, idle && clipboardHasImage()))
            pasteClipboard();
        if (actionItem(keys, tr(Str::MenuDelete), Action::DeleteSelection, nullptr, selected))
            deleteSelection();
        ImGui::Separator();
        if (actionItem(keys, tr(Str::MenuSelectAll), Action::SelectAll, nullptr, idle))
            selectAll();
        if (actionItem(keys, tr(Str::MenuDeselect), Action::Deselect, nullptr, selected))
            document_.clearSelection();
        ImGui::Separator();
        if (actionItem(keys, tr(Str::MenuSwapColors), Action::SwapColors))
            toolbox_.swapColors();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr(Str::MenuImage))) {
        const bool idle = !toolbox_.gestureActive();
        if (actionItem(keys, tr(Str::MenuResize), Action::ResizeImage, nullptr, idle))
            imageDialogs_.openResize(document_);
        if (actionItem(keys, tr(Str::MenuCanvasSize), Action::CanvasSize, nullptr, idle))
            imageDialogs_.openCanvasSize(document_);
        ImGui::Separator();
        if (actionItem(keys, tr(Str::MenuCropToSelection), Action::CropToSelection, nullptr,
                       idle && document_.selection().active()))
            cropToSelection();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr(Str::MenuView))) {
        if (actionItem(keys, tr(Str::MenuZoomIn), Action::ZoomIn))
            canvas_.zoomIn();
        if (actionItem(keys, tr(Str::MenuZoomOut), Action::ZoomOut))
            canvas_.zoomOut();
        if (actionItem(keys, tr(Str::MenuFitToWindow), Action::FitToWindow))
            canvas_.fitToWindow();
        if (actionItem(keys, tr(Str::MenuActualSize), Action::ActualSize))
            canvas_.actualSize();
        ImGui::Separator();

        if (actionItem(keys, tr(Str::MenuPixelGrid), Action::TogglePixelGrid, &settings_.pixelGrid))
            settingsDirty_ = true;
        if (actionItem(keys, tr(Str::MenuRulers), Action::ToggleRulers, &settings_.showRulers))
            settingsDirty_ = true;
        if (actionItem(keys, tr(Str::MenuGuides), Action::ToggleGuides, &settings_.showGuides))
            settingsDirty_ = true;
        if (actionItem(keys, tr(Str::MenuSnapToGuides), Action::ToggleSnapToGuides, &settings_.snapToGuides,
                       settings_.showGuides))
            settingsDirty_ = true;
        if (ImGui::MenuItem(tr(Str::MenuClearGuides), nullptr, false, !document_.guides().empty()))
            document_.guides().clear();
        if (ImGui::MenuItem(tr(Str::MenuToolsPanel), nullptr, &settings_.showToolsPanel))
            settingsDirty_ = true;
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

    ImGui::EndMainMenuBar();
}

void App::drawStatusBar() {
    const float height = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, tokens().statusBarBg);
    ImGui::PushStyleColor(ImGuiCol_Text, tokens().text);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginViewportSideBar("##StatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, height, flags)) {
        char zoom[24];
        std::snprintf(zoom, sizeof(zoom), "%.0f", static_cast<double>(canvas_.zoomPercent()));
        const std::string widthText = std::to_string(document_.image().width());
        const std::string heightText = std::to_string(document_.image().height());
        const char* tool = tr(toolString(toolbox_.current()));
        const std::string left =
            canvas_.hasHoverPixel()
                ? trf(Str::StatusHover, {tool, std::to_string(canvas_.hoverX()), std::to_string(canvas_.hoverY()), widthText,
                                         heightText, zoom})
                : trf(Str::StatusIdle, {tool, widthText, heightText, zoom});
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
