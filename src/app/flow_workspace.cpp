#include "app/flow_workspace.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>

#include "app/clipboard.hpp"
#include "app/dock_util.hpp"
#include "app/icon_widgets.hpp"
#include "app/keybinds.hpp"
#include "app/modal_ui.hpp"
#include "app/settings.hpp"
#include "app/theme.hpp"
#include "app/toasts.hpp"
#include "core/i18n.hpp"
#include "flow/flow_layout.hpp"
#include "flow/flow_svg.hpp"
#include "io/file_write.hpp"
#include "io/image_io.hpp"
#include "io/path_utf8.hpp"

namespace trace {

namespace {

constexpr float kShapesPanelFraction = 0.15f;
constexpr float kPropertiesPanelFraction = 0.21f;
constexpr float kDuplicateOffset = 20.0f; // board units; also the step between repeated pastes
constexpr float kImageFitSize = 400.0f;   // an imported picture is shown no larger than this (board units)

// Visible titles are translated; the part after ### is the window's identity in the saved layout.
std::string shapesWindow() {
    return std::string(tr(Str::WindowFlowShapes)) + "###FlowShapes";
}
std::string canvasWindow() {
    return std::string(tr(Str::WindowFlowCanvas)) + "###FlowCanvas";
}
std::string propertiesWindow() {
    return std::string(tr(Str::WindowFlowProperties)) + "###FlowProperties";
}

// Every id in `ids` that exists, plus the children of any container among them (and, with `withGroups`, the other
// members of any group among them), so a container or group is always copied and removed as a whole.
std::unordered_set<flow::NodeId> closure(const flow::FlowDocument& doc, std::span<const flow::NodeId> ids,
                                         bool withGroups) {
    std::unordered_set<flow::NodeId> in;
    std::unordered_set<flow::GroupId> groups;
    const auto add = [&](const flow::Node& n) {
        if (!in.insert(n.id).second)
            return false;
        if (withGroups && n.groupId != flow::kNoGroup)
            groups.insert(n.groupId);
        return true;
    };
    for (const flow::NodeId id : ids) {
        if (const flow::Node* n = doc.node(id))
            add(*n);
    }
    // Repeat until nothing new joins; nesting is shallow, so this takes a pass or two.
    for (bool changed = true; changed;) {
        changed = false;
        for (const flow::Node& n : doc.nodes()) {
            if (in.count(n.id))
                continue;
            const bool child = n.parentId != flow::kNoNode && in.count(n.parentId) != 0;
            const bool member = withGroups && n.groupId != flow::kNoGroup && groups.count(n.groupId) != 0;
            if (child || member)
                changed |= add(n);
        }
    }
    return in;
}

} // namespace

FlowWorkspace::FlowWorkspace(WorkspaceContext& context)
    : Workspace(context), adapter_(state_), files_(adapter_, context.settings, {}) {}

void FlowWorkspace::init(SDL_Window* window, SDL_Renderer* renderer) {
    view_.init(renderer);

    FileSession::Hooks hooks;
    hooks.documentReplaced = [this](bool) {
        view_.reset();
        state_.clearSelection();
        state_.tool = FlowTool::Select;
        pasteStep_ = 0;
        view_.fit(state_); // a new or opened document always starts fitted to the window
    };
    hooks.settingsChanged = [this] { ctx_.settingsDirty = true; };
    hooks.saved = [this](const std::string& name) { ctx_.toasts.push(trf(Str::ToastSaved, {name})); };
    hooks.routeOpen = ctx_.routeOpen;
    files_.setHooks(std::move(hooks));
    adapter_.setExporter([this](const std::filesystem::path& path, ImageFormat format, const SaveOptions& options) {
        return exportFromSave(path, format, options);
    });
    if (!files_.init(window))
        SDL_Log("Native file dialogs are unavailable: no SDL event type could be registered");

    state_.gridSize = static_cast<float>(ctx_.settings.flow.gridSize);
    state_.showGrid = ctx_.settings.flow.showGrid;
    state_.snapToGrid = ctx_.settings.flow.snapToGrid;
    state_.smartGuides = ctx_.settings.flow.smartGuides;
    state_.defaultRoute = ctx_.settings.flow.defaultRoute;
    state_.defaultArrowStart = ctx_.settings.flow.defaultArrowStart;
    state_.defaultArrowEnd = ctx_.settings.flow.defaultArrowEnd;
    synced_ = ctx_.settings.flow;

    files_.newDocumentNow();
}

void FlowWorkspace::syncSettings() {
    FlowSettings& fs = ctx_.settings.flow;
    // What the board holds now, in the shape of the settings.
    FlowSettings live = synced_;
    live.showGrid = state_.showGrid;
    live.snapToGrid = state_.snapToGrid;
    live.smartGuides = state_.smartGuides;
    live.defaultRoute = state_.defaultRoute;
    live.defaultArrowStart = state_.defaultArrowStart;
    live.defaultArrowEnd = state_.defaultArrowEnd;
    if (!(live == synced_)) { // changed on the board: save it
        live.gridSize = fs.gridSize;
        fs = live;
        synced_ = live;
        ctx_.settingsDirty = true;
    } else if (!(fs == synced_)) { // changed in the Settings window: apply it
        state_.gridSize = static_cast<float>(fs.gridSize);
        state_.showGrid = fs.showGrid;
        state_.snapToGrid = fs.snapToGrid;
        state_.smartGuides = fs.smartGuides;
        state_.defaultRoute = fs.defaultRoute;
        state_.defaultArrowStart = fs.defaultArrowStart;
        state_.defaultArrowEnd = fs.defaultArrowEnd;
        synced_ = fs;
    }
}

void FlowWorkspace::release() noexcept {
    exportImage_.discard();
    view_.release();
}

// --- windows -----------------------------------------------------------------------------------

ImGuiID FlowWorkspace::dockSpaceId() const {
    return ImHashStr("TraceDockSpace.Flow");
}

// Shapes on the left, properties on the right, the board in between (with its tab bar hidden).
void FlowWorkspace::buildDefaultLayout(ImGuiID dockId, ImVec2 size) {
    layoutChecked_ = true;
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, size);

    ImGuiID left = 0;
    ImGuiID rest = 0;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, kShapesPanelFraction, &left, &rest);
    ImGuiID right = 0;
    ImGuiID center = 0;
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, kPropertiesPanelFraction / (1.0f - kShapesPanelFraction), &right,
                                &center);
    ImGui::DockBuilderDockWindow(shapesWindow().c_str(), left);
    ImGui::DockBuilderDockWindow(canvasWindow().c_str(), center);
    ImGui::DockBuilderDockWindow(propertiesWindow().c_str(), right);
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(center))
        node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    ImGui::DockBuilderFinish(dockId);
}

