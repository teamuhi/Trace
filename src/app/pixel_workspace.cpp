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
#include "app/keybinds.hpp"
#include "app/raster_common.hpp"
#include "app/settings.hpp"
#include "app/toasts.hpp"
#include "app/tools_panel.hpp"
#include "core/i18n.hpp"
#include "io/image_io.hpp"
#include "io/path_utf8.hpp"
#include "tools/tool_text.hpp"

namespace trace {

namespace {

constexpr float kToolsPanelFraction = 0.15f;
constexpr float kLayersPanelFraction = 0.21f;

// The nine tools of Pixel Art mode, in the order of the panel. (Today that is every raster tool; the list is
// here so the panel and the shortcuts never depend on it.)
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

} // namespace

PixelWorkspace::PixelWorkspace(WorkspaceContext& context)
    : Workspace(context),
      imageDialogs_(true),
      adapter_(document_, context.settings),
      files_(adapter_, context.settings, {}) {}

void PixelWorkspace::init(SDL_Window* window, SDL_Renderer* renderer) {
    canvas_.init(renderer);
    canvas_.setPixelArtView(true);
    layers_.init(renderer);

    FileSession::Hooks hooks;
    hooks.documentReplaced = [this](bool) {
        layers_.reset();
        canvas_.fitToWindow(); // a new or opened document always starts fitted to the window
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
}

// --- windows -----------------------------------------------------------------------------------

ImGuiID PixelWorkspace::dockSpaceId() const {
    return ImHashStr("TraceDockSpace.Pixel");
}

// Tools on the left, layers on the right, the canvas in between (with its tab bar hidden).
void PixelWorkspace::buildDefaultLayout(ImGuiID dockId, ImVec2 size) {
    layoutChecked_ = true;
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, size);

    ImGuiID left = 0;
    ImGuiID rest = 0;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, kToolsPanelFraction, &left, &rest);
    ImGuiID right = 0;
    ImGuiID center = 0;
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, kLayersPanelFraction / (1.0f - kToolsPanelFraction), &right, &center);
    ImGui::DockBuilderDockWindow(toolsWindow().c_str(), left);
    ImGui::DockBuilderDockWindow(canvasWindow().c_str(), center);
    ImGui::DockBuilderDockWindow(layersWindow().c_str(), right);
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(center))
        node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    ImGui::DockBuilderFinish(dockId);
}

// An imgui.ini written while this mode was still a placeholder has the dockspace but none of its windows. That
// is detected once, when the mode is first shown: no saved settings for the canvas window means rebuild.
bool PixelWorkspace::layoutStale(ImGuiID) {
    if (layoutChecked_)
        return false;
    layoutChecked_ = true;
    return ImGui::FindWindowSettingsByID(ImHashStr(canvasWindow().c_str())) == nullptr;
}

void PixelWorkspace::syncToolSettings() {
    PixelSettings& px = ctx_.settings.pixel;
    ToolSettings& live = toolbox_.settings();

    // Settings changed from outside (Restore defaults, a reloaded file): the tools follow.
    if (!(px.tools == synced_))
        live = px.tools;

    // Whole pixels only: no antialiasing, the eraser clears, sizes stay inside pixel art's range.
    live.antialias = false;
    live.eraseToTransparent = true;
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
    document_.setHistoryLimit(static_cast<std::size_t>(settings.historyLimitMb) * 1024u * 1024u);

    if (px.showToolsPanel) {
        if (ImGui::Begin(toolsWindow().c_str(), &px.showToolsPanel))
            drawToolsPanel(toolbox_, settings.keymap, kPixelTools, ToolOptions::Pixel);
        ImGui::End();
        ctx_.settingsDirty |= !px.showToolsPanel; // closed with its title bar button
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags canvasFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin(canvasWindow().c_str(), nullptr, canvasFlags))
        canvas_.draw(document_, toolbox_);
    ImGui::End();
    ImGui::PopStyleVar();

    // A press on a hidden or locked layer did nothing; say why, once per click.
    if (toolbox_.refusalCount() != seenRefusals_) {
        seenRefusals_ = toolbox_.refusalCount();
        const bool hidden = toolbox_.lastRefusal() == Toolbox::Refusal::LayerHidden;
        ctx_.toasts.push(tr(hidden ? Str::ToastLayerHidden : Str::ToastLayerLocked), Toasts::Kind::Error);
    }

    if (px.showLayersPanel) {
        if (ImGui::Begin(layersWindow().c_str(), &px.showLayersPanel))
            layers_.draw(document_, toolbox_.gestureActive(), ctx_.toasts);
        ImGui::End();
        ctx_.settingsDirty |= !px.showLayersPanel;
    }
}

