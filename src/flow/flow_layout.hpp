#pragma once

#include <span>

#include "flow/flow_document.hpp"

namespace trace::flow {

enum class AlignMode : unsigned char { Left, Center, Right, Top, Middle, Bottom };
enum class DistributeAxis : unsigned char { Horizontal, Vertical };
enum class MatchMode : unsigned char { Width, Height };

// Layout commands on a selection. Each acts on "units": a group counts as one unit (it moves as a block), and a
// node whose container is also in the selection is not a unit of its own (it follows its container). Each
// returns whether the document changed. Moves go through FlowDocument::moveNodes, so children and group members
// follow; nothing here commits to history (the caller does).
//
// - alignNodes: aligns units to the bounds of all the units; needs >= 2 units, else a no-op.
// - distributeNodes: equal gaps between units along the axis, outermost units stay; needs >= 3 units.
// - matchSize: gives every selected node the width (or height) of the first node of `ids` that exists; needs >= 2 nodes;
//   a container's children do not move. Groups are not resized as a block: only the listed nodes change.
bool alignNodes(FlowDocument& doc, std::span<const NodeId> ids, AlignMode mode);
bool distributeNodes(FlowDocument& doc, std::span<const NodeId> ids, DistributeAxis axis);
bool matchSize(FlowDocument& doc, std::span<const NodeId> ids, MatchMode mode);

// How many units `ids` forms (see above); align needs >= 2 and distribute >= 3. For enabling menu items and buttons.
std::size_t layoutUnitCount(const FlowDocument& doc, std::span<const NodeId> ids);

enum class LayoutDirection : unsigned char { TopDown, LeftRight };

// Arranges the nodes of a flowchart in layers along the connectors (a simplified Sugiyama layout).
//
// `ids` are the nodes to lay out; empty means every node. Units are as for the commands above (a group is one
// block; a node under a container in the set rides with that container, and a container is laid out as one block
// with its children, whose insides are left as they are). Only connectors whose two ends dock to nodes of two
// different units count. The algorithm:
//   1. break cycles by reversing DFS back edges (deterministic: units and edges are visited in document order);
//   2. longest-path layering, so every connector (as reversed) points from a lower layer to a higher one;
//   3. 4 barycenter sweeps (down, up, down, up) over the order inside the layers to reduce crossings;
//   4. coordinates: layers are stacked along the direction (TopDown: layers are rows; LeftRight: columns) with
//      `spacing` between them (measured between the largest unit of one layer and the next) and units inside a
//      layer are centered on the layer line, `spacing * 2 / 3` apart, so sizes never overlap;
//   5. unconnected components (and single units) are laid out separately and put side by side across the layer
//      axis, `spacing` apart.
// The result is anchored so the top-left corner of the bounds of the laid-out units stays where it was.
// Docked ends of the connectors among the laid-out units are set to Face::Auto so the routes follow the new places.
// Free-ended connectors are untouched. Deterministic (same input, same output), never overlaps two units, finite for
// any input; returns false (and bumps no revision) when nothing moved, there are fewer than 2 units, or `spacing` is
// not finite. spacing is clamped to [0, 2000]. Nothing is committed to history (the caller does).
bool autoLayout(FlowDocument& doc, std::span<const NodeId> ids, LayoutDirection direction, float spacing = 60.0f);

} // namespace trace::flow