// An imgui.ini written while this mode was still a placeholder has the dockspace but only the placeholder window, and
// one that docked a panel into the canvas's node has that panel hidden behind the board. Both are detected once, when
// the mode is first shown: no saved settings for the canvas window, or a panel in its node, means rebuild.
bool FlowWorkspace::layoutStale(ImGuiID) {
    if (layoutChecked_)
        return false;
    layoutChecked_ = true;
    return ImGui::FindWindowSettingsByID(ImHashStr(canvasWindow().c_str())) == nullptr ||
           panelHiddenBehindCanvas(canvasWindow(), {shapesWindow(), propertiesWindow()});
}

void FlowWorkspace::showAllWindows() {
    showShapes_ = true;
    showProperties_ = true;
}

void FlowWorkspace::drawWindows() {
    syncSettings();
    if (state_.pendingTemplate && !files_.busy())
        state_.pendingTemplate.reset(); // New was cancelled at the unsaved-changes prompt: a plain New must stay empty
    const auto limit = static_cast<std::size_t>(std::max(ctx_.settings.historyLimitMb, 1)) * 1024u * 1024u;
    if (state_.history.maxBytes() != limit)
        state_.history.setMaxBytes(limit);

    if (state_.importImageRequested) {
        state_.importImageRequested = false;
        if (state_.tool == FlowTool::Image)
            state_.tool = FlowTool::Select; // the dialog is the tool's whole job
        requestImport();
    }

    if (showShapes_) {
        if (ImGui::Begin(shapesWindow().c_str(), &showShapes_))
            shapesPanel_.draw(state_, ctx_.settings.keymap);
        ImGui::End();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    constexpr ImGuiWindowFlags canvasFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    refuseDockingOverCanvas();
    if (ImGui::Begin(canvasWindow().c_str(), nullptr, canvasFlags)) {
        ctx_.toasts.setCanvasRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());
        view_.draw(state_);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (showProperties_) {
        if (ImGui::Begin(propertiesWindow().c_str(), &showProperties_))
            propertiesPanel_.draw(state_);
        ImGui::End();
    }
}

void FlowWorkspace::drawDialogs() {
    files_.drawModals();
    if (askDelete_)
        drawDeletePrompt();
    if (exportAsk_)
        drawExportDialog();
    recordPngExport(); // does nothing unless a PNG export is queued
}

// "Delete contents too?": asked when a selected container still holds shapes that are not selected themselves.
void FlowWorkspace::drawDeletePrompt() {
    const std::string title = std::string(tr(Str::FlowDeleteTitle)) + "###FlowDeleteContents";
    if (!deleteShown_) {
        ImGui::OpenPopup(title.c_str());
        deleteShown_ = true;
    }
    if (!modal::begin(title.c_str(), 24.0f)) {
        askDelete_ = false; // the popup went away by itself: nothing is asked any more
        deleteShown_ = false;
        return;
    }
    modal::paragraph(tr(Str::FlowDeleteBody));

    // Keeping the contents loses nothing, so it is the default; deleting them is the red one.
    const int pressed = modal::footer({{tr(Str::FlowDeleteContents), modal::ButtonKind::Danger},
                                       {tr(Str::WordCancel), modal::ButtonKind::Secondary, true, false, true},
                                       {tr(Str::FlowKeepContents), modal::ButtonKind::Primary, true, true}});
    if (pressed >= 0) {
        ImGui::CloseCurrentPopup();
        askDelete_ = false;
        deleteShown_ = false;
        if (pressed != 1)
            removeSelection(pressed == 0);
    }
    modal::end();
}

// --- menus -------------------------------------------------------------------------------------

void FlowWorkspace::drawFileMenuExtras() {
    struct Item {
        Str label;
        flow::TemplateKind kind;
    };
    static constexpr std::array<Item, 3> kItems = {{{Str::FlowTemplateProcess, flow::TemplateKind::Process},
                                                    {Str::FlowTemplateDecisionTree, flow::TemplateKind::DecisionTree},
                                                    {Str::FlowTemplateSwimlane, flow::TemplateKind::Swimlane}}};
    if (!ImGui::BeginMenu(tr(Str::MenuNewFromTemplate), files_.hasDocument() && !files_.busy()))
        return;
    for (const Item& item : kItems) {
        if (ImGui::MenuItem(tr(item.label))) {
            // New asks about unsaved changes first; the adapter picks the template up when the new board is made.
            state_.pendingTemplate = item.kind;
            files_.requestNew();
        }
    }
    ImGui::EndMenu();
}

void FlowWorkspace::autoLayoutBoard() {
    if (!idle())
        return;
    // Two or more selected shapes are arranged among themselves, otherwise the whole board.
    std::vector<flow::NodeId> ids;
    if (state_.selNodes.size() >= 2)
        ids = state_.selNodes;
    const flow::LayoutDirection direction =
        state_.layoutLeftRight ? flow::LayoutDirection::LeftRight : flow::LayoutDirection::TopDown;
    if (flow::autoLayout(state_.doc, ids, direction))
        commit();
    else
        ctx_.toasts.push(tr(Str::ToastFlowLayoutNothing));
}

void FlowWorkspace::drawModeMenus() {
    if (!ImGui::BeginMenu(tr(Str::MenuArrange)))
        return;
    const Keymap& keys = ctx_.settings.keymap;
    const std::size_t count = state_.selNodes.size();
    const bool any = idle() && count >= 1;

    const auto order = [&](Icon icon, Str label, Action action, Arrange op) {
        if (actionMenuItem(keys, icon, tr(label), action, nullptr, any))
            arrange(op);
    };
    order(Icon::ItemForward, Str::ActionBringForward, Action::BringForward, Arrange::Forward);
    order(Icon::ItemBackward, Str::ActionSendBackward, Action::SendBackward, Arrange::Backward);
    order(Icon::FlowchartBringForward, Str::ActionBringToFront, Action::BringToFront, Arrange::Front);
    order(Icon::FlowchartBringBack, Str::ActionSendToBack, Action::SendToBack, Arrange::Back);
    ImGui::Separator();
    if (actionMenuItem(keys, Icon::LinkAdd, tr(Str::ActionGroupNodes), Action::GroupNodes, nullptr, canGroup()))
        arrange(Arrange::Group);
    if (actionMenuItem(keys, Icon::LinkBroken, tr(Str::ActionUngroupNodes), Action::UngroupNodes, nullptr, canUngroup()))
        arrange(Arrange::Ungroup);
    ImGui::Separator();

    // Align and Match Size need two shapes, Distribute three (a group counts as one shape for the layout).
    const bool two = idle() && count >= 2;
    const bool three = idle() && count >= 3;
    if (ImGui::BeginMenu(tr(Str::MenuFlowAlign), two)) {
        // align_center.svg points two arrows at a horizontal line, which is the vertical centering of Middle; turned
        // a quarter, it is the horizontal centering of Center.
        struct Item {
            Str label;
            flow::AlignMode mode;
            Icon icon;
            int quarterTurns;
        };
        static constexpr std::array<Item, 6> items = {{{Str::MenuFlowLeft, flow::AlignMode::Left, Icon::AlignLeft, 0},
                                                       {Str::MenuFlowCenter, flow::AlignMode::Center, Icon::AlignCenter, 1},
                                                       {Str::MenuFlowRight, flow::AlignMode::Right, Icon::AlignRight, 0},
                                                       {Str::MenuFlowTop, flow::AlignMode::Top, Icon::AlignTop, 0},
                                                       {Str::MenuFlowMiddle, flow::AlignMode::Middle, Icon::AlignCenter, 0},
                                                       {Str::MenuFlowBottom, flow::AlignMode::Bottom, Icon::AlignBottom, 0}}};
        for (const Item& item : items) {
            if (iconMenuItem(item.icon, tr(item.label), nullptr, nullptr, true, item.quarterTurns))
                editSelection([mode = item.mode](flow::FlowDocument& doc, std::span<const flow::NodeId> ids) {
                    return flow::alignNodes(doc, ids, mode);
                });
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr(Str::MenuFlowDistribute), three)) {
        if (iconMenuItem(Icon::ArrowGap, tr(Str::MenuFlowHorizontally)))
            editSelection([](flow::FlowDocument& doc, std::span<const flow::NodeId> ids) {
                return flow::distributeNodes(doc, ids, flow::DistributeAxis::Horizontal);
            });
        if (iconMenuItem(Icon::ArrowGap, tr(Str::MenuFlowVertically), nullptr, nullptr, true, 1))
            editSelection([](flow::FlowDocument& doc, std::span<const flow::NodeId> ids) {
                return flow::distributeNodes(doc, ids, flow::DistributeAxis::Vertical);
            });
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr(Str::MenuFlowMatchSize), two)) {
        if (ImGui::MenuItem(tr(Str::MenuFlowWidth)))
            editSelection([](flow::FlowDocument& doc, std::span<const flow::NodeId> ids) {
                return flow::matchSize(doc, ids, flow::MatchMode::Width);
            });
        if (ImGui::MenuItem(tr(Str::MenuFlowHeight)))
            editSelection([](flow::FlowDocument& doc, std::span<const flow::NodeId> ids) {
                return flow::matchSize(doc, ids, flow::MatchMode::Height);
            });
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (actionMenuItem(keys, Icon::SortVertical, tr(Str::ActionAutoLayout), Action::AutoLayout, nullptr,
                       idle() && state_.doc.nodes().size() >= 2))
        autoLayoutBoard();
    if (ImGui::BeginMenu(tr(Str::MenuFlowLayoutDirection))) {
        if (ImGui::MenuItem(tr(Str::MenuFlowTopDown), nullptr, !state_.layoutLeftRight))
            state_.layoutLeftRight = false;
        if (ImGui::MenuItem(tr(Str::MenuFlowLeftRight), nullptr, state_.layoutLeftRight))
            state_.layoutLeftRight = true;
        ImGui::EndMenu();
    }
    ImGui::Separator();

    // Connectors: the route of the selected connectors, or of every connector when none is selected, and
    // color coding by the shape each one leaves.
    if (ImGui::BeginMenu(tr(Str::MenuFlowConnectors), idle() && !state_.doc.edges().empty())) {
        std::vector<flow::EdgeId> targets = state_.selEdges;
        if (targets.empty()) {
            for (const flow::Edge& edge : state_.doc.edges())
                targets.push_back(edge.id);
        }
        struct Item {
            Str label;
            flow::Route route;
        };
        static constexpr std::array<Item, 3> routes = {{{Str::FlowRouteCurved, flow::Route::Curved},
                                                        {Str::FlowRouteElbow, flow::Route::Elbow},
                                                        {Str::FlowRouteStraight, flow::Route::Straight}}};
        for (const Item& item : routes) {
            if (!ImGui::MenuItem(tr(item.label)))
                continue;
            for (const flow::EdgeId id : targets) {
                if (const flow::Edge* edge = state_.doc.edge(id))
                    state_.doc.setEdgeRoute(id, item.route, edge->arrowStart, edge->arrowEnd);
            }
            commit();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr(Str::MenuFlowColorBySource))) {
            for (const flow::EdgeId id : targets) {
                const flow::Edge* edge = state_.doc.edge(id);
                const flow::Node* from = edge ? state_.doc.node(edge->from.node) : nullptr;
                if (!from)
                    continue;
                const std::optional<Rgba8> color = from->style.fill ? from->style.fill : from->style.stroke;
                if (!color)
                    continue;
                flow::Style style = edge->style;
                style.stroke = Rgba8{color->r, color->g, color->b, 255};
                state_.doc.setEdgeStyle(id, style);
            }
            commit();
        }
        ImGui::EndMenu();
    }
    ImGui::EndMenu();
}

