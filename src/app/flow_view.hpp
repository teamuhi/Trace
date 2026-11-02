#pragma once

#include <imgui.h>

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/flow_state.hpp"
#include "app/texture_mirror.hpp"
#include "core/view_transform.hpp"
#include "flow/flow_geometry.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace trace {

struct FlowDrawCtx; // per-frame drawing context, defined in flow_view.cpp

// Where and with which colors FlowView::drawForExport draws.
struct FlowExportScene {
    flow::Rect area;             // the board rectangle that maps to the whole image
    float scale = 1.0f;          // pixels per board unit
    ImVec2 size{};               // image size in pixels (the clip rectangle)
    ImU32 board = 0;             // what the board is painted with (the plate behind a label that sits on its line)
    ImU32 ink = 0;               // Auto stroke and text
    ImU32 paper = 0;             // Auto fill
    std::span<const flow::NodeId> only; // draw just these shapes and the connectors between them (empty = all)
};

// The flowchart board: draws a FlowState's document inside the current ImGui window and turns mouse and keyboard
// input into edits (see docs/PLAN_FLOWCHART.md, F3 and F4). Owns the pan/zoom (ViewTransform with limits 5% to
// 800%), the in-progress gesture, the inline text editor and one display texture per image.
//
// It edits `state.doc` directly and follows the rules in flow_state.hpp (gestures through history
// begin/endGesture, one-shot changes through state.commit()). It never opens dialogs; the Image tool only
// raises state.importImageRequested.
class FlowView {
public:
    FlowView() = default;
    ~FlowView() { release(); }
    FlowView(const FlowView&) = delete;
    FlowView& operator=(const FlowView&) = delete;

    void init(SDL_Renderer* renderer) noexcept { renderer_ = renderer; }
    // Must run before the renderer is destroyed (image textures). Safe to call repeatedly.
    void release() noexcept;

    // Fills the current window's content region with the board and handles all input for it.
    void draw(FlowState& state);

    void zoomIn();
    void zoomOut();
    void actualSize();
    // Fits the content (or centers the origin on an empty board). Applied on the next draw() if the window size
    // is not known yet.
    void fit(const FlowState& state);
    float zoomPercent() const noexcept { return view_.zoom() * 100.0f; }

    // A pointer gesture (move, resize, rubber band, connecting, panning) or the inline text editor is open.
    bool gestureActive() const noexcept;
    // The inline text editor is open (the workspace must not fire single-key shortcuts then).
    bool textEditing() const noexcept;
    // Abandons any gesture and closes the text editor without committing (document replaced, mode left).
    void reset();

    // Opens the inline editor on a node (or an edge label) at the next draw().
    void beginEditNode(flow::NodeId id);
    void beginEditEdge(flow::EdgeId id);

    // The center of the board area drawn last, in board coordinates (where pasted and imported items land).
    flow::Pt viewCenter() const noexcept;
    // Board position under the mouse if it is over the board, else the view center.
    flow::Pt pointerOrCenter() const noexcept;
    // Board position of a screen point (a dropped picture) if it is over the board area drawn last, else the view center.
    flow::Pt screenOrCenter(ImVec2 screen) const noexcept {
        const bool inside = hasView_ && screen.x >= p0_.x && screen.y >= p0_.y && screen.x < p1_.x && screen.y < p1_.y;
        return inside ? toBoard(screen) : viewCenter();
    }

    // --- export (the board drawn off-screen, see flow_export.hpp) ---
    // The board rectangle that holds everything an export draws: the shapes, the connectors with their arrowheads
    // and labels, line widths and a sticky note's shadow. `only` (empty = everything) limits it to those shapes and
    // the connectors between them. Returns false when there is nothing to draw. Measures text, so call it inside
    // an ImGui frame.
    static bool exportBounds(const flow::FlowDocument& doc, std::span<const flow::NodeId> only, flow::Rect& out);
    // Draws the document into `dl` (not the screen) with the same shape, connector and text code as the board, but
    // no grid, selection, handles or hover: board point p lands at ((p - scene.origin) * scene.scale) pixels and
    // nothing is drawn outside [0, size]. Must run inside an ImGui frame, so that glyphs are baked into the font
    // atlas by that frame's Render.
    void drawForExport(const flow::FlowDocument& doc, ImDrawList* dl, const FlowExportScene& scene);

private:
    // The private part belongs to flow_view.cpp's author: add and restructure members freely. The public API
    // above is the contract the workspace is written against.
    enum class Mode : unsigned char { None, Pan, Move, Resize, Band, Connect, Reconnect, Create, Label, Bend };

