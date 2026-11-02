#pragma once

#include <algorithm>
#include <optional>
#include <vector>

#include "flow/flow_document.hpp"
#include "flow/flow_history.hpp"
#include "flow/flow_templates.hpp"

namespace trace {

enum class FlowTool : unsigned char {
    Select,
    Hand,
    Connector,
    Text,   // click or drag makes a Text node and opens it for editing
    Sticky, // click or drag makes a Sticky note
    Image,  // the workspace opens the import dialog (see FlowState::importImageRequested)
    Shape,  // click or drag places FlowState::shapeKind
};

// Everything the Flowchart view, its panels and the workspace share about the document being edited: the
// document, its undo history, the selection, the active tool and the board options. Plain data plus a few
// helpers; no ImGui, so it can be tested. The workspace owns exactly one.
//
// Rules for whoever edits `doc`:
// - A single change (a panel button, a menu command, a paste): change the document, then call commit().
// - A pointer drag (move, resize, rubber-band a connection): history.beginGesture() at the press and
//   history.endGesture(doc) at the release, so the whole drag is one undo step.
// - After undo/redo/new/open the document is replaced with replaceDocument(), which also repairs the selection.
struct FlowState {
    flow::FlowDocument doc;
    flow::FlowHistory history;

    // Selection. Both lists hold ids that exist in `doc` (pruneSelection() keeps it so). Selecting a node of a
    // group selects the whole group: the view does that, not this struct.
    std::vector<flow::NodeId> selNodes;
    std::vector<flow::EdgeId> selEdges;

    FlowTool tool = FlowTool::Select;
    flow::NodeKind shapeKind = flow::NodeKind::Rectangle; // used when tool == FlowTool::Shape
    int polygonSides = flow::kDefaultPolygonSides;        // corners of a Polygon placed next (Shapes panel)

    // Board options (not saved yet; the Flowchart settings page of a later phase persists them).
    bool showGrid = true;
    bool snapToGrid = true;
    bool smartGuides = true;
    float gridSize = 20.0f;

    // Auto layout runs left to right instead of top to bottom.
    bool layoutLeftRight = false;
    // Set by File > New from Template: the next New (once any unsaved-changes prompt is answered) starts from it.
    std::optional<flow::TemplateKind> pendingTemplate;

    // What a new connector starts as.
    flow::Route defaultRoute = flow::Route::Curved;
    flow::Arrow defaultArrowStart = flow::Arrow::None;
    flow::Arrow defaultArrowEnd = flow::Arrow::Arrow;
    std::optional<Rgba8> defaultEdgeStroke;                // nullopt = Auto (theme ink)

    // Set by the view or the Shapes panel when the Image tool is picked; the workspace opens the import dialog
    // and clears it.
    bool importImageRequested = false;

    // --- helpers ---
    // Records the document as a new undo step if it changed. Returns whether a step was recorded.
    bool commit() { return history.commit(doc); }

    bool hasSelection() const noexcept { return !selNodes.empty() || !selEdges.empty(); }
    bool isNodeSelected(flow::NodeId id) const noexcept {
        return std::find(selNodes.begin(), selNodes.end(), id) != selNodes.end();
    }
    bool isEdgeSelected(flow::EdgeId id) const noexcept {
        return std::find(selEdges.begin(), selEdges.end(), id) != selEdges.end();
    }
    void clearSelection() {
        selNodes.clear();
        selEdges.clear();
    }
    void selectOnlyNode(flow::NodeId id) {
        clearSelection();
        if (doc.node(id))
            selNodes.push_back(id);
    }
    void selectOnlyEdge(flow::EdgeId id) {
        clearSelection();
        if (doc.edge(id))
            selEdges.push_back(id);
    }
    // Drops selected ids that no longer exist in the document.
    void pruneSelection() {
        selNodes.erase(std::remove_if(selNodes.begin(), selNodes.end(), [&](flow::NodeId id) { return !doc.node(id); }),
                       selNodes.end());
        selEdges.erase(std::remove_if(selEdges.begin(), selEdges.end(), [&](flow::EdgeId id) { return !doc.edge(id); }),
                       selEdges.end());
    }
    // Swaps in a document (an undo/redo state, or a freshly loaded one) and repairs the selection.
    void replaceDocument(const flow::FlowDocument& next) {
        doc = next;
        pruneSelection();
    }
};

} // namespace trace