void FlowWorkspace::drawEditMenuExtras() {
    ImGui::Separator();
    if (actionMenuItem(ctx_.settings.keymap, Icon::Duplicate, tr(Str::ActionDuplicate), Action::Duplicate, nullptr,
                       canCopy()))
        duplicateSelection();
}

void FlowWorkspace::drawViewMenuItems() {
    iconMenuItem(Icon::BorderInner, tr(Str::MenuFlowShowGrid), nullptr, &state_.showGrid);
    iconMenuItem(Icon::Anchor, tr(Str::MenuFlowSnapToGrid), nullptr, &state_.snapToGrid);
    iconMenuItem(Icon::SingleLine, tr(Str::MenuFlowSmartGuides), nullptr, &state_.smartGuides);
    iconMenuItem(Icon::InsertShapes, tr(Str::MenuFlowShapesPanel), nullptr, &showShapes_);
    iconMenuItem(Icon::SlidersHorizontal, tr(Str::MenuFlowPropertiesPanel), nullptr, &showProperties_);
}

// --- input -------------------------------------------------------------------------------------

void FlowWorkspace::handleShortcuts(const Keymap& keys) {
    // Nothing fires while a shape is dragged, the label editor is open or a prompt is showing.
    if (!idle() || busy())
        return;
    const auto fire = [&](Action action) { return triggeredIn(keys, action, ModeContext::Flow); };

    struct ToolKey {
        Action action;
        FlowTool tool;
        flow::NodeKind kind; // for FlowTool::Shape
    };
    static constexpr std::array<ToolKey, 10> kTools = {{
        {Action::ToolFlowSelect, FlowTool::Select, flow::NodeKind::Rectangle},
        {Action::ToolFlowHand, FlowTool::Hand, flow::NodeKind::Rectangle},
        {Action::ToolFlowConnector, FlowTool::Connector, flow::NodeKind::Rectangle},
        {Action::ToolFlowText, FlowTool::Text, flow::NodeKind::Rectangle},
        {Action::ToolFlowSticky, FlowTool::Sticky, flow::NodeKind::Rectangle},
        {Action::ToolFlowImage, FlowTool::Image, flow::NodeKind::Rectangle},
        {Action::ShapeRectangle, FlowTool::Shape, flow::NodeKind::Rectangle},
        {Action::ShapeEllipse, FlowTool::Shape, flow::NodeKind::Ellipse},
        {Action::ShapeDiamond, FlowTool::Shape, flow::NodeKind::Diamond},
        {Action::ShapeContainer, FlowTool::Shape, flow::NodeKind::Container},
    }};
    for (const ToolKey& key : kTools) {
        if (!fire(key.action))
            continue;
        if (key.tool == FlowTool::Image) {
            state_.importImageRequested = true; // handled by drawWindows(); the tool stays as it was
        } else {
            state_.tool = key.tool;
            if (key.tool == FlowTool::Shape)
                state_.shapeKind = key.kind;
        }
        return;
    }

    if (fire(Action::Duplicate) && canCopy())
        duplicateSelection();
    if (fire(Action::BringForward))
        arrange(Arrange::Forward);
    if (fire(Action::SendBackward))
        arrange(Arrange::Backward);
    if (fire(Action::BringToFront))
        arrange(Arrange::Front);
    if (fire(Action::SendToBack))
        arrange(Arrange::Back);
    if (fire(Action::GroupNodes))
        arrange(Arrange::Group);
    if (fire(Action::UngroupNodes))
        arrange(Arrange::Ungroup);
    if (fire(Action::AutoLayout))
        autoLayoutBoard();
}