    // The one pointer gesture in progress. Only the fields of its mode are meaningful.
    struct Gesture {
        Mode mode = Mode::None;
        int button = ImGuiMouseButton_Left;
        ImVec2 startScreen{};
        flow::Pt startBoard{};
        bool dragging = false; // the pointer left the click tolerance
        // Move: every node that moves (selection, group members, container children) and where it started.
        std::vector<flow::NodeId> ids;
        std::vector<flow::NodeId> sortedIds;
        std::vector<flow::Rect> startRects;
        flow::Rect startBounds{};
        flow::NodeId clickNode = flow::kNoNode; // a plain click on it (no drag) narrows the selection to it
        std::vector<flow::Guide> guides;
        flow::Pt applied{}; // the delta applied to the document so far
        bool histOwned = false; // a history gesture was opened for this drag
        // Resize
        flow::NodeId node = flow::kNoNode;
        flow::ResizeHandle handle = flow::ResizeHandle::None;
        flow::Rect startRect{};
        // Band
        bool additive = false;
        std::vector<flow::NodeId> baseNodes;
        std::vector<flow::EdgeId> baseEdges;
        // Connect: the end the noodle starts at. Reconnect: the end that stays.
        flow::End from{};
        flow::EdgeId edge = flow::kNoEdge;
        bool movingFrom = false; // Reconnect: the "from" end is the one being dragged
        // Create
        FlowTool tool = FlowTool::Shape;
        flow::NodeKind kind = flow::NodeKind::Rectangle;
        int sides = flow::kDefaultPolygonSides; // Create: corners of a Polygon being placed
        flow::Rect createRect{};
    };

    // What the pointer is over when no gesture runs.
    struct Hover {
        flow::NodeId body = flow::kNoNode; // the node under the pointer
        flow::NodeId host = flow::kNoNode; // the node whose connection dots show (under or just beside the pointer)
        flow::Face face = flow::Face::Auto;
        bool onFace = false; // the pointer is on one of host's dots
        flow::EdgeId edge = flow::kNoEdge;
        flow::ResizeHandle handle = flow::ResizeHandle::None;
        bool onEdgeEnd = false;
        bool onBend = false; // the pointer is on the route handle of the selected connector
    };

    struct Editor {
        bool open = false;
        bool isEdge = false;
        flow::NodeId node = flow::kNoNode;
        flow::EdgeId edge = flow::kNoEdge;
        std::string text;
        std::string original;
        bool fresh = false;        // a node a tool just made; an empty result removes it again
        bool ownsGesture = false;  // a history gesture was opened for it (closed with the editor)
        bool focus = true;         // grab keyboard focus on the first frame
        int frames = 0;
        ImVec2 rectMin{};
        ImVec2 rectMax{};
    };

    struct Picker {
        bool requestOpen = false;
        bool open = false;
        flow::End from{};
        flow::Pt drop{};
    };

    struct ImageTex {
        flow::ImagePtr src;
        TextureMirror mirror;
    };

    struct DropTarget {
        flow::NodeId node = flow::kNoNode;
        flow::Face face = flow::Face::Auto;
        bool onDot = false; // the pointer is on that face's dot (not just somewhere on the shape)
    };

    // --- view ---
    flow::Pt toBoard(ImVec2 screen) const noexcept;
    ImVec2 toScreen(flow::Pt p) const noexcept;
    float pxToBoard(float px) const noexcept;
    flow::Rect visibleBoard(float marginPx) const noexcept;
    void applyFit(const FlowState& state);
    void handleWheel(bool hovered);

    // --- per frame ---
    void prepareFrame(FlowState& state);
    void drawBoard(FlowState& state, const FlowDrawCtx& c);
    void drawOverlays(FlowState& state, const FlowDrawCtx& c);
    void drawGestureOverlay(FlowState& state, const FlowDrawCtx& c);
    SDL_Texture* imageTexture(const flow::FlowDocument& doc, flow::ImageId id);

    // --- picking ---
    struct PickResult {
        flow::NodeId body = flow::kNoNode;
        flow::NodeId nearby = flow::kNoNode;
    };
    PickResult pickNode(const FlowState& state, flow::Pt p, float nearTolerance) const;
    bool nodeHit(const FlowState& state, const flow::Node& n, flow::Pt p, float tolerance) const;
    flow::EdgeId pickEdge(const FlowState& state, flow::Pt p) const;
    const flow::EdgePath* pathOf(flow::EdgeId id) const;
    bool labelHit(const flow::Edge& e, const flow::EdgePath& path, flow::Pt p) const;
    bool onBendHandle(flow::EdgeId id, flow::Pt p) const;
    flow::NodeId containerAt(const FlowState& state, flow::Pt p, const std::vector<flow::NodeId>& sortedExclude) const;
    DropTarget resolveDrop(const FlowState& state, flow::Pt p) const;
    void updateHover(const FlowState& state, flow::Pt p);

