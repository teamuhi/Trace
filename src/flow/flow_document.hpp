#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "flow/flow_types.hpp"

namespace trace::flow {

// The flowchart model: nodes, edges and images. Pure data and editing rules, no history (see flow_history.hpp).
// A copy is cheap enough to be an undo step because images are shared pointers.
//
// Invariants kept by every operation: ids are unique and never reused within a document; z values are
// unique; every edge end that names a node names an existing node; a node's parent is an existing Container
// and parent chains never form a cycle; a group has at least 2 members; coordinates are finite and within
// +-kMaxCoord; sizes are at least kMinNodeSize; counts stay under the k* caps.
class FlowDocument {
public:
    // --- read access (nodes()/edges() are in insertion order; paint order is by z) ---
    const std::vector<Node>& nodes() const noexcept { return nodes_; }
    const std::vector<Edge>& edges() const noexcept { return edges_; }
    const Node* node(NodeId id) const noexcept;
    const Edge* edge(EdgeId id) const noexcept;
    ImagePtr image(ImageId id) const noexcept;
    bool empty() const noexcept { return nodes_.empty() && edges_.empty(); }
    std::vector<NodeId> nodesByZ() const; // back to front
    std::vector<NodeId> childrenOf(NodeId container) const;
    std::vector<NodeId> groupMembers(GroupId group) const;
    Rect boundsOf(std::span<const NodeId> ids) const; // zero rect when no valid ids
    std::uint64_t revision() const noexcept { return revision_; } // bumped by every successful mutation
    std::size_t approxBytes() const noexcept; // for the history memory limit; excludes shared image pixels

    // --- editing; every mutator returns false / kNo* and changes nothing when it refuses ---
    // addNode sanitises rect and style, assigns id and top z, and returns kNoNode at the cap.
    NodeId addNode(NodeKind kind, Rect rect, std::string text = {}, Style style = {}, int sides = kDefaultPolygonSides);
    ImageId addImage(ImagePtr image); // kNoImage when null, empty or at the cap
    bool setNodeImage(NodeId id, ImageId image);
    // Removes the nodes and every edge touching them; children of a removed container are detached
    // (parentId cleared) and group membership is repaired (a group left with < 2 members dissolves).
    std::size_t removeNodes(std::span<const NodeId> ids); // returns how many were removed
    std::size_t removeEdges(std::span<const EdgeId> ids);
    // Moves by (dx,dy). Children of moved containers and members of moved groups move along; each node moves once.
    bool moveNodes(std::span<const NodeId> ids, float dx, float dy);
    bool setNodeRect(NodeId id, Rect rect); // resize/move one node; children do not move; min size enforced
    bool setText(NodeId id, std::string text);
    bool setStyle(NodeId id, const Style& style);
    bool setKind(NodeId id, NodeKind kind);
    bool setSides(NodeId id, int sides); // Polygon nodes only; clamped to kMinPolygonSides..kMaxPolygonSides
    EdgeId connect(End from, End to); // refuses unknown nodes; kNoEdge at the cap; self loops allowed
    bool reconnect(EdgeId id, bool fromEnd, End end);
    bool setEdgeRoute(EdgeId id, Route route, Arrow start, Arrow end);
    // The hand-made route (nullopt = automatic, see Edge::via). Changing the route type or moving an end resets it.
    bool setEdgeVia(EdgeId id, std::optional<Pt> via);
    bool setEdgeLabel(EdgeId id, std::string label);
    // Where the label sits: `position` along the line (0..1, NaN becomes 0.5), its side, and whether it turns with the line.
    bool setEdgeLabelPlacement(EdgeId id, float position, LabelSide side, bool along);
    bool setEdgeStyle(EdgeId id, const Style& style);
    // z-order: moves the given nodes as a block, keeping their relative order.
    bool bringToFront(std::span<const NodeId> ids);
    bool sendToBack(std::span<const NodeId> ids);
    bool bringForward(std::span<const NodeId> ids); // one step past the next overlapping node above
    bool sendBackward(std::span<const NodeId> ids);
    // Groups need >= 2 distinct nodes; members of other groups are pulled into the new one (flat groups).
    GroupId group(std::span<const NodeId> ids);
    bool ungroup(GroupId group);
    // Sets the parent (a Container) or clears it with kNoNode. Refuses unknown ids, non-containers,
    // self-parenting and cycles.
    bool reparent(NodeId id, NodeId newParent);

private:
    NodeId nextNode_ = 1;
    EdgeId nextEdge_ = 1;
    ImageId nextImage_ = 1;
    GroupId nextGroup_ = 1;
    std::uint32_t nextZ_ = 1;
    std::uint64_t revision_ = 0;
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    std::unordered_map<ImageId, ImagePtr> images_;
    std::unordered_map<NodeId, std::size_t> nodeIndex_; // id -> index in nodes_; rebuilt after removals
    void reindex();
};

} // namespace trace::flow
