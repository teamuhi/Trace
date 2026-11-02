#include "app/pixel_workspace.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "app/clipboard.hpp"
#include "app/dock_util.hpp"
#include "app/icon_widgets.hpp"
#include "app/keybinds.hpp"
#include "app/raster_common.hpp"
#include "app/settings.hpp"
#include "app/symmetry_panel.hpp"
#include "app/toasts.hpp"
#include "app/tools_panel.hpp"
#include "core/i18n.hpp"
#include "io/image_io.hpp"
#include "io/pixel_export.hpp"
#include "io/path_utf8.hpp"
#include "tools/tool_text.hpp"

namespace trace {

namespace {

constexpr float kToolsPanelFraction = 0.15f;
constexpr float kLayersPanelFraction = 0.21f;
constexpr float kPalettePanelFraction = 0.42f; // of the right column, below Layers
constexpr float kTimelinePanelFraction = 0.27f; // of the height, along the whole bottom

// The nine tools of Pixel Art mode, in the order of the panel. (Every raster tool but Text; the list is here so the
// panel and the shortcuts never depend on it.)
constexpr std::array<ToolId, 9> kPixelTools = {ToolId::Pencil, ToolId::Brush,   ToolId::Eraser,
                                               ToolId::Line,   ToolId::Rectangle, ToolId::Ellipse,
                                               ToolId::Fill,   ToolId::Picker,  ToolId::Select};

// Visible titles are translated; the part after ### is the window's identity in the saved layout.
std::string toolsWindow() {
    return std::string(tr(Str::WindowPixelTools)) + "###PixelTools";
}
std::string canvasWindow() {
    return std::string(tr(Str::WindowPixelCanvas)) + "###PixelCanvas";
}
std::string layersWindow() {
    return std::string(tr(Str::WindowLayers)) + "###PixelLayers";
}
std::string paletteWindow() {
    return std::string(tr(Str::WindowPalette)) + "###PixelPalette";
}
std::string timelineWindow() {
    return std::string(tr(Str::WindowTimeline)) + "###PixelTimeline";
}

} // namespace

PixelWorkspace::PixelWorkspace(WorkspaceContext& context)
    : Workspace(context),
      imageDialogs_(true),
      palettePanel_(context, palette_),
      adapter_(document_, context.settings, palette_),
      files_(adapter_, context.settings, {}) {}

void PixelWorkspace::init(SDL_Window* window, SDL_Renderer* renderer) {
    canvas_.init(renderer);
    canvas_.setPixelArtView(true);
    layers_.init(renderer);
    timeline_.init(renderer);

    FileSession::Hooks hooks;
    hooks.documentReplaced = [this](bool) {
        layers_.reset();
        timeline_.reset();
        palettePanel_.reset();
        canvas_.fitToWindow(); // a new or opened document always starts fitted to the window
        // Every mirror line and rotation center goes back to the middle of the new canvas; the list stays.
        SymmetrySet& set = toolbox_.settings().symmetry;
        for (int i = 0; i < std::clamp(set.count, 0, kMaxSymmetryItems); ++i) {
            set.items[static_cast<std::size_t>(i)].posX = kSymmetryCenter;
            set.items[static_cast<std::size_t>(i)].posY = kSymmetryCenter;
        }
    };
    hooks.settingsChanged = [this] { ctx_.settingsDirty = true; };
    hooks.saved = [this](const std::string& name) { ctx_.toasts.push(trf(Str::ToastSaved, {name})); };
    hooks.routeOpen = ctx_.routeOpen;
    files_.setHooks(std::move(hooks));
    if (!files_.init(window))
        SDL_Log("Native file dialogs are unavailable: no SDL event type could be registered");

    toolbox_.settings() = ctx_.settings.pixel.tools;
    synced_ = toolbox_.settings();
    toolbox_.select(ToolId::Pencil);
    files_.newDocumentNow();
}

void PixelWorkspace::release() noexcept {
    canvas_.release();
    layers_.release();
    timeline_.release();
}

// --- windows -----------------------------------------------------------------------------------

ImGuiID PixelWorkspace::dockSpaceId() const {
    return ImHashStr("TraceDockSpace.Pixel");
}

// Tools on the left, layers and the palette stacked on the right, the canvas in between (with its tab bar hidden),
// and the timeline along the whole bottom.
void PixelWorkspace::buildDefaultLayout(ImGuiID dockId, ImVec2 size) {
    layoutChecked_ = true;
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, size);

