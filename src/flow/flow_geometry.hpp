#pragma once

#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "flow/flow_types.hpp"

namespace trace::flow {

// Pure flowchart geometry: node outlines, face anchors, edge routing, hit tests and snapping. No SDL or
// ImGui and no FlowDocument dependency: edge routing takes a lookup so a caller can pass
// `[&](NodeId id) { return doc.node(id); }`.
//
// Every input is sanitised first (NaN becomes 0, everything is clamped to +-kMaxCoord, sizes are >= 0), so
// hostile values never produce NaN or infinity, never crash and never loop for long. Zoom is not a concern
// here: the caller converts a screen-space radius or tolerance to world units (px / zoom) before calling.

using NodeLookup = std::function<const Node*(NodeId)>;

constexpr float kContainerTitleHeight = 32.0f; // height of a Container's title band
constexpr float kElbowMargin = 24.0f;          // how far an Elbow route leaves a face before turning

// --- sanitising --------------------------------------------------------------------------------------
float sanitizeCoord(float v) noexcept; // NaN -> 0, otherwise clamped to +-kMaxCoord
Pt sanitize(Pt p) noexcept;
Rect sanitize(Rect r) noexcept; // finite, clamped; w and h are >= 0

// --- outlines ----------------------------------------------------------------------------------------
// The closed outline of a shape inside `rect` (first point is not repeated at the end). Text, Image,
// Sticky and Container use the rect; Rounded uses `cornerRadius` clamped to half the shorter side;
// Terminator is a pill; Ellipse and Circle are sampled (extremes are polygon vertices); Database is the
// cylinder silhouette; Document has a wavy base. Always within the (sanitised) rect.
std::vector<Pt> outlineFor(NodeKind kind, Rect rect, float cornerRadius = 8.0f, int sides = kDefaultPolygonSides);
std::vector<Pt> nodeOutline(const Node& node);
// Database only: the visible front rim of the top cap (open polyline, left to right), for drawing.
std::vector<Pt> databaseRim(Rect rect);
// Container only (others get the same band geometry, harmless): the title band at the top of the rect.
Rect containerTitleBand(Rect rect);

// --- faces -------------------------------------------------------------------------------------------
// Where an edge docks: a point on the outline and the outward unit normal.
struct Anchor {
    Pt point;
    Pt normal;
};
// Top/Right/Bottom/Left = the midpoint of that side. Ellipse, Diamond and Circle extremes coincide with
// the midpoints; Parallelogram and Document anchors sit on their slanted side / wavy base. Face::Auto is
// not a real face here and behaves as Right; use routeEdge to resolve Auto.
Anchor faceAnchor(NodeKind kind, Rect rect, Face face, int sides = kDefaultPolygonSides);
Anchor faceAnchor(const Node& node, Face face);

// --- routing -----------------------------------------------------------------------------------------
// A routed edge: the flattened polyline plus tangents (unit vectors in the direction of travel, so the
// start tangent leaves the first node and the end tangent enters the last).
struct EdgePath {
    std::vector<Pt> points;
    Pt startTangent{1.0f, 0.0f};
    Pt endTangent{1.0f, 0.0f};
    Pt labelPos;      // midpoint by arc length
    float length = 0.0f;
    Face fromFace = Face::Auto; // faces actually used; Auto for a free-point end
    Face toFace = Face::Auto;
    // The handle a user drags to shape the route by hand (hasHandle is false when the route has nothing to move: a
    // plain L or a zero-length elbow). `handle` is where it is now, `handleBase` where it sits for an automatic
    // route, and `handleAxis` the unit direction an elbow segment slides in ((0,0) = free in both directions).
    bool hasHandle = false;
    Pt handle;
    Pt handleBase;
    Pt handleAxis;
};

// One end handed to routePath: an anchor, plus the rect of the node it docks to (used by Elbow to detour
// around the node; absent for a free point).
struct RouteEnd {
    Anchor anchor;
    Rect bounds;
    bool hasBounds = false;
};

// Route between two ends (also used for the live "noodle" while dragging a new connector). Normals are
// snapped to a cardinal direction. Curved = cubic Bezier with control arms clamp(dist*0.4, 24, 160) along
// the normals; Elbow = axis-aligned L / Z / U / detour paths; Straight = one segment.
//
// `via` is the manual adjustment (Edge::via), an offset from the automatic route: Straight bends through the
// midpoint of its ends moved by it, Curved moves the middle of the curve (the curve stays smooth and is the
// automatic one while the offset is zero), Elbow slides its middle segment (the component along the segment's
// normal; a plain L has no segment to slide and ignores it).
EdgePath routePath(Route route, const RouteEnd& from, const RouteEnd& to, const std::optional<Pt>& via = std::nullopt);

// The `via` that puts the path's handle at `world`: relative to handleBase, and for an elbow only along handleAxis.
Pt viaFromPoint(const EdgePath& path, Pt world);

// Resolve faces (Auto picks the pair that minimises the route length, re-evaluated every call; a free
// point's normal points at the other end) and route. Nullopt when an end names a node the lookup cannot
// find. A self-loop (both ends on one node) goes around a corner: Auto/Auto is Right -> Top, and equal or
// opposite faces are adjusted so the loop always wraps one corner.
std::optional<EdgePath> routeEdge(const Edge& edge, const NodeLookup& lookup);

// Arc-length helpers on a polyline (fraction is clamped to 0..1). Empty input gives {0,0}.
Pt pointAtFraction(std::span<const Pt> points, float fraction);
float polylineLength(std::span<const Pt> points);
// Unit direction of travel at that fraction of the arc length ({1,0} for a degenerate path).
Pt tangentAtFraction(std::span<const Pt> points, float fraction);

// Where a connector label goes. `t` is the fraction along the path, `along` turns the text to follow the line (the
// angle is kept in (-90, 90] degrees so the text never reads upside down), `side` puts it on the line or beside it,
// and halfW / halfH are the label box's half size (the box is cleared by a small gap when it sits beside the line).
// Above is the side facing screen-up (to the right of a vertical line), Below the opposite one.
struct LabelPlacement {
    Pt center;         // the middle of the label box
    Pt base;           // the point on the line it belongs to
    float angle = 0.0f; // radians; the box and its text are turned by this about `center`
};
LabelPlacement placeLabel(std::span<const Pt> points, float t, bool along, LabelSide side, float halfW, float halfH);

// The point of the path nearest to `p`: its fraction of the arc length, its distance from p, and the signed distance
// of p along the "Above" normal at that point (positive on the Above side). Empty input gives t = 0.5.
struct PathProjection {
    float t = 0.5f;
    float distance = 0.0f;
    float side = 0.0f;
    Pt nearest;
};
PathProjection projectOnPath(std::span<const Pt> points, Pt p);

// How far a label box of half size (halfW, halfH), turned by `angle`, reaches along the unit direction `dir`.
float labelExtent(float angle, float halfW, float halfH, Pt dir);

// Arrowhead drawn at `tip`, pointing along `direction` (travel direction into the tip). Arrow is a
// filled triangle, Open a chevron polyline, Dot a filled circle touching the tip. `inset` is how far back
// from the tip the line itself should stop so the stroke does not poke through the head.
struct ArrowHead {
    std::vector<Pt> points; // empty for Arrow::None
    bool filled = false;
    bool closed = false;
    float inset = 0.0f;
};
ArrowHead arrowHead(Arrow type, Pt tip, Pt direction, float strokeWidth);
// The head for one end of a routed path (atStart = the "from" end; it points away from the path).
ArrowHead arrowHeadFor(const EdgePath& path, bool atStart, Arrow type, float strokeWidth);

// --- hit tests ---------------------------------------------------------------------------------------
// Point inside the node's outline, or within `tolerance` of it.
bool hitNode(const Node& node, Pt p, float tolerance = 0.0f);
bool pointInPolygon(std::span<const Pt> polygon, Pt p);
bool hitContainerTitle(const Node& node, Pt p);

enum class ResizeHandle : std::uint8_t { None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };
Pt resizeHandlePos(Rect rect, ResizeHandle handle);
// The 8 resize handles of `rect`; the nearest one within `radius` wins (corners win ties).
ResizeHandle hitResizeHandle(Rect rect, Pt p, float radius);

// The 4 face handles sit on the face anchors; the nearest within `radius` wins.
std::optional<Face> hitFaceHandle(const Node& node, Pt p, float radius);

// Distance from `p` to the polyline (a very large finite value for an empty one).
float distanceToPath(std::span<const Pt> points, Pt p);
bool hitEdge(const EdgePath& path, Pt p, float tolerance);

enum class EdgeEnd : std::uint8_t { None, From, To };
// The edge's start or end handle if `p` is within `radius` of it (nearest wins, To on a tie).
EdgeEnd hitEdgeEndpoint(const EdgePath& path, Pt p, float radius);

// --- snapping ----------------------------------------------------------------------------------------
// Grid spacing <= 0 (or non-finite) leaves values unchanged.
float snapToGrid(float v, float grid);
Pt snapToGrid(Pt p, float grid);
Rect snapRectPosition(Rect r, float grid); // moves the top-left onto the grid, keeps the size
Rect snapRectEdges(Rect r, float grid);    // snaps all four edges (a resize); size stays >= 0

// A smart alignment line to draw: vertical lines are at x = pos spanning y from..to, horizontal at y = pos.
struct Guide {
    bool vertical = false;
    float pos = 0.0f;
    float from = 0.0f;
    float to = 0.0f;
};
struct SnapResult {
    Pt delta;                // add to the moving rect to align it
    bool snappedX = false;
    bool snappedY = false;
    std::vector<Guide> guides; // lines of the final (snapped) position, merged per coordinate
};
// Align the moving rect's left / center / right and top / middle / bottom to those of `others` when within
// `threshold` world units (the nearest alignment per axis wins).
SnapResult snapToGuides(Rect moving, std::span<const Rect> others, float threshold);

} // namespace trace::flow