    // --- selection ---
    std::vector<flow::NodeId> selectionUnit(const FlowState& state, flow::NodeId id) const;
    void selectNode(FlowState& state, flow::NodeId id);
    void toggleNode(FlowState& state, flow::NodeId id);

    // --- input ---
    void routeInput(FlowState& state, bool hovered);
    void handleKeys(FlowState& state);
    void pressSelect(FlowState& state, flow::Pt p, bool doubleClick);
    void pressConnector(FlowState& state, flow::Pt p);
    void pressRight(FlowState& state, flow::Pt p);
    void beginPan(int button);
    void beginMove(FlowState& state, flow::Pt p, flow::NodeId clickNode);
    void beginResize(FlowState& state, flow::Pt p, flow::NodeId node, flow::ResizeHandle handle);
    void beginBand(FlowState& state, flow::Pt p, bool additive);
    void beginConnect(FlowState& state, flow::Pt p, flow::NodeId node, flow::Face face);
    void beginReconnect(FlowState& state, flow::Pt p, flow::EdgeId edge, bool movingFrom);
    void beginCreate(FlowState& state, flow::Pt p, FlowTool tool);
    void beginLabelDrag(FlowState& state, flow::Pt p, flow::EdgeId edge);
    void beginBendDrag(FlowState& state, flow::Pt p, flow::EdgeId edge);
    void updateGesture(FlowState& state);
    void updateMove(FlowState& state, flow::Pt p);
    void updateResize(FlowState& state, flow::Pt p);
    void updateBand(FlowState& state, flow::Pt p);
    void updateCreate(FlowState& state, flow::Pt p);
    void updateLabelDrag(FlowState& state, flow::Pt p);
    void updateBendDrag(FlowState& state, flow::Pt p);
    void releaseGesture(FlowState& state, flow::Pt p);
    void releaseMove(FlowState& state);
    void releaseConnect(FlowState& state, flow::Pt p);
    void releaseCreate(FlowState& state);
    void endGestureState();
    void abortGesture(FlowState& state);
    bool gestureValid(const FlowState& state) const;
    void beginHist(FlowState& state);
    void endHist(FlowState& state);
    void cancelHist(FlowState& state);
    void reparentMoved(FlowState& state);

    // --- text editor and shape picker ---
    void openEditor(FlowState& state);
    void drawEditor(FlowState& state);
    void closeEditor(FlowState& state, bool commit);
    void drawPicker(FlowState& state);
    void drawEdgeMenu(FlowState& state);
    void createConnectedNode(FlowState& state, flow::NodeKind kind);
    void connectToFreePoint(FlowState& state);

    SDL_Renderer* renderer_ = nullptr;
    ViewTransform view_;

    ImVec2 p0_{};
    ImVec2 p1_{};
    Vec2f viewSize_{};
    bool hasView_ = false;
    bool fitPending_ = true;
    bool fitMode_ = true; // refit when the window size changes, until the user pans or zooms
    Vec2f fitSize_{};
    bool pointerInside_ = false;
    flow::Pt pointerBoard_{};

    Gesture gesture_;
    Hover hover_;
    Editor editor_;
    Picker picker_;
    bool edgeMenuRequest_ = false; // a right click landed on a connector: open its menu
    int histDepth_ = 0;        // history gestures this view opened and has not closed
    bool abandon_ = false;     // reset() asked for the open gesture to be rolled back at the next draw()
    bool refocus_ = false;
    flow::GroupId enteredGroup_ = flow::kNoGroup;
    flow::NodeId pendingEditNode_ = flow::kNoNode;
    flow::EdgeId pendingEditEdge_ = flow::kNoEdge;
    bool pendingFresh_ = false; // the pending edit is of a node a tool just made

    // Rebuilt every frame.
    std::vector<flow::NodeId> zOrder_;
    struct EdgeDraw {
        flow::EdgeId id = flow::kNoEdge;
        flow::EdgePath path;
    };
    std::vector<EdgeDraw> visEdges_;
    flow::Rect visible_{};

    std::unordered_map<flow::ImageId, std::unique_ptr<ImageTex>> images_;
};

} // namespace trace