    ImGuiID bottom = 0;
    ImGuiID top = 0;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Down, kTimelinePanelFraction, &bottom, &top);
    ImGui::DockBuilderDockWindow(timelineWindow().c_str(), bottom);
    ImGuiID left = 0;
    ImGuiID rest = 0;
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, kToolsPanelFraction, &left, &rest);
    ImGuiID right = 0;
    ImGuiID center = 0;
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, kLayersPanelFraction / (1.0f - kToolsPanelFraction), &right, &center);
    ImGui::DockBuilderDockWindow(toolsWindow().c_str(), left);
    ImGui::DockBuilderDockWindow(canvasWindow().c_str(), center);
    ImGuiID rightTop = 0;
    ImGuiID rightBottom = 0;
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, kPalettePanelFraction, &rightBottom, &rightTop);
    ImGui::DockBuilderDockWindow(layersWindow().c_str(), rightTop);
    ImGui::DockBuilderDockWindow(paletteWindow().c_str(), rightBottom);
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(center))
        node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    ImGui::DockBuilderFinish(dockId);
}

// An imgui.ini written while this mode was still a placeholder has the dockspace but none of its windows, and one
// written before the Palette or Timeline window existed lacks that one; one that docked a panel into the canvas's
// node has that panel hidden behind the canvas. All are detected once, when the mode is first shown: no saved
// settings for the canvas, the palette or the timeline window, or a panel in the canvas's node, means rebuild.
bool PixelWorkspace::layoutStale(ImGuiID) {
    if (layoutChecked_)
        return false;
    layoutChecked_ = true;
    return ImGui::FindWindowSettingsByID(ImHashStr(canvasWindow().c_str())) == nullptr ||
           ImGui::FindWindowSettingsByID(ImHashStr(paletteWindow().c_str())) == nullptr ||
           ImGui::FindWindowSettingsByID(ImHashStr(timelineWindow().c_str())) == nullptr ||
           panelHiddenBehindCanvas(canvasWindow(),
                                   {toolsWindow(), layersWindow(), paletteWindow(), timelineWindow()});
}

void PixelWorkspace::showAllWindows() {
    PixelSettings& px = ctx_.settings.pixel;
    px.showToolsPanel = true;
    px.showLayersPanel = true;
    showPalette_ = true;
    showTimeline_ = true;
    ctx_.settingsDirty = true;
}

void PixelWorkspace::syncToolSettings() {
    PixelSettings& px = ctx_.settings.pixel;
    ToolSettings& live = toolbox_.settings();

    // The symmetry list is session state: it is not saved, so the stored copy only ever mirrors the live one. Done
    // first, so neither Restore defaults nor a settings change can reset it or count as a change. Every item is
    // forced into range, and a position never stays beyond the canvas it was set on (after a smaller canvas, a
    // bigger one would show it); the exact middle stays "center".
    SymmetrySet& set = live.symmetry;
    set.count = std::clamp(set.count, 0, kMaxSymmetryItems);
    for (int i = 0; i < set.count; ++i) {
        SymmetryItem& item = set.items[static_cast<std::size_t>(i)];
        item = sanitizedItem(item);
        item.posX = item.posX < 0 ? item.posX : std::min(item.posX, 2 * document_.image().width());
        item.posY = item.posY < 0 ? item.posY : std::min(item.posY, 2 * document_.image().height());
    }
    px.tools.symmetry = live.symmetry;
    synced_.symmetry = live.symmetry;

    // Settings changed from outside (Restore defaults, a reloaded file): the tools follow.
    if (!(px.tools == synced_))
        live = px.tools; // its symmetry was just set from `live`, so the list carries over

    // Whole pixels only: no antialiasing, the eraser clears, sizes stay inside pixel art's range.
    live.antialias = false;
    live.eraseToTransparent = true;
    live.brushHardness = 100; // no soft edge in whole pixels
    live.brushSize = std::clamp(live.brushSize, ToolSettings::kMinSize, ToolSettings::kMaxPixelSize);
    live.strokeWidth = std::clamp(live.strokeWidth, ToolSettings::kMinSize, ToolSettings::kMaxPixelSize);

    if (!(live == px.tools)) {
        px.tools = live;
        ctx_.settingsDirty = true;
    }
    synced_ = px.tools;
}

