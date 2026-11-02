#include "app/paint_workspace.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <new>
#include <string>

#include "app/clipboard.hpp"
#include "app/dock_util.hpp"
#include "app/embedded_fonts.hpp"
#include "app/font_library.hpp"
#include "app/icon_widgets.hpp"
#include "app/keybinds.hpp"
#include "app/raster_common.hpp"
#include "app/settings.hpp"
#include "app/toasts.hpp"
#include "app/tools_panel.hpp"
#include "core/board_export.hpp"
#include "core/board_growth.hpp"
#include "core/i18n.hpp"
#include "io/image_io.hpp"
#include "io/path_utf8.hpp"
#include "tools/tool_text.hpp"

namespace trace {

namespace {

constexpr float kToolsPanelFraction = 0.11f;
constexpr float kLayersPanelFraction = 0.19f;

// The part after ### is the window's identity in the saved layout.
std::string layersWindow() {
    return std::string(tr(Str::WindowLayers)) + "###PaintLayers";
}

constexpr Rgba8 kWhitePaper{255, 255, 255, 255};
constexpr Rgba8 kBlackPaper{0, 0, 0, 255};
constexpr float kBoardViewMargin = 0.25f; // a press grows the board to the view plus this fraction on each side
constexpr int kBoardMinMargin = 256;
constexpr int kBoardReach = 3072;         // never plan further than this from the press
constexpr int kBoardPressPad = 128;

constexpr Rgba8 paperOf(bool dark) noexcept {
    return dark ? kBlackPaper : kWhitePaper;
}
constexpr Rgba8 inkOf(bool dark) noexcept {
    return dark ? kWhitePaper : kBlackPaper;
}

} // namespace

PaintWorkspace::PaintWorkspace(WorkspaceContext& context)
    : Workspace(context), adapter_(document_, context.settings), files_(adapter_, context.settings, {}) {}

void PaintWorkspace::init(SDL_Window* window, SDL_Renderer* renderer) {
    canvas_.init(renderer);
    layers_.init(renderer);

    FileSession::Hooks hooks;
    hooks.documentReplaced = [this](bool createdBlank) {
        layers_.reset();
        if (document_.endless()) {
            // A new or opened whiteboard: the pen must show on its paper, and the view starts centered.
            fixColorsForPaper(adapter_.boardDark());
            if (createdBlank)
                canvas_.actualSize();
            else
                canvas_.fitToWindow(); // an opened board shows what was drawn
            return;
        }
        if (createdBlank && ctx_.settings.newDocZoom == Settings::NewDocZoom::ActualSize)
            canvas_.actualSize();
        else
            canvas_.fitToWindow();
    };
    hooks.settingsChanged = [this] { ctx_.settingsDirty = true; };
    hooks.saved = [this](const std::string& name) { ctx_.toasts.push(trf(Str::ToastSaved, {name})); };
    hooks.routeOpen = ctx_.routeOpen;
    files_.setHooks(std::move(hooks));
    canvas_.setBeforePress([this](Document& doc, Vec2f at) { growBoardForPress(doc, at); });
    if (!files_.init(window))
        SDL_Log("Native file dialogs are unavailable: no SDL event type could be registered");

    fontLibrary().startScan(); // the installed fonts, found in the background while the program starts
    toolbox_.text().setFontResolver([](std::string_view family, std::string_view style) {
        return fontLibrary().bytes(family, style);
    });
    toolbox_.settings() = ctx_.settings.toolDefaults;
    toolbox_.select(ctx_.settings.startTool);
    files_.newDocumentNow();
}

// --- windows -----------------------------------------------------------------------------------

ImGuiID PaintWorkspace::dockSpaceId() const {
    return ImHashStr("TraceDockSpace");
}

// Tools on the left, layers on the right, the canvas in between (with its tab bar hidden).
void PaintWorkspace::buildDefaultLayout(ImGuiID dockId, ImVec2 size) {
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
    ImGui::DockBuilderDockWindow("Tools", left);
    ImGui::DockBuilderDockWindow("Canvas", center);
    ImGui::DockBuilderDockWindow(layersWindow().c_str(), right);
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(center))
        node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    ImGui::DockBuilderFinish(dockId);
}