void FlowWorkspace::requestImport() {
    if (busy() || ctx_.dialogs.busy())
        return;
    if (!ctx_.dialogs.open(NativeDialogs::Tag::ImportImage))
        ctx_.toasts.push(tr(Str::ErrDialogUnavailable), Toasts::Kind::Error);
}

void FlowWorkspace::onNativeDialog(const NativeDialogs::Result& result) {
    if (result.tag == NativeDialogs::Tag::ExportPng || result.tag == NativeDialogs::Tag::ExportSvg) {
        onExportDialog(result);
        return;
    }
    if (result.tag != NativeDialogs::Tag::ImportImage)
        return;
    LoadResult loaded = loadImage(pathFromUtf8(result.path));
    if (!loaded.image) {
        ctx_.toasts.push(loaded.error, Toasts::Kind::Error);
        return;
    }
    addImageNode(std::move(*loaded.image), view_.viewCenter());
}

// --- status ------------------------------------------------------------------------------------

std::string FlowWorkspace::statusLeft() const {
    char zoom[24];
    std::snprintf(zoom, sizeof(zoom), "%.0f", static_cast<double>(view_.zoomPercent()));
    return trf(Str::StatusFlow, {zoom, std::to_string(state_.doc.nodes().size()),
                                 std::to_string(state_.doc.edges().size()),
                                 std::to_string(state_.selNodes.size() + state_.selEdges.size())});
}

// --- edit verbs --------------------------------------------------------------------------------

void FlowWorkspace::commit() {
    try {
        state_.commit();
    } catch (const std::exception&) {
        // The change is in the document but not in the history; the next commit records it.
        ctx_.toasts.push(tr(Str::ErrLayerMemory), Toasts::Kind::Error);
    }
}

void FlowWorkspace::undo() {
    if (!canUndo())
        return;
    if (const flow::FlowDocument* previous = state_.history.undo())
        state_.replaceDocument(*previous);
}

void FlowWorkspace::redo() {
    if (!canRedo())
        return;
    if (const flow::FlowDocument* next = state_.history.redo())
        state_.replaceDocument(*next);
}

bool FlowWorkspace::canPaste() const {
    return idle() && (!clip_.empty() || clipboardHasImage());
}

FlowWorkspace::Clip FlowWorkspace::capture(std::span<const flow::NodeId> ids) const {
    const flow::FlowDocument& doc = state_.doc;
    const std::unordered_set<flow::NodeId> in = closure(doc, ids, true);
    Clip clip;
    std::vector<flow::NodeId> kept;
    for (const flow::NodeId id : doc.nodesByZ()) { // back to front, so pasting keeps the stacking order
        if (!in.count(id))
            continue;
        const flow::Node* node = doc.node(id);
        clip.nodes.push_back(*node);
        kept.push_back(id);
        if (node->imageId != flow::kNoImage) {
            if (flow::ImagePtr image = doc.image(node->imageId))
                clip.images.emplace(node->imageId, std::move(image));
        }
    }
    // Connectors that stay within the copied shapes; a free end comes along with the connector.
    const auto inside = [&](const flow::End& end) { return end.node == flow::kNoNode || in.count(end.node) != 0; };
    for (const flow::Edge& edge : doc.edges()) {
        const bool touches = edge.from.node != flow::kNoNode || edge.to.node != flow::kNoNode;
        if (touches && inside(edge.from) && inside(edge.to))
            clip.edges.push_back(edge);
    }
    clip.bounds = doc.boundsOf(kept);
    return clip;
}