void PixelWorkspace::drawWindows() {
    Settings& settings = ctx_.settings;
    PixelSettings& px = settings.pixel;
    syncToolSettings();

    canvas_.setPixelGridEnabled(settings.pixelGrid);
    canvas_.setGridMinZoom(static_cast<float>(px.gridZoomPercent) / 100.0f);
    canvas_.setPatterns(settings.backdropPattern, settings.transparencyPattern, settings.patternSpacing);
    canvas_.setRulersEnabled(settings.showRulers);
    canvas_.setGuidesVisible(settings.showGuides);
    canvas_.setSnapToGuides(settings.snapToGuides);
    canvas_.setTileDimPercent(px.tileDimPercent);
    canvas_.setOnionOptions(px.onionBefore, px.onionAfter, px.onionOpacityPercent, px.onionLoop);
    document_.setHistoryLimit(static_cast<std::size_t>(settings.historyLimitMb) * 1024u * 1024u);

    // Playback moves the shown frame before the canvas is drawn, so a new frame shows at once; while it runs the
    // tools refuse to draw.
    timeline_.update(document_, toolbox_.gestureActive());
    toolbox_.setEditingBlocked(timeline_.playing());

    if (px.showToolsPanel) {
        if (ImGui::Begin(toolsWindow().c_str(), &px.showToolsPanel))
            drawToolsPanel(toolbox_, settings.keymap, kPixelTools, ToolOptions::Pixel,
                           PixelCanvasSize{document_.image().width(), document_.image().height()});
        ImGui::End();
        ctx_.settingsDirty |= !px.showToolsPanel; // closed with its title bar button
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags canvasFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    refuseDockingOverCanvas();
    if (ImGui::Begin(canvasWindow().c_str(), nullptr, canvasFlags)) {
        ctx_.toasts.setCanvasRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());
        canvas_.draw(document_, toolbox_);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // A press on a hidden or locked layer did nothing; say why, once per click.
    if (toolbox_.refusalCount() != seenRefusals_) {
        seenRefusals_ = toolbox_.refusalCount();
        Str reason = Str::ToastLayerLocked;
        if (toolbox_.lastRefusal() == Toolbox::Refusal::LayerHidden)
            reason = Str::ToastLayerHidden;
        else if (toolbox_.lastRefusal() == Toolbox::Refusal::Playing)
            reason = Str::ToastPauseToDraw;
        ctx_.toasts.push(tr(reason), Toasts::Kind::Error);
    }

    if (px.showLayersPanel) {
        if (ImGui::Begin(layersWindow().c_str(), &px.showLayersPanel))
            layers_.draw(document_, toolbox_.gestureActive(), ctx_.toasts);
        ImGui::End();
        ctx_.settingsDirty |= !px.showLayersPanel;
    }

    if (showPalette_) {
        if (ImGui::Begin(paletteWindow().c_str(), &showPalette_))
            palettePanel_.draw(document_, toolbox_);
        ImGui::End();
    }

    if (showTimeline_) {
        if (ImGui::Begin(timelineWindow().c_str(), &showTimeline_))
            timeline_.draw(document_, toolbox_.gestureActive(), ctx_.toasts);
        ImGui::End();
    }
}

void PixelWorkspace::drawDialogs() {
    files_.drawModals();
    palettePanel_.drawDialogs(document_, toolbox_);
    if (const auto request = exportDialog_.draw(document_))
        beginExport(*request);
    if (const auto outcome = imageDialogs_.draw(document_, toolbox_.settings().secondary)) {
        if (outcome->error)
            files_.report(outcome->text, true);
        else
            ctx_.toasts.push(outcome->text);
    }
}

// --- menus -------------------------------------------------------------------------------------

void PixelWorkspace::drawModeMenus() {
    const Keymap& keys = ctx_.settings.keymap;
    const bool ready = idle();

    if (ImGui::BeginMenu(tr(Str::MenuImage))) {
        if (ImGui::MenuItem(tr(Str::MenuResize), nullptr, false, ready))
            imageDialogs_.openResize(document_, kMaxPixelCanvasSide);
        if (ImGui::MenuItem(tr(Str::MenuCanvasSize), nullptr, false, ready))
            imageDialogs_.openCanvasSize(document_, kMaxPixelCanvasSide);
        ImGui::Separator();
        if (iconMenuItem(Icon::Crop, tr(Str::MenuCropToSelection), nullptr, nullptr, hasSelection()))
            cropToSelection();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr(Str::MenuLayer))) {
        if (const auto command = drawLayerMenuItems(document_, &keys, ready))
            runCommand(*command);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr(Str::MenuFrame))) {
        const auto item = [&](Icon icon, const char* label, LayerCommand command, bool shortcut) {
            const bool enabled = ready && canRunLayerCommand(document_, command);
            const bool clicked = shortcut ? actionMenuItem(keys, icon, label, Action::NewFrame, nullptr, enabled)
                                          : iconMenuItem(icon, label, nullptr, nullptr, enabled);
            if (clicked)
                runCommand(command);
        };
        item(Icon::AddLayer, tr(Str::MenuNewFrame), LayerCommand::NewFrame, true);
        item(Icon::Duplicate, tr(Str::MenuDuplicateFrame), LayerCommand::DuplicateFrame, false);
        item(Icon::Delete, tr(Str::MenuDeleteFrame), LayerCommand::DeleteFrame, false);
        ImGui::Separator();
        const bool several = ready && document_.frameCount() > 1;
        if (actionMenuItem(keys, Icon::NextFrame, tr(Str::MenuNextFrame), Action::NextFrame, nullptr, several))
            timeline_.stepFrame(document_, 1, ctx_.toasts);
        if (actionMenuItem(keys, Icon::PreviousFrame, tr(Str::MenuPrevFrame), Action::PrevFrame, nullptr, several))
            timeline_.stepFrame(document_, -1, ctx_.toasts);
        ImGui::Separator();
        const bool playing = timeline_.playing();
        if (actionMenuItem(keys, playing ? Icon::VideoPause : Icon::VideoPlay,
                           tr(playing ? Str::MenuPauseAnimation : Str::MenuPlayAnimation), Action::PlayAnimation, nullptr,
                           ready && (playing || several)))
            timeline_.togglePlay(document_);
        bool onion = canvas_.onionSkin();
        if (actionMenuItem(keys, Icon::OnionSkin, tr(Str::MenuOnionSkin), Action::ToggleOnionSkin, &onion))
            canvas_.setOnionSkin(onion);
        ImGui::EndMenu();
    }
}