void PixelWorkspace::drawDialogs() {
    files_.drawModals();
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
        if (ImGui::MenuItem(tr(Str::MenuCropToSelection), nullptr, false, hasSelection()))
            cropToSelection();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr(Str::MenuLayer))) {
        const auto item = [&](const char* label, LayerCommand command, bool shortcut) {
            const bool enabled = ready && canRunLayerCommand(document_, command);
            const bool clicked = shortcut ? actionMenuItem(keys, label, Action::NewLayer, nullptr, enabled)
                                          : ImGui::MenuItem(label, nullptr, false, enabled);
            if (clicked)
                runCommand(command);
        };
        item(tr(Str::MenuNewLayer), LayerCommand::New, true);
        item(tr(Str::MenuDuplicateLayer), LayerCommand::Duplicate, false);
        item(tr(Str::MenuDeleteLayer), LayerCommand::Delete, false);
        ImGui::Separator();
        item(tr(Str::MenuMergeDown), LayerCommand::MergeDown, false);
        item(tr(Str::MenuFlattenImage), LayerCommand::Flatten, false);
        ImGui::Separator();
        item(tr(Str::MenuMoveLayerUp), LayerCommand::MoveUp, false);
        item(tr(Str::MenuMoveLayerDown), LayerCommand::MoveDown, false);
        ImGui::EndMenu();
    }
}

void PixelWorkspace::drawEditMenuExtras() {
    ImGui::Separator();
    if (actionMenuItem(ctx_.settings.keymap, tr(Str::MenuSwapColors), Action::SwapColors))
        toolbox_.swapColors();
}

void PixelWorkspace::drawViewMenuItems() {
    PixelSettings& px = ctx_.settings.pixel;
    drawRasterViewItems(ctx_.settings, ctx_.settingsDirty, document_);
    if (ImGui::MenuItem(tr(Str::MenuToolsPanel), nullptr, &px.showToolsPanel))
        ctx_.settingsDirty = true;
    if (ImGui::MenuItem(tr(Str::MenuLayersPanel), nullptr, &px.showLayersPanel))
        ctx_.settingsDirty = true;
}

// --- input -------------------------------------------------------------------------------------

void PixelWorkspace::runCommand(LayerCommand command) {
    if (idle())
        runLayerCommand(document_, command, ctx_.toasts);
}

void PixelWorkspace::handleShortcuts(const Keymap& keys) {
    // Layer changes wait until the current gesture is finished.
    if (idle() && triggeredIn(keys, Action::NewLayer, ModeContext::Pixel))
        runCommand(LayerCommand::New);
    handleRasterShortcuts(keys, ModeContext::Pixel, ctx_.settings, ctx_.settingsDirty, toolbox_, document_, idle(),
                          ToolSettings::kMaxPixelSize, ToolSettings::kMaxPixelSize);
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
    const std::filesystem::path stem = pathFromUtf8(files_.displayName()).stem();
    if (!ctx_.dialogs.save(NativeDialogs::Tag::ExportPng, utf8FromPath(stem) + ".png"))
        ctx_.toasts.push(tr(Str::ErrDialogUnavailable), Toasts::Kind::Error);
}

void PixelWorkspace::onNativeDialog(const NativeDialogs::Result& result) {
    if (result.tag != NativeDialogs::Tag::ExportPng)
        return;
    std::filesystem::path path = pathFromUtf8(result.path);
    std::string extension = utf8FromPath(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".png")
        path += ".png"; // the file type chosen in the dialog is PNG, so the name says so
    // The composite of the shown frame, one image pixel per file pixel.
    const std::string error = saveImage(path, document_.displayImage(), ImageFormat::Png);
    if (error.empty())
        ctx_.toasts.push(trf(Str::ToastSaved, {utf8FromPath(path.filename())}));
    else
        files_.report(error, true);
}

} // namespace trace