FlowWorkspace::Inserted FlowWorkspace::insertClip(flow::FlowDocument& doc, const Clip& clip, float dx, float dy,
                                                  std::vector<flow::NodeId>& created) {
    using namespace flow;
    std::unordered_map<NodeId, NodeId> nodeMap;
    std::unordered_map<ImageId, ImageId> imageMap;
    std::unordered_map<GroupId, std::vector<NodeId>> groups;

    for (const Node& source : clip.nodes) {
        Rect rect = source.rect;
        rect.x += dx;
        rect.y += dy;
        const NodeId id = doc.addNode(source.kind, rect, source.text, source.style, source.sides);
        if (id == kNoNode)
            return Inserted::Full;
        nodeMap.emplace(source.id, id);
        created.push_back(id);
        if (source.imageId != kNoImage) {
            const auto shared = clip.images.find(source.imageId);
            if (shared != clip.images.end()) {
                auto mapped = imageMap.find(source.imageId);
                if (mapped == imageMap.end()) {
                    const ImageId image = doc.addImage(shared->second);
                    if (image == kNoImage)
                        return Inserted::TooManyImages;
                    mapped = imageMap.emplace(source.imageId, image).first;
                }
                doc.setNodeImage(id, mapped->second);
            }
        }
        if (source.groupId != kNoGroup)
            groups[source.groupId].push_back(id);
    }
    // Parent links and groups only survive inside the copied set; the rest of the document is not touched.
    for (const Node& source : clip.nodes) {
        const auto parent = nodeMap.find(source.parentId);
        if (source.parentId != kNoNode && parent != nodeMap.end())
            doc.reparent(nodeMap.at(source.id), parent->second);
    }
    for (const auto& entry : groups) {
        if (entry.second.size() >= 2)
            doc.group(entry.second);
    }
    for (const Edge& source : clip.edges) {
        const auto remap = [&](End end) {
            if (end.node == kNoNode) {
                end.point.x += dx;
                end.point.y += dy;
            } else {
                end.node = nodeMap.at(end.node); // capture() only keeps edges whose ends were copied
            }
            return end;
        };
        const EdgeId id = doc.connect(remap(source.from), remap(source.to));
        if (id == kNoEdge)
            return Inserted::Full;
        doc.setEdgeRoute(id, source.route, source.arrowStart, source.arrowEnd);
        doc.setEdgeVia(id, source.via);
        doc.setEdgeLabel(id, source.label);
        doc.setEdgeLabelPlacement(id, source.labelPos, source.labelSide, source.labelAlong);
        doc.setEdgeStyle(id, source.style);
    }
    return Inserted::Ok;
}

bool FlowWorkspace::insertAndSelect(const Clip& clip, float dx, float dy) {
    flow::FlowDocument next = state_.doc; // work on a copy, so a refusal halfway leaves the board untouched
    std::vector<flow::NodeId> created;
    switch (insertClip(next, clip, dx, dy, created)) {
    case Inserted::Ok:
        break;
    case Inserted::Full:
        ctx_.toasts.push(tr(Str::ErrFlowFull), Toasts::Kind::Error);
        return false;
    case Inserted::TooManyImages:
        ctx_.toasts.push(trf(Str::ErrFlowImages, {std::to_string(flow::kMaxImages)}), Toasts::Kind::Error);
        return false;
    }
    state_.doc = std::move(next);
    state_.clearSelection();
    state_.selNodes = std::move(created);
    commit();
    return true;
}

void FlowWorkspace::copy() {
    if (!canCopy())
        return;
    clip_ = capture(state_.selNodes);
    pasteStep_ = 0;
    lastPaste_ = flow::Pt{3.0f * flow::kMaxCoord, 3.0f * flow::kMaxCoord}; // the next paste lands on the pointer
    // The newest copy wins over a picture that was already on the system clipboard.
    SDL_ClearClipboardData();
    clipAlive_.reset();
    queueClipboardRaster(false);
    ctx_.toasts.push(tr(Str::ToastCopied));
}

void FlowWorkspace::cut() {
    if (!canCopy())
        return;
    copy();
    queueClipboardRaster(true); // the shapes are gone by the time the picture is drawn, so draw it from a copy
    removeSelection(true); // the children of a cut container are in the clipboard, so they go too
}

void FlowWorkspace::paste() {
    if (!idle())
        return;
    // A picture on the system clipboard is newer than the internal clipboard (copying inside Trace clears it), unless
    // it is the raster Copy itself put there: then the shapes are inserted.
    if (clipboardHasImage() && !clipboardIsOurs()) {
        pasteSystemImage();
        return;
    }
    if (clip_.empty())
        return;
    const flow::Pt at = view_.pointerOrCenter();
    if (std::abs(at.x - lastPaste_.x) < 1.0f && std::abs(at.y - lastPaste_.y) < 1.0f)
        ++pasteStep_; // pasting again in the same place: step aside so copies do not hide each other
    else
        pasteStep_ = 0;
    lastPaste_ = at;
    const float shift = kDuplicateOffset * static_cast<float>(pasteStep_);
    // The top-left of the copied shapes lands on the target.
    if (insertAndSelect(clip_, at.x - clip_.bounds.x + shift, at.y - clip_.bounds.y + shift))
        state_.tool = FlowTool::Select;
}

// Copy and Cut also leave a picture of the shapes on the system clipboard, so they can be pasted into Paint, Pixel Art
// or another program. It goes through the PNG export (recorded in the next rendered frame, drawn in afterRender): a
// transparent background, x2, the selected shapes and what containers hold, light-theme Auto colors.
void FlowWorkspace::queueClipboardRaster(bool snapshot) {
    const bool replacing = exportStage_ == ExportStage::Queued && exportToClipboard_; // Cut after its own Copy
    if (exportStage_ != ExportStage::Idle && !replacing)
        return;
    cancelExport();
    if (snapshot) {
        try {
            exportDoc_ = state_.doc;
        } catch (const std::exception&) {
            ctx_.toasts.push(tr(Str::ErrImageMemory), Toasts::Kind::Error);
            return; // the shapes still paste inside Flowchart
        }
    }
    exportRun_ = FlowExportOptions{};
    exportRun_.scale = 2;
    exportRun_.background = FlowExportBackground::Transparent;
    exportRun_.selectionOnly = true;
    exportRun_.padding = 4;
    exportOnly_ = state_.selNodes;
    exportToClipboard_ = true;
    exportStage_ = ExportStage::Queued;
}

void FlowWorkspace::pasteSystemImage() {
    LoadResult loaded = getClipboardImage();
    if (!loaded.image) {
        ctx_.toasts.push(loaded.error, Toasts::Kind::Error);
        return;
    }
    addImageNode(std::move(*loaded.image), view_.pointerOrCenter());
}