void PixelWorkspace::drawEditMenuExtras() {
    ImGui::Separator();
    if (actionMenuItem(ctx_.settings.keymap, Icon::SortVertical, tr(Str::MenuSwapColors), Action::SwapColors))
        toolbox_.swapColors();
}

void PixelWorkspace::drawViewMenuItems() {
    PixelSettings& px = ctx_.settings.pixel;
    drawRasterViewItems(ctx_.settings, ctx_.settingsDirty, document_);
    if (iconMenuItem(Icon::SlidersHorizontal, tr(Str::MenuToolsPanel), nullptr, &px.showToolsPanel))
        ctx_.settingsDirty = true;
    if (iconMenuItem(Icon::AddLayer, tr(Str::MenuLayersPanel), nullptr, &px.showLayersPanel))
        ctx_.settingsDirty = true;
    iconMenuItem(Icon::PaintSplash, tr(Str::MenuPalettePanel), nullptr, &showPalette_);
    iconMenuItem(Icon::VideoLoop, tr(Str::MenuTimelinePanel), nullptr, &showTimeline_);
    ImGui::Separator();
    bool tiled = canvas_.tilePreview();
    if (actionMenuItem(ctx_.settings.keymap, Icon::Preview, tr(Str::ActionToggleTilePreview), Action::ToggleTilePreview,
                       &tiled, idle()))
        canvas_.setTilePreview(tiled);
}

