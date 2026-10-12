#include "app/paint_workspace.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "app/clipboard.hpp"
#include "app/keybinds.hpp"
#include "app/raster_common.hpp"
#include "app/settings.hpp"
#include "app/toasts.hpp"
#include "app/tools_panel.hpp"
#include "core/i18n.hpp"
#include "tools/tool_text.hpp"

namespace trace {

namespace {

constexpr float kToolsPanelFraction = 0.11f;

} // namespace

PaintWorkspace::PaintWorkspace(WorkspaceContext& context)
    : Workspace(context), adapter_(document_, context.settings), files_(adapter_, context.settings, {}) {}

void PaintWorkspace::init(SDL_Window* window, SDL_Renderer* renderer) {
    canvas_.init(renderer);

    FileSession::Hooks hooks;
    hooks.documentReplaced = [this](bool createdBlank) {
        if (createdBlank && ctx_.settings.newDocZoom == Settings::NewDocZoom::ActualSize)
            canvas_.actualSize();
        else
            canvas_.fitToWindow();
    };
    hooks.settingsChanged = [this] { ctx_.settingsDirty = true; };
    hooks.saved = [this](const std::string& name) { ctx_.toasts.push(trf(Str::ToastSaved, {name})); };
    hooks.routeOpen = ctx_.routeOpen;
    files_.setHooks(std::move(hooks));
    if (!files_.init(window))
        SDL_Log("Native file dialogs are unavailable: no SDL event type could be registered");

    toolbox_.settings() = ctx_.settings.toolDefaults;
    toolbox_.select(ctx_.settings.startTool);
    files_.newDocumentNow();
}

// --- windows -----------------------------------------------------------------------------------

ImGuiID PaintWorkspace::dockSpaceId() const {
    return ImHashStr("TraceDockSpace");
}

// Tools on the left, the canvas filling the rest (with its tab bar hidden).
void PaintWorkspace::buildDefaultLayout(ImGuiID dockId, ImVec2 size) {
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

void PaintWorkspace::drawWindows() {
    Settings& settings = ctx_.settings;
    canvas_.setPixelGridEnabled(settings.pixelGrid);
    canvas_.setPatterns(settings.backdropPattern, settings.transparencyPattern, settings.patternSpacing);
    canvas_.setRulersEnabled(settings.showRulers);
    canvas_.setGuidesVisible(settings.showGuides);
    canvas_.setSnapToGuides(settings.snapToGuides);
    document_.setHistoryLimit(static_cast<std::size_t>(settings.historyLimitMb) * 1024u * 1024u);

    if (settings.showToolsPanel) {
        if (ImGui::Begin("Tools", &settings.showToolsPanel))
            drawToolsPanel(toolbox_, settings.keymap);
        ImGui::End();
        ctx_.settingsDirty |= !settings.showToolsPanel; // closed with its title bar button
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags canvasFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("Canvas", nullptr, canvasFlags))
        canvas_.draw(document_, toolbox_);
    ImGui::End();
    ImGui::PopStyleVar();
}

void PaintWorkspace::drawDialogs() {
    files_.drawModals();
    if (const auto outcome = imageDialogs_.draw(document_, toolbox_.settings().secondary)) {
        if (outcome->error)
            files_.report(outcome->text, true);
        else
            ctx_.toasts.push(outcome->text);
    }
}

// --- menus -------------------------------------------------------------------------------------

void PaintWorkspace::drawModeMenus() {
    if (!ImGui::BeginMenu(tr(Str::MenuImage)))
        return;
    const Keymap& keys = ctx_.settings.keymap;
    const bool ready = idle();
    if (actionMenuItem(keys, tr(Str::MenuResize), Action::ResizeImage, nullptr, ready))
        imageDialogs_.openResize(document_);
    if (actionMenuItem(keys, tr(Str::MenuCanvasSize), Action::CanvasSize, nullptr, ready))
        imageDialogs_.openCanvasSize(document_);
    ImGui::Separator();
    if (actionMenuItem(keys, tr(Str::MenuCropToSelection), Action::CropToSelection, nullptr, hasSelection()))
        cropToSelection();
    ImGui::EndMenu();
}

void PaintWorkspace::drawEditMenuExtras() {
    ImGui::Separator();
    if (actionMenuItem(ctx_.settings.keymap, tr(Str::MenuSwapColors), Action::SwapColors))
        toolbox_.swapColors();
}

void PaintWorkspace::drawViewMenuItems() {
    Settings& settings = ctx_.settings;
    drawRasterViewItems(settings, ctx_.settingsDirty, document_);
    if (ImGui::MenuItem(tr(Str::MenuToolsPanel), nullptr, &settings.showToolsPanel))
        ctx_.settingsDirty = true;
}

// --- input -------------------------------------------------------------------------------------

void PaintWorkspace::handleShortcuts(const Keymap& keys) {
    const auto fire = [&](Action action, bool repeat = false) {
        return triggeredIn(keys, action, ModeContext::Paint, repeat);
    };

    // These wait until the current gesture is finished.
    if (idle()) {
        if (fire(Action::ResizeImage))
            imageDialogs_.openResize(document_);
        if (fire(Action::CanvasSize))
            imageDialogs_.openCanvasSize(document_);
        if (fire(Action::CropToSelection))
            cropToSelection();
    }
    handleRasterShortcuts(keys, ModeContext::Paint, ctx_.settings, ctx_.settingsDirty, toolbox_, document_, idle(),
                          ToolSettings::kMaxSize, ToolSettings::kMaxStrokeWidth);
}

// --- status ------------------------------------------------------------------------------------

std::string PaintWorkspace::statusLeft() const {
    char zoom[24];
    std::snprintf(zoom, sizeof(zoom), "%.0f", static_cast<double>(canvas_.zoomPercent()));
    const std::string widthText = std::to_string(document_.image().width());
    const std::string heightText = std::to_string(document_.image().height());
    const char* tool = tr(toolString(toolbox_.current()));
    return canvas_.hasHoverPixel()
               ? trf(Str::StatusHover, {tool, std::to_string(canvas_.hoverX()), std::to_string(canvas_.hoverY()),
                                        widthText, heightText, zoom})
               : trf(Str::StatusIdle, {tool, widthText, heightText, zoom});
}

// --- edit verbs --------------------------------------------------------------------------------

bool PaintWorkspace::canPaste() const {
    return idle() && clipboardHasImage();
}

Rgba8 PaintWorkspace::clearColor() const {
    const ToolSettings& t = toolbox_.settings();
    return t.eraseToTransparent ? Rgba8{} : t.secondary; // what the eraser would leave
}

void PaintWorkspace::copySelection(bool cut) {
    if (copySelectionToClipboard(document_, ctx_.toasts, cut) && cut)
        deleteSelection();
}

void PaintWorkspace::paste() {
    LoadResult result = getClipboardImage();
    if (!result.image) {
        ctx_.toasts.push(result.error, Toasts::Kind::Error);
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

void PaintWorkspace::deleteSelection() {
    if (!document_.selection().active())
        return;
    document_.deleteSelection(clearColor());
    document_.commit();
}

void PaintWorkspace::cropToSelection() {
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

void PaintWorkspace::selectAll() {
    toolbox_.select(ToolId::Select);
    document_.selectAll();
}

} // namespace trace