// A dragged-in picture becomes an Image node under the drop point (the middle of the board view when the point is
// elsewhere). An animated GIF contributes its first frame.
void FlowWorkspace::dropImage(const std::filesystem::path& path, ImVec2 screen, int index) {
    LoadResult loaded = loadImage(path);
    if (!loaded.image) {
        ctx_.toasts.push(loaded.error, Toasts::Kind::Error);
        return;
    }
    if (loaded.frameCount > 1)
        ctx_.toasts.push(trf(Str::ToastDropGifFirstFrame, {std::to_string(loaded.frameCount)}));
    flow::Pt at = view_.screenOrCenter(screen);
    const float step = kDuplicateOffset * static_cast<float>(index);
    at.x += step;
    at.y += step;
    addImageNode(std::move(*loaded.image), at);
}

// A picture becomes an Image node centred on `center`, shown at its own size or scaled down to fit 400 board
// units; the pixels are stored as they are.
bool FlowWorkspace::addImageNode(Image image, flow::Pt center) {
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0)
        return false;
    try {
        const auto pixels = std::make_shared<const Image>(std::move(image));
        const float scale = std::min(1.0f, kImageFitSize / static_cast<float>(std::max(width, height)));
        const float w = std::max(static_cast<float>(width) * scale, flow::kMinNodeSize);
        const float h = std::max(static_cast<float>(height) * scale, flow::kMinNodeSize);

        flow::FlowDocument next = state_.doc;
        const flow::ImageId imageId = next.addImage(pixels);
        if (imageId == flow::kNoImage) {
            ctx_.toasts.push(trf(Str::ErrFlowImages, {std::to_string(flow::kMaxImages)}), Toasts::Kind::Error);
            return false;
        }
        const flow::NodeId id =
            next.addNode(flow::NodeKind::Image, flow::Rect{center.x - w * 0.5f, center.y - h * 0.5f, w, h});
        if (id == flow::kNoNode) {
            ctx_.toasts.push(tr(Str::ErrFlowFull), Toasts::Kind::Error);
            return false;
        }
        next.setNodeImage(id, imageId);
        state_.doc = std::move(next);
        state_.selectOnlyNode(id);
        state_.tool = FlowTool::Select;
        commit();
        return true;
    } catch (const std::exception&) {
        ctx_.toasts.push(tr(Str::ErrImageMemory), Toasts::Kind::Error);
        return false;
    }
}

void FlowWorkspace::duplicateSelection() {
    if (!canCopy())
        return;
    const Clip clip = capture(state_.selNodes); // the internal clipboard is left alone
    insertAndSelect(clip, kDuplicateOffset, kDuplicateOffset);
}

void FlowWorkspace::selectAll() {
    if (!canSelectAll())
        return;
    state_.clearSelection();
    for (const flow::Node& node : state_.doc.nodes())
        state_.selNodes.push_back(node.id);
    for (const flow::Edge& edge : state_.doc.edges())
        state_.selEdges.push_back(edge.id);
}

void FlowWorkspace::deselect() {
    if (idle())
        state_.clearSelection();
}

bool FlowWorkspace::selectionHasFilledContainer() const {
    for (const flow::NodeId id : state_.selNodes) {
        const flow::Node* node = state_.doc.node(id);
        if (!node || node->kind != flow::NodeKind::Container)
            continue;
        for (const flow::NodeId child : state_.doc.childrenOf(id)) {
            if (!state_.isNodeSelected(child))
                return true; // a child that is selected too goes anyway; only a bystander needs asking about
        }
    }
    return false;
}

void FlowWorkspace::deleteSelection() {
    if (!canDeleteSelection())
        return;
    if (selectionHasFilledContainer())
        askDelete_ = true; // drawDeletePrompt() asks; its answer calls removeSelection()
    else
        removeSelection(false);
}

void FlowWorkspace::removeSelection(bool withContents) {
    std::vector<flow::NodeId> nodes = state_.selNodes;
    if (withContents) {
        const std::unordered_set<flow::NodeId> all = closure(state_.doc, nodes, false);
        nodes.assign(all.begin(), all.end());
    }
    state_.doc.removeEdges(state_.selEdges); // the connectors of removed shapes go with the shapes
    state_.doc.removeNodes(nodes);           // without contents, children of a container are detached, not removed
    state_.clearSelection();
    commit();
}

// --- arrange -----------------------------------------------------------------------------------

void FlowWorkspace::editSelection(const Edit& edit) {
    if (!idle() || state_.selNodes.empty())
        return;
    const std::vector<flow::NodeId> ids = state_.selNodes;
    if (edit(state_.doc, ids))
        commit();
}

bool FlowWorkspace::canUngroup() const noexcept {
    if (!idle())
        return false;
    return std::any_of(state_.selNodes.begin(), state_.selNodes.end(), [&](flow::NodeId id) {
        const flow::Node* node = state_.doc.node(id);
        return node && node->groupId != flow::kNoGroup;
    });
}

void FlowWorkspace::arrange(Arrange op) {
    editSelection([op](flow::FlowDocument& doc, std::span<const flow::NodeId> ids) {
        switch (op) {
        case Arrange::Forward:
            return doc.bringForward(ids);
        case Arrange::Backward:
            return doc.sendBackward(ids);
        case Arrange::Front:
            return doc.bringToFront(ids);
        case Arrange::Back:
            return doc.sendToBack(ids);
        case Arrange::Group:
            return doc.group(ids) != flow::kNoGroup;
        case Arrange::Ungroup: {
            std::vector<flow::GroupId> groups;
            for (const flow::NodeId id : ids) {
                const flow::Node* node = doc.node(id);
                if (node && node->groupId != flow::kNoGroup &&
                    std::find(groups.begin(), groups.end(), node->groupId) == groups.end())
                    groups.push_back(node->groupId);
            }
            bool changed = false;
            for (const flow::GroupId group : groups)
                changed |= doc.ungroup(group);
            return changed;
        }
        }
        return false;
    });
}

// --- export ------------------------------------------------------------------------------------
// Export is a flow of its own: the dialog collects the options, the native save dialog names the file, and then
// SVG is written at once while PNG is queued. A queued PNG is recorded into a standalone draw list during the next
// rendered frame (drawDialogs) and rendered to pixels by afterRender, once the App has drawn that frame (flow_export.hpp).