// --- input -------------------------------------------------------------------------------------

void PixelWorkspace::runCommand(LayerCommand command) {
    if (!idle())
        return;
    timeline_.pause(); // a layer or frame change ends playback
    runLayerCommand(document_, command, ctx_.toasts);
}

void PixelWorkspace::handleShortcuts(const Keymap& keys) {
    // Layer changes wait until the current gesture is finished.
    if (idle() && triggeredIn(keys, Action::NewLayer, ModeContext::Pixel))
        runCommand(LayerCommand::New);
    if (idle() && triggeredIn(keys, Action::ToggleTilePreview, ModeContext::Pixel))
        canvas_.setTilePreview(!canvas_.tilePreview());
    // Frames and playback wait for the current gesture too, and are not for a text field to trigger.
    if (idle() && !ImGui::GetIO().WantTextInput) {
        if (triggeredIn(keys, Action::NewFrame, ModeContext::Pixel))
            runCommand(LayerCommand::NewFrame);
        if (triggeredIn(keys, Action::NextFrame, ModeContext::Pixel))
            timeline_.stepFrame(document_, 1, ctx_.toasts);
        if (triggeredIn(keys, Action::PrevFrame, ModeContext::Pixel))
            timeline_.stepFrame(document_, -1, ctx_.toasts);
        if (triggeredIn(keys, Action::PlayAnimation, ModeContext::Pixel))
            timeline_.togglePlay(document_);
        if (triggeredIn(keys, Action::ToggleOnionSkin, ModeContext::Pixel))
            canvas_.setOnionSkin(!canvas_.onionSkin());
    }
    // Mirroring never changes in the middle of a stroke.
    SymmetrySet& symmetry = toolbox_.settings().symmetry;
    for (const int preset : {0, 1}) {
        const Action action = preset == 0 ? Action::ToggleSymmetryX : Action::ToggleSymmetryY;
        if (idle() && triggeredIn(keys, action, ModeContext::Pixel) && !toggleMirrorPreset(symmetry, preset))
            ctx_.toasts.push(tr(Str::SymAddLimit), Toasts::Kind::Error);
    }
    handleRasterShortcuts(keys, ModeContext::Pixel, ctx_.settings, ctx_.settingsDirty, toolbox_, document_, idle(),
                          kPixelTools, ToolSettings::kMaxPixelSize, ToolSettings::kMaxPixelSize);
}

// --- status ------------------------------------------------------------------------------------

std::string PixelWorkspace::statusLeft() const {
    char zoom[24];
    std::snprintf(zoom, sizeof(zoom), "%.0f", static_cast<double>(canvas_.zoomPercent()));
    const std::string widthText = std::to_string(document_.image().width());
    const std::string heightText = std::to_string(document_.image().height());
    const char* tool = tr(toolString(toolbox_.current()));
    const std::string& layer = document_.layer(document_.activeLayer()).name;
    return canvas_.hasHoverPixel()
               ? trf(Str::StatusPixelHover, {tool, std::to_string(canvas_.hoverX()), std::to_string(canvas_.hoverY()),
                                             widthText, heightText, zoom, layer})
               : trf(Str::StatusPixelIdle, {tool, widthText, heightText, zoom, layer});
}

// --- edit verbs --------------------------------------------------------------------------------

bool PixelWorkspace::ensureEditable() {
    const LayerInfo& layer = document_.layer(document_.activeLayer());
    if (!layer.visible) {
        ctx_.toasts.push(tr(Str::ToastLayerHidden), Toasts::Kind::Error);
        return false;
    }
    if (layer.locked) {
        ctx_.toasts.push(tr(Str::ToastLayerLocked), Toasts::Kind::Error);
        return false;
    }
    return true;
}

bool PixelWorkspace::canPaste() const {
    return idle() && clipboardHasImage();
}

void PixelWorkspace::copySelection(bool cut) {
    if (cut && !ensureEditable())
        return; // nothing is copied either, so a refused cut leaves the clipboard alone
    if (!cut && !document_.selection().active()) {
        copyPictureToClipboard(document_, ctx_.toasts); // no selection: the whole picture, to paste into Paint
        return;
    }
    if (copySelectionToClipboard(document_, ctx_.toasts, cut) && cut)
        deleteSelection();
}