// An imgui.ini written before the Layers window existed has the dockspace without it, so the window would float; one
// that docked a panel into the canvas's node has that panel hidden behind the canvas. Both are detected once, when
// the mode is first shown: no saved settings for the Layers window, or a panel in the canvas's node, means rebuild.
bool PaintWorkspace::layoutStale(ImGuiID) {
    if (layoutChecked_)
        return false;
    layoutChecked_ = true;
    return ImGui::FindWindowSettingsByID(ImHashStr(layersWindow().c_str())) == nullptr ||
           panelHiddenBehindCanvas("Canvas", {"Tools", layersWindow()});
}

void PaintWorkspace::showAllWindows() {
    ctx_.settings.showToolsPanel = true;
    ctx_.settings.showLayersPanel = true;
    ctx_.settingsDirty = true;
}

void PaintWorkspace::drawWindows() {
    Settings& settings = ctx_.settings;
    canvas_.setPixelGridEnabled(settings.pixelGrid);
    canvas_.setPatterns(settings.backdropPattern, settings.transparencyPattern, settings.patternSpacing);
    canvas_.setBoard(document_.endless(), paperColor());
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
    refuseDockingOverCanvas();
    if (ImGui::Begin("Canvas", nullptr, canvasFlags)) {
        ctx_.toasts.setCanvasRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());
        canvas_.draw(document_, toolbox_);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // A press on a hidden or locked layer did nothing; say why, once per click.
    if (toolbox_.refusalCount() != seenRefusals_) {
        seenRefusals_ = toolbox_.refusalCount();
        Str reason = Str::ToastLayerLocked;
        switch (toolbox_.lastRefusal()) {
        case Toolbox::Refusal::LayerHidden:
            reason = Str::ToastLayerHidden;
            break;
        case Toolbox::Refusal::FillUnbounded:
            reason = Str::ToastFillUnbounded;
            break;
        default:
            break;
        }
        ctx_.toasts.push(tr(reason), Toasts::Kind::Error);
    }

    if (settings.showLayersPanel) {
        // Inert while a gesture or an open text box is in progress, so a layer change never drops the text.
        if (ImGui::Begin(layersWindow().c_str(), &settings.showLayersPanel))
            layers_.draw(document_, !idle(), ctx_.toasts);
        ImGui::End();
        ctx_.settingsDirty |= !settings.showLayersPanel; // closed with its title bar button
    }
}

void PaintWorkspace::drawDialogs() {
    files_.drawModals();
    if (const auto outcome = imageDialogs_.draw(document_, toolbox_.settings().secondary)) {
        if (outcome->error)
            files_.report(outcome->text, true);
        else
            ctx_.toasts.push(outcome->text);
    }
    if (const auto run = boardExport_.draw(document_))
        beginBoardExport(*run);
}

// --- whiteboard export -------------------------------------------------------------------------

void PaintWorkspace::requestExport() {
    if (!document_.endless() || busy() || ctx_.dialogs.busy() || !idle())
        return;
    document_.commit();
    boardExport_.open(document_, canvas_.visibleImageRect());
}

// The dialog's choice is kept until the file name comes back.
void PaintWorkspace::beginBoardExport(const BoardExportRun& run) {
    if (ctx_.dialogs.busy())
        return;
    const bool jpeg = run.options.format == BoardExportFormat::Jpeg;
    const std::string stem = utf8FromPath(pathFromUtf8(files_.displayName()).stem());
    boardRun_ = run;
    if (!ctx_.dialogs.save(jpeg ? NativeDialogs::Tag::ExportJpeg : NativeDialogs::Tag::ExportPng,
                           stem + (jpeg ? ".jpg" : ".png"))) {
        boardRun_.reset();
        ctx_.toasts.push(tr(Str::ErrDialogUnavailable), Toasts::Kind::Error);
    }
}