namespace {

// The colors an export is drawn with. White and Transparent use the light theme's Auto colors (the chart is
// meant for a light page); Board color uses the current theme's, on its board color.
struct ExportColors {
    std::optional<Rgba8> background; // nullopt = transparent
    Rgba8 ink;
    Rgba8 paper;
    Rgba8 board; // what a label that sits on its line is plated with
};

Rgba8 rgba8(const ImVec4& v) {
    const auto channel = [](float f) { return static_cast<std::uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return Rgba8{channel(v.x), channel(v.y), channel(v.z), 255};
}

ExportColors exportColors(FlowExportBackground background) {
    const ThemeTokens& t = background == FlowExportBackground::Board ? tokens() : baseTokens(false);
    ExportColors c;
    c.ink = rgba8(t.flowInkAuto);
    c.paper = rgba8(t.flowPaperAuto);
    c.board = background == FlowExportBackground::Board ? rgba8(t.flowBoard) : Rgba8{255, 255, 255, 255};
    if (background != FlowExportBackground::Transparent)
        c.background = c.board;
    return c;
}

// The name SVG text is set in: the font the interface uses, when it has a name outside the program.
std::string exportFontFamily(const Settings& settings) {
    switch (settings.font) {
    case FontChoice::Inter: return "Inter";
    case FontChoice::Mono: return "JetBrains Mono, monospace";
    case FontChoice::Custom: {
        const std::string stem = utf8FromPath(pathFromUtf8(settings.customFontPath).stem());
        return stem.empty() ? std::string("sans-serif") : stem;
    }
    case FontChoice::Classic: break;
    }
    return "sans-serif";
}

// The shapes an export of the selection draws: the selected ones that still exist, and what a container holds.
std::vector<flow::NodeId> selectedShapes(const flow::FlowDocument& doc, std::span<const flow::NodeId> selected) {
    const std::unordered_set<flow::NodeId> in = closure(doc, selected, false);
    return std::vector<flow::NodeId>(in.begin(), in.end());
}

// A file name that ends in `extension` (".png"), whatever case the user typed it in.
void ensureExtension(std::filesystem::path& path, const char* extension) {
    std::string have = utf8FromPath(path.extension());
    std::transform(have.begin(), have.end(), have.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (have != extension)
        path += extension;
}

} // namespace

void FlowWorkspace::requestExport() {
    if (!canExport() || ctx_.dialogs.busy())
        return;
    exportOptions_.padding = std::clamp(exportOptions_.padding, 0, kExportMaxPadding);
    exportAsk_ = true;
}

void FlowWorkspace::drawExportDialog() {
    const std::string title = std::string(tr(Str::ActionExport)) + "###FlowExport";
    if (!exportShown_) {
        ImGui::OpenPopup(title.c_str());
        exportShown_ = true;
    }
    if (!modal::begin(title.c_str(), 26.0f)) {
        exportAsk_ = false; // the popup went away by itself
        exportShown_ = false;
        return;
    }

    FlowExportOptions& o = exportOptions_;
    const bool haveSelection = !state_.selNodes.empty(); // selected connectors alone are not exportable
    if (!haveSelection)
        o.selectionOnly = false;

    // One option per row: its name on the left, its choices side by side.
    modal::Form form({tr(Str::FlowExportFormat), tr(Str::FlowExportScale), tr(Str::FlowExportBackground),
                      tr(Str::FlowExportContent), tr(Str::FlowExportPadding)});
    form.row(tr(Str::FlowExportFormat));
    const int format = modal::choices("##exportFormat", o.format == FlowExportFormat::Png ? 0 : 1,
                                      {tr(Str::FlowExportFormatPng), tr(Str::FlowExportFormatSvg)});
    if (format >= 0)
        o.format = format == 0 ? FlowExportFormat::Png : FlowExportFormat::Svg;

    if (o.format == FlowExportFormat::Png) {
        form.row(tr(Str::FlowExportScale));
        const int scale = modal::choices("##exportScale", o.scale == 4 ? 2 : o.scale == 2 ? 1 : 0, {"x1", "x2", "x4"});
        if (scale >= 0)
            o.scale = scale == 0 ? 1 : scale == 1 ? 2 : 4;
    }

    form.row(tr(Str::FlowExportBackground));
    const int background = modal::choices(
        "##exportBackground", static_cast<int>(o.background),
        {tr(Str::FlowExportBgWhite), tr(Str::FlowExportBgTransparent), tr(Str::FlowExportBgBoard)});
    if (background >= 0)
        o.background = static_cast<FlowExportBackground>(background);

    form.row(tr(Str::FlowExportContent));
    const int content = modal::choices("##exportContent", o.selectionOnly ? 1 : 0,
                                       {tr(Str::FlowExportAll), tr(Str::FlowExportSelection)}, haveSelection ? 0u : 2u);
    if (content >= 0)
        o.selectionOnly = content == 1;

    form.row(tr(Str::FlowExportPadding));
    modal::slider("##exportPadding", o.padding, 0, kExportMaxPadding);

    const int pressed = modal::footer({{tr(Str::WordCancel), modal::ButtonKind::Secondary, true, false, true},
                                       {tr(Str::MenuExport), modal::ButtonKind::Primary, true, true}});
    const bool start = pressed == 1;
    if (pressed >= 0) {
        ImGui::CloseCurrentPopup();
        exportAsk_ = false;
        exportShown_ = false;
    }
    modal::end();
    if (start)
        beginExport();
}

// Export... was pressed: freeze the options and the selection, and ask where to save.
void FlowWorkspace::beginExport() {
    if (exportStage_ != ExportStage::Idle || ctx_.dialogs.busy())
        return;
    exportRun_ = exportOptions_;
    exportRun_.scale = exportScale(exportRun_.scale);
    exportRun_.padding = std::clamp(exportRun_.padding, 0, kExportMaxPadding);
    exportOnly_.clear();
    if (exportRun_.selectionOnly)
        exportOnly_ = state_.selNodes;
    exportRun_.selectionOnly = !exportOnly_.empty();

    const bool svg = exportRun_.format == FlowExportFormat::Svg;
    const std::filesystem::path stem = pathFromUtf8(files_.displayName()).stem();
    exportStage_ = ExportStage::AwaitingSave;
    if (!ctx_.dialogs.save(svg ? NativeDialogs::Tag::ExportSvg : NativeDialogs::Tag::ExportPng,
                           utf8FromPath(stem) + (svg ? ".svg" : ".png"))) {
        cancelExport();
        ctx_.toasts.push(tr(Str::ErrDialogUnavailable), Toasts::Kind::Error);
    }
}

void FlowWorkspace::onExportDialog(const NativeDialogs::Result& result) {
    if (exportStage_ != ExportStage::AwaitingSave)
        return; // not ours (or one already settled)
    if (result.cancelled || result.path.empty()) {
        cancelExport(); // cancelled: dropped without a word
        if (!result.error.empty())
            ctx_.toasts.push(result.error, Toasts::Kind::Error);
        return;
    }
    std::filesystem::path path = pathFromUtf8(result.path);
    if (exportRun_.format == FlowExportFormat::Svg) {
        ensureExtension(path, ".svg");
        writeSvg(path);
        cancelExport();
    } else {
        ensureExtension(path, ".png");
        exportPath_ = std::move(path);
        exportFormat_ = ImageFormat::Png;
        exportSave_ = SaveOptions{};
        exportStage_ = ExportStage::Queued; // drawn by the next rendered frame
    }
}

void FlowWorkspace::writeSvg(const std::filesystem::path& path) {
    std::vector<flow::NodeId> only;
    if (exportRun_.selectionOnly) {
        only = selectedShapes(state_.doc, exportOnly_); // the selection may have changed since the dialog
        if (only.empty()) {
            ctx_.toasts.push(tr(Str::FlowExportNothing), Toasts::Kind::Error);
            return;
        }
    }
    const ExportColors colors = exportColors(exportRun_.background);
    flow::SvgOptions options;
    options.background = colors.background;
    options.inkAuto = colors.ink;
    options.paperAuto = colors.paper;
    options.stickyFill = Rgba8{255, 233, 125, 255}; // the board's pale yellow, as the PNG draws it
    options.padding = static_cast<float>(exportRun_.padding);
    options.fontFamily = exportFontFamily(ctx_.settings);
    options.only = only;

    std::string error;
    try {
        const std::string text = flow::exportSvg(state_.doc, options);
        error = writeFileAtomic(path, std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(text.data()), text.size()));
    } catch (const std::exception&) {
        error = tr(Str::ErrImageMemory);
    }
    if (error.empty())
        ctx_.toasts.push(trf(Str::ToastSaved, {utf8FromPath(path.filename())}));
    else
        ctx_.toasts.push(error, Toasts::Kind::Error);
}

std::string FlowWorkspace::exportFromSave(const std::filesystem::path& path, ImageFormat format, const SaveOptions& options) {
    if (exportStage_ != ExportStage::Idle || state_.doc.empty())
        return tr(Str::FlowExportNothing);
    exportRun_ = exportOptions_;
    exportRun_.scale = exportScale(exportRun_.scale);
    exportRun_.padding = std::clamp(exportRun_.padding, 0, kExportMaxPadding);
    exportRun_.selectionOnly = false;
    exportOnly_.clear();
    if (format == ImageFormat::Svg) {
        exportRun_.format = FlowExportFormat::Svg;
        writeSvg(path); // a drawing of the chart itself, not a picture of it; reports for itself
        return {};
    }
    exportRun_.format = FlowExportFormat::Png;
    exportPath_ = path;
    exportFormat_ = format;
    exportSave_ = options;
    exportSave_.dpi = 96 * exportRun_.scale; // a board unit is a CSS pixel, so a page shows the chart at its natural size
    exportStage_ = ExportStage::Queued;      // drawn by the next rendered frame
    return {};
}

// Inside the frame: draws the chart into the standalone list. It stays queued while the window is minimized (no
// draw data is rendered then, so the glyphs it needs would never reach the atlas texture).
void FlowWorkspace::recordPngExport() {
    if (exportStage_ != ExportStage::Queued || exportImage_.recorded())
        return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (!(display.x > 0.0f) || !(display.y > 0.0f))
        return;

    const flow::FlowDocument& board = exportDoc_ ? *exportDoc_ : state_.doc;
    std::vector<flow::NodeId> only;
    if (exportRun_.selectionOnly)
        only = selectedShapes(board, exportOnly_);
    flow::Rect content;
    if ((exportRun_.selectionOnly && only.empty()) || !FlowView::exportBounds(board, only, content)) {
        cancelExport();
        ctx_.toasts.push(tr(Str::FlowExportNothing), Toasts::Kind::Error);
        return;
    }
    const std::optional<FlowExportPlan> plan = planExport(content, exportRun_.padding, exportRun_.scale);
    if (!plan) {
        cancelExport();
        ctx_.toasts.push(tr(Str::FlowExportTooLarge), Toasts::Kind::Error);
        return;
    }

    const ExportColors colors = exportColors(exportRun_.background);
    ImDrawList* list = nullptr;
    try {
        list = exportImage_.begin(plan->width, plan->height, colors.background.value_or(Rgba8{}));
    } catch (const std::exception&) {
        cancelExport();
        ctx_.toasts.push(tr(Str::ErrImageMemory), Toasts::Kind::Error);
        return;
    }
    const auto u32 = [](Rgba8 c) { return IM_COL32(c.r, c.g, c.b, c.a); };
    FlowExportScene scene;
    scene.area = plan->area;
    scene.scale = plan->scale;
    scene.size = ImVec2(static_cast<float>(plan->width), static_cast<float>(plan->height));
    scene.board = u32(colors.board);
    scene.ink = u32(colors.ink);
    scene.paper = u32(colors.paper);
    scene.only = only;
    view_.drawForExport(board, list, scene);
}

// After the App rendered the frame: draws the recording to pixels and saves it.
void FlowWorkspace::afterRender(SDL_Renderer* renderer) {
    if (!exportImage_.recorded())
        return;
    std::string error;
    const std::optional<Image> image = exportImage_.render(renderer, error);
    const std::filesystem::path path = exportPath_;
    const bool toClipboard = exportToClipboard_;
    cancelExport();
    if (!image) {
        ctx_.toasts.push(trf(Str::FlowExportRenderFailed, {error}), Toasts::Kind::Error);
        return;
    }
    if (toClipboard) {
        // Copy cleared the clipboard; a failure here only costs the picture, the shapes still paste inside Flowchart.
        ClipboardToken token = std::make_shared<std::atomic<bool>>(false);
        if (setClipboardImage(*image, token))
            clipAlive_ = std::move(token);
        else
            ctx_.toasts.push(tr(Str::ErrClipboardCopy), Toasts::Kind::Error);
        return;
    }
    const std::string saveError = saveImage(path, *image, exportFormat_, exportSave_);
    if (saveError.empty())
        ctx_.toasts.push(trf(Str::ToastSaved, {utf8FromPath(path.filename())}));
    else
        ctx_.toasts.push(saveError, Toasts::Kind::Error);
}

void FlowWorkspace::cancelExport() noexcept {
    exportStage_ = ExportStage::Idle;
    exportOnly_.clear();
    exportPath_.clear();
    exportImage_.discard();
    exportToClipboard_ = false;
    exportDoc_.reset();
}

} // namespace trace