void PixelWorkspace::paste() {
    if (!ensureEditable())
        return;
    LoadResult result = getClipboardImage();
    if (!result.image) {
        ctx_.toasts.push(result.error, Toasts::Kind::Error);
        return;
    }
    // Drop it at the top-left of what is visible, so it always lands where the user is looking. It floats on the
    // active layer; a paste larger than the canvas is clipped.
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

void PixelWorkspace::dropImage(const std::filesystem::path& path, ImVec2 screen, int index) {
    if (!ensureEditable())
        return;
    constexpr std::size_t kMaxImportPixels = 32u * 1024u * 1024u; // all frames of a dropped GIF together
    AnimationLoadResult loaded = loadAnimation(path, static_cast<std::size_t>(kMaxFrames), kMaxImportPixels);
    if (loaded.frames.empty()) {
        ctx_.toasts.push(loaded.error, Toasts::Kind::Error);
        return;
    }

    const Image& canvas = document_.image();
    const int canvasW = canvas.width();
    const int canvasH = canvas.height();
    const int w = loaded.frames.front().width();
    const int h = loaded.frames.front().height();
    // Centered on the drop point, or on the middle of the view when it is not over the canvas; each further file of
    // one drop steps down and right. A picture that fits is kept fully inside the canvas.
    Vec2f center;
    if (const std::optional<Vec2f> hit = canvas_.screenToImage(screen.x, screen.y)) {
        center = *hit;
    } else {
        center = Vec2f{static_cast<float>(canvasW) * 0.5f, static_cast<float>(canvasH) * 0.5f};
    }
    const float step = static_cast<float>(index) * 8.0f;
    int x = static_cast<int>(std::floor(center.x + step)) - w / 2;
    int y = static_cast<int>(std::floor(center.y + step)) - h / 2;
    x = w <= canvasW ? std::clamp(x, 0, canvasW - w) : (canvasW - w) / 2;
    y = h <= canvasH ? std::clamp(y, 0, canvasH - h) : (canvasH - h) / 2;
    if (w > canvasW || h > canvasH)
        ctx_.toasts.push(tr(Str::ToastDropClipped));

    if (loaded.frames.size() == 1) {
        if (loaded.totalFrames > 1)
            ctx_.toasts.push(tr(Str::ErrGifTooLarge), Toasts::Kind::Error);
        if (!document_.paste(std::move(loaded.frames.front()), x, y)) {
            files_.report(tr(Str::ErrPasteMemory), true);
            return;
        }
        toolbox_.select(ToolId::Select);
        document_.commit();
        return;
    }

    // How many frames the document can take: the frame limit from the active frame on, and the memory budget with one
    // more layer.
    const long long budget = static_cast<long long>(kMaxDocumentPixels) /
                             (static_cast<long long>(canvasW) * canvasH * (document_.layerCount() + 1));
    std::size_t count = loaded.frames.size();
    count = std::min<std::size_t>(count, static_cast<std::size_t>(std::max(0, kMaxFrames - document_.activeFrame())));
    count = std::min<std::size_t>(count, static_cast<std::size_t>(std::max<long long>(0, budget)));
    if (count == 0 || document_.layerCount() >= kMaxLayers) {
        ctx_.toasts.push(tr(Str::ToastLayerTooBig), Toasts::Kind::Error);
        return;
    }
    const std::size_t total = static_cast<std::size_t>(std::max(loaded.totalFrames, static_cast<int>(loaded.frames.size())));
    switch (document_.importFrames(std::span<const Image>(loaded.frames.data(), count),
                                   std::span<const int>(loaded.delaysMs.data(), count), x, y, utf8FromPath(path.stem()))) {
    case Document::OpResult::Applied:
        if (count < total)
            ctx_.toasts.push(trf(Str::ToastDropFramesTrimmed, {std::to_string(total), std::to_string(count)}));
        else
            ctx_.toasts.push(trf(Str::ToastDropFramesImported, {std::to_string(count)}));
        break;
    case Document::OpResult::NoMemory:
        files_.report(tr(Str::ErrLayerMemory), true);
        break;
    case Document::OpResult::Nothing:
        break;
    }
}

void PixelWorkspace::deleteSelection() {
    if (!document_.selection().active() || !ensureEditable())
        return;
    document_.deleteSelection(Rgba8{}); // pixel art layers clear to transparency
    document_.commit();
}

void PixelWorkspace::selectAll() {
    toolbox_.select(ToolId::Select);
    document_.selectAll();
}

void PixelWorkspace::cropToSelection() {
    switch (document_.cropToSelection()) {
    case Document::TransformResult::NoMemory:
        files_.report(tr(Str::ErrCropMemory), true);
        break;
    case Document::TransformResult::Applied:
        ctx_.toasts.push(trf(Str::ToastCropped, {std::to_string(document_.image().width()),
                                                 std::to_string(document_.image().height())}));
        break;
    case Document::TransformResult::Nothing:
        break;
    }
}

// --- export ------------------------------------------------------------------------------------

void PixelWorkspace::requestExport() {
    if (busy() || ctx_.dialogs.busy() || !idle())
        return;
    exportDialog_.open(document_);
}

// The dialog's choice is kept until the file name comes back. Where the file goes is the native dialog's business;
// what goes into it is exportPng, exportGif or exportSpriteSheet (src/io/pixel_export.hpp).
void PixelWorkspace::beginExport(const PixelExportOptions& options) {
    if (ctx_.dialogs.busy())
        return;
    const std::string stem = utf8FromPath(pathFromUtf8(files_.displayName()).stem());
    NativeDialogs::Tag tag = NativeDialogs::Tag::ExportPng;
    std::string name = stem + ".png";
    if (options.format == PixelExportFormat::Gif) {
        tag = NativeDialogs::Tag::ExportGif;
        name = stem + ".gif";
    } else if (options.format == PixelExportFormat::Sheet) {
        tag = NativeDialogs::Tag::ExportSheet;
        name = stem + "_sheet.png";
    }
    exportRun_ = options;
    if (!ctx_.dialogs.save(tag, name)) {
        exportRun_.reset();
        ctx_.toasts.push(tr(Str::ErrDialogUnavailable), Toasts::Kind::Error);
    }
}

void PixelWorkspace::onNativeDialog(const NativeDialogs::Result& result) {
    if (palettePanel_.onNativeDialog(result))
        return; // a palette to load or save
    if (result.tag != NativeDialogs::Tag::ExportPng && result.tag != NativeDialogs::Tag::ExportGif &&
        result.tag != NativeDialogs::Tag::ExportSheet)
        return;
    if (!exportRun_)
        return; // not ours
    const PixelExportOptions run = *exportRun_;
    exportRun_.reset();
    if (result.cancelled || result.path.empty()) {
        if (!result.error.empty())
            ctx_.toasts.push(result.error, Toasts::Kind::Error);
        return; // cancelled: dropped without a word
    }

    const bool gif = run.format == PixelExportFormat::Gif;
    std::filesystem::path path = pathFromUtf8(result.path);
    std::string extension = utf8FromPath(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != (gif ? ".gif" : ".png"))
        path += gif ? ".gif" : ".png"; // the file type chosen in the dialog decides, so the name says so

    std::string error;
    switch (run.format) {
    case PixelExportFormat::Png:
        error = exportPng(path, document_, document_.activeFrame(), run.scale);
        break;
    case PixelExportFormat::Gif:
        error = exportGif(path, document_, run.firstFrame, run.lastFrame, run.scale, run.gifAlpha());
        break;
    case PixelExportFormat::Sheet: {
        SheetOptions sheet = run.sheet;
        sheet.scale = run.scale;
        error = exportSpriteSheet(path, document_, run.firstFrame, run.lastFrame, sheet);
        break;
    }
    }
    if (error.empty())
        ctx_.toasts.push(trf(Str::ToastSaved, {utf8FromPath(path.filename())}));
    else
        ctx_.toasts.push(localizedExportMessage(error), Toasts::Kind::Error);
}

} // namespace trace