void PaintWorkspace::onNativeDialog(const NativeDialogs::Result& result) {
    if (result.tag != NativeDialogs::Tag::ExportPng && result.tag != NativeDialogs::Tag::ExportJpeg)
        return;
    if (!boardRun_)
        return; // not ours
    const BoardExportRun run = *boardRun_;
    boardRun_.reset();
    if (result.cancelled || result.path.empty()) {
        if (!result.error.empty())
            ctx_.toasts.push(result.error, Toasts::Kind::Error);
        return; // cancelled: dropped without a word
    }

    const bool jpeg = run.options.format == BoardExportFormat::Jpeg;
    std::filesystem::path path = pathFromUtf8(result.path);
    std::string extension = utf8FromPath(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool named = jpeg ? (extension == ".jpg" || extension == ".jpeg") : extension == ".png";
    if (!named)
        path += jpeg ? ".jpg" : ".png"; // the file type chosen in the dialog decides, so the name says so

    Rgba8 background = paperColor();
    switch (run.options.background) {
    case BoardBackground::Paper:
        break;
    case BoardBackground::White:
        background = kWhitePaper;
        break;
    case BoardBackground::Black:
        background = kBlackPaper;
        break;
    case BoardBackground::Transparent:
        background = Rgba8{};
        break;
    }

    std::string error;
    try {
        document_.commit();
        const Image picture = renderBoardExport(document_, run.plan, background, run.options.smooth);
        if (picture.empty()) {
            error = tr(Str::BoardExpEmpty);
        } else {
            SaveOptions save;
            save.jpegQuality = ctx_.settings.jpegQuality;
            error = saveImage(path, picture, jpeg ? ImageFormat::Jpeg : ImageFormat::Png, save);
        }
    } catch (const std::bad_alloc&) {
        error = tr(Str::BoardExpTooLarge);
    }
    if (error.empty())
        ctx_.toasts.push(trf(Str::ToastSaved, {utf8FromPath(path.filename())}));
    else
        ctx_.toasts.push(error, Toasts::Kind::Error);
}

// --- menus -------------------------------------------------------------------------------------

void PaintWorkspace::drawFileMenuExtras() {
    if (iconMenuItem(Icon::File, tr(Str::MenuNewWhiteboard), nullptr, nullptr, idle() && !files_.busy()))
        newWhiteboard();
}

void PaintWorkspace::drawModeMenus() {
    const bool ready = idle() && !document_.endless(); // a whiteboard grows by itself, so it has no fixed size
    if (ImGui::BeginMenu(tr(Str::MenuImage))) {
        const Keymap& keys = ctx_.settings.keymap;
        if (actionMenuItem(keys, tr(Str::MenuResize), Action::ResizeImage, nullptr, ready))
            imageDialogs_.openResize(document_);
        if (actionMenuItem(keys, tr(Str::MenuCanvasSize), Action::CanvasSize, nullptr, ready))
            imageDialogs_.openCanvasSize(document_);
        ImGui::Separator();
        if (actionMenuItem(keys, Icon::Crop, tr(Str::MenuCropToSelection), Action::CropToSelection, nullptr,
                           hasSelection() && !document_.endless()))
            cropToSelection();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr(Str::MenuLayer))) {
        // No keymap: the New Layer action is bound in Pixel Art only, so its chord would not work here.
        if (const auto command = drawLayerMenuItems(document_, nullptr, ready))
            runCommand(*command);
        ImGui::EndMenu();
    }
}

void PaintWorkspace::runCommand(LayerCommand command) {
    if (idle())
        runLayerCommand(document_, command, ctx_.toasts);
}

void PaintWorkspace::drawEditMenuExtras() {
    ImGui::Separator();
    if (actionMenuItem(ctx_.settings.keymap, Icon::SortVertical, tr(Str::MenuSwapColors), Action::SwapColors))
        toolbox_.swapColors();
}

void PaintWorkspace::drawViewMenuItems() {
    Settings& settings = ctx_.settings;
    drawRasterViewItems(settings, ctx_.settingsDirty, document_);
    if (document_.endless()) {
        if (ImGui::BeginMenu(tr(Str::MenuWhiteboardPaper))) {
            const bool dark = adapter_.boardDark();
            if (ImGui::MenuItem(tr(Str::WordWhite), nullptr, !dark))
                setBoardTheme(false);
            if (ImGui::MenuItem(tr(Str::WordBlack), nullptr, dark))
                setBoardTheme(true);
            ImGui::EndMenu();
        }
    }
    if (iconMenuItem(Icon::SlidersHorizontal, tr(Str::MenuToolsPanel), nullptr, &settings.showToolsPanel))
        ctx_.settingsDirty = true;
    if (iconMenuItem(Icon::AddLayer, tr(Str::MenuLayersPanel), nullptr, &settings.showLayersPanel))
        ctx_.settingsDirty = true;
}

// --- input -------------------------------------------------------------------------------------

void PaintWorkspace::handleShortcuts(const Keymap& keys) {
    const auto fire = [&](Action action, bool repeat = false) {
        return triggeredIn(keys, action, ModeContext::Paint, repeat);
    };

    // These wait until the current gesture is finished.
    if (idle() && !document_.endless()) {
        if (fire(Action::ResizeImage))
            imageDialogs_.openResize(document_);
        if (fire(Action::CanvasSize))
            imageDialogs_.openCanvasSize(document_);
        if (fire(Action::CropToSelection))
            cropToSelection();
    }
    handleRasterShortcuts(keys, ModeContext::Paint, ctx_.settings, ctx_.settingsDirty, toolbox_, document_, idle(),
                          kPaintTools, ToolSettings::kMaxSize, ToolSettings::kMaxStrokeWidth);
}

// --- status ------------------------------------------------------------------------------------

std::string PaintWorkspace::statusLeft() const {
    char zoom[24];
    std::snprintf(zoom, sizeof(zoom), "%.0f", static_cast<double>(canvas_.zoomPercent()));
    const std::string widthText = std::to_string(document_.image().width());
    const std::string heightText = std::to_string(document_.image().height());
    const char* tool = tr(toolString(toolbox_.current()));
    return canvas_.hasHoverPixel()
               ? trf(Str::StatusHover, {tool, std::to_string(canvas_.hoverX() + document_.originX()),
                                        std::to_string(canvas_.hoverY() + document_.originY()), widthText, heightText, zoom})
               : trf(Str::StatusIdle, {tool, widthText, heightText, zoom});
}

// --- edit verbs --------------------------------------------------------------------------------

bool PaintWorkspace::canPaste() const {
    return idle() && clipboardHasImage();
}

Rgba8 PaintWorkspace::clearColor() const {
    const ToolSettings& t = toolbox_.settings();
    return clearsToTransparency(t, document_) ? Rgba8{} : t.secondary; // what the eraser would leave
}

bool PaintWorkspace::ensureEditable() {
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

void PaintWorkspace::copySelection(bool cut) {
    if (cut && !ensureEditable())
        return; // nothing is copied either, so a refused cut leaves the clipboard alone
    if (copySelectionToClipboard(document_, ctx_.toasts, cut) && cut)
        deleteSelection();
}

void PaintWorkspace::paste() {
    if (!ensureEditable())
        return;
    LoadResult result = getClipboardImage();
    if (!result.image) {
        ctx_.toasts.push(result.error, Toasts::Kind::Error);
        return;
    }
    if (document_.endless()) {
        // The middle of the view: a board has no corner to start from.
        const IRect view = canvas_.visibleImageRect();
        const int x = (view.x0 + view.x1 - result.image->width()) / 2;
        const int y = (view.y0 + view.y1 - result.image->height()) / 2;
        placeImage(std::move(*result.image), x, y);
        return;
    }
    // Drop it at the top-left of what is visible, so it always lands where the user is looking.
    const Vec2f origin = canvas_.visibleImageOrigin();
    const int x = std::max(0, static_cast<int>(std::floor(origin.x)));
    const int y = std::max(0, static_cast<int>(std::floor(origin.y)));
    placeImage(std::move(*result.image), x, y);
}

bool PaintWorkspace::placeImage(Image image, int x, int y) {
    if (document_.endless()) {
        // Flatten any floating pixels first (growing needs a settled board), and keep the spot in world terms because
        // growing to the left or top moves the origin.
        document_.clearSelection();
        const int worldX = x + document_.originX();
        const int worldY = y + document_.originY();
        const int w = image.width();
        const int h = image.height();
        ensureBoardRoom(IRect{x - kBoardPressPad, y - kBoardPressPad, x + w + kBoardPressPad, y + h + kBoardPressPad});
        x = worldX - document_.originX();
        y = worldY - document_.originY();
    }
    if (!document_.paste(std::move(image), x, y)) {
        files_.report(tr(Str::ErrPasteMemory), true);
        return false;
    }
    toolbox_.select(ToolId::Select); // so it can be dragged into place straight away
    document_.commit();
    return true;
}

void PaintWorkspace::deleteSelection() {
    if (!document_.selection().active() || !ensureEditable())
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

// --- endless whiteboard ------------------------------------------------------------------------

Rgba8 PaintWorkspace::paperColor() const noexcept {
    return paperOf(adapter_.boardDark());
}

void PaintWorkspace::newWhiteboard() {
    if (idle())
        files_.requestNew(PaintFileAdapter::kNewWhiteboard);
}

void PaintWorkspace::fixColorsForPaper(bool dark) {
    ToolSettings& t = toolbox_.settings();
    if (t.primary == paperOf(dark))
        t.primary = inkOf(dark); // white pen on white paper (or black on black) would draw nothing visible
}

void PaintWorkspace::setBoardTheme(bool dark) {
    if (!document_.endless() || adapter_.boardDark() == dark)
        return;
    ToolSettings& t = toolbox_.settings();
    if (t.primary == inkOf(!dark))
        t.primary = inkOf(dark);
    if (t.secondary == paperOf(!dark))
        t.secondary = paperOf(dark);
    adapter_.setBoardDark(dark);
    ctx_.toasts.push(tr(dark ? Str::WordBlack : Str::WordWhite));
}

bool PaintWorkspace::ensureBoardRoom(const IRect& want) {
    const Image& img = document_.image();
    const BoardGrowth g = planBoardGrowth(img.width(), img.height(), want, document_.layerCount(), document_.frameCount());
    if (g.capped) {
        const double now = ImGui::GetTime();
        if (now - boardLimitToastAt_ > 3.0) {
            boardLimitToastAt_ = now;
            ctx_.toasts.push(trf(Str::ToastBoardLimit, {std::to_string(img.width()), std::to_string(img.height())}),
                             Toasts::Kind::Error);
        }
        return false;
    }
    if (!g.any())
        return true;
    switch (document_.growCanvas(g.left, g.top, g.right, g.bottom)) {
    case Document::OpResult::Applied:
        return true;
    case Document::OpResult::NoMemory:
        ctx_.toasts.push(tr(Str::ErrBoardMemory), Toasts::Kind::Error);
        return false;
    case Document::OpResult::Nothing:
        return false;
    }
    return false;
}

void PaintWorkspace::growBoardForPress(Document& doc, Vec2f at) {
    if (!doc.endless() || toolbox_.textEditing() || doc.selection().floating())
        return;
    constexpr float kLimit = 1.0e7f;
    const int px = static_cast<int>(std::floor(std::clamp(std::isfinite(at.x) ? at.x : 0.0f, -kLimit, kLimit)));
    const int py = static_cast<int>(std::floor(std::clamp(std::isfinite(at.y) ? at.y : 0.0f, -kLimit, kLimit)));
    const IRect view = canvas_.visibleImageRect();
    const int mx = std::max(kBoardMinMargin, static_cast<int>(static_cast<float>(view.width()) * kBoardViewMargin));
    const int my = std::max(kBoardMinMargin, static_cast<int>(static_cast<float>(view.height()) * kBoardViewMargin));
    IRect want{view.x0 - mx, view.y0 - my, view.x1 + mx, view.y1 + my};
    want = intersectionOf(want, IRect{px - kBoardReach, py - kBoardReach, px + kBoardReach, py + kBoardReach});
    want = unionOf(want, IRect{px - kBoardPressPad, py - kBoardPressPad, px + kBoardPressPad, py + kBoardPressPad});
    ensureBoardRoom(want);
}

// --- dropped pictures --------------------------------------------------------------------------

void PaintWorkspace::dropImage(const std::filesystem::path& path, ImVec2 screen, int index) {
    if (!ensureEditable())
        return;
    LoadResult loaded = loadImage(path);
    if (!loaded.image) {
        ctx_.toasts.push(loaded.error, Toasts::Kind::Error);
        return;
    }
    if (loaded.frameCount > 1)
        ctx_.toasts.push(trf(Str::ToastDropGifFirstFrame, {std::to_string(loaded.frameCount)}));

    const int w = loaded.image->width();
    const int h = loaded.image->height();
    // The drop point when it is over the canvas, otherwise the middle of the view; each further file of one drop steps
    // down and right so they do not hide each other.
    Vec2f center;
    if (const std::optional<Vec2f> hit = canvas_.screenToImage(screen.x, screen.y)) {
        center = *hit;
    } else {
        const IRect view = canvas_.visibleImageRect();
        center = Vec2f{static_cast<float>(view.x0 + view.x1) * 0.5f, static_cast<float>(view.y0 + view.y1) * 0.5f};
    }
    const float step = static_cast<float>(index) * 24.0f;
    int x = static_cast<int>(std::floor(center.x + step)) - w / 2;
    int y = static_cast<int>(std::floor(center.y + step)) - h / 2;
    if (!document_.endless()) {
        const Image& img = document_.image();
        if (w > img.width() || h > img.height()) {
            ctx_.toasts.push(tr(Str::ToastDropClipped));
        }
        // Keep it inside when it fits, so the whole picture can be seen and dragged.
        if (w <= img.width())
            x = std::clamp(x, 0, img.width() - w);
        if (h <= img.height())
            y = std::clamp(y, 0, img.height() - h);
    }
    placeImage(std::move(*loaded.image), x, y);
}

} // namespace trace
