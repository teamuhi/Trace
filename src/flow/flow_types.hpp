#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "core/color.hpp"
#include "core/image.hpp"

namespace trace::flow {

using NodeId = std::uint32_t;
using EdgeId = std::uint32_t;
using ImageId = std::uint32_t;
using GroupId = std::uint32_t;

constexpr NodeId kNoNode = 0; // ids start at 1, so 0 means "none"
constexpr EdgeId kNoEdge = 0;
constexpr ImageId kNoImage = 0;
constexpr GroupId kNoGroup = 0;

// Every coordinate and size is clamped to this range so hostile files and runaway drags stay finite.
constexpr float kMaxCoord = 1.0e6f;
constexpr float kMinNodeSize = 8.0f;
constexpr std::size_t kMaxNodes = 20000;
constexpr std::size_t kMaxEdges = 40000;
constexpr std::size_t kMaxImages = 256;
constexpr std::size_t kMaxTextBytes = 16 * 1024;

struct Pt {
    float x = 0.0f;
    float y = 0.0f;
    constexpr bool operator==(const Pt&) const = default;
};

struct Rect {
    float x = 0.0f; // top-left
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    constexpr bool operator==(const Rect&) const = default;
    constexpr float right() const noexcept { return x + w; }
    constexpr float bottom() const noexcept { return y + h; }
    constexpr Pt center() const noexcept { return Pt{x + w * 0.5f, y + h * 0.5f}; }
};

enum class NodeKind : std::uint8_t {
    Rectangle,
    Rounded,
    Ellipse,
    Diamond,       // decision
    Parallelogram, // input / output
    Terminator,    // pill
    Document,      // wavy base
    Database,      // cylinder
    Hexagon,       // preparation
    Circle,        // connector
    Sticky,        // sticky note
    Text,          // no outline
    Image,
    Container, // swimlane with a title band
    Polygon,   // regular polygon with Node::sides sides, inscribed in the rect
};
constexpr int kNodeKindCount = 15;

constexpr int kMinPolygonSides = 3;
constexpr int kMaxPolygonSides = 32;
constexpr int kDefaultPolygonSides = 5;

enum class Face : std::uint8_t { Top, Right, Bottom, Left, Auto };
enum class Route : std::uint8_t { Curved, Elbow, Straight };
enum class Arrow : std::uint8_t { None, Arrow, Open, Dot };
enum class Dash : std::uint8_t { Solid, Dashed, Dotted };
enum class TextAlign : std::uint8_t { Left, Center, Right };
enum class VAlign : std::uint8_t { Top, Middle, Bottom }; // where a block of text sits in its shape, top to bottom
// Where a connector's label sits relative to the line: on it (the line is interrupted), or beside it on the side
// that faces up (Above) or down (Below) in the label's own reading direction.
enum class LabelSide : std::uint8_t { On, Above, Below };

// nullopt on a color means Auto: the theme decides on screen, the export background decides on export.
struct Style {
    std::optional<Rgba8> fill{};
    std::optional<Rgba8> stroke{};
    std::optional<Rgba8> textColor{};
    float strokeWidth = 2.0f; // 0.5 .. 32
    Dash dash = Dash::Solid;
    float cornerRadius = 8.0f; // Rounded only, 0 .. 200
    float fontSize = 16.0f;    // 6 .. 200
    TextAlign align = TextAlign::Center;
    VAlign valign = VAlign::Middle;
    bool operator==(const Style&) const = default;
};

struct Node {
    NodeId id = kNoNode;
    NodeKind kind = NodeKind::Rectangle;
    Rect rect{0, 0, 160, 80};
    std::string text{};
    Style style{};
    ImageId imageId = kNoImage; // Image nodes only
    NodeId parentId = kNoNode;  // the Container this node sits in
    GroupId groupId = kNoGroup;
    int sides = kDefaultPolygonSides; // Polygon only: kMinPolygonSides..kMaxPolygonSides
    std::uint32_t z = 0; // paint order, unique per document, larger is on top
};

// One end of an edge: docked to a face of a node, or a free point on the board.
struct End {
    NodeId node = kNoNode; // kNoNode means a free point
    Face face = Face::Auto;
    Pt point{};            // used only when node == kNoNode; initialized so End{node} is a complete initializer
    bool operator==(const End&) const = default;
};

struct Edge {
    EdgeId id = kNoEdge;
    End from{};
    End to{};
    Route route = Route::Curved;
    Arrow arrowStart = Arrow::None;
    Arrow arrowEnd = Arrow::Arrow;
    std::string label{};
    // A hand-made route: nullopt = routed automatically. The offset is relative to the automatic route (the
    // midpoint of a Straight or Curved path, the middle segment of an Elbow one), so it follows the shapes when they
    // move. See flow_geometry.hpp for what each route does with it.
    std::optional<Pt> via{};
    float labelPos = 0.5f;               // where along the line the label sits, 0 (start) .. 1 (end)
    LabelSide labelSide = LabelSide::On;
    bool labelAlong = false;             // the label turns with the line instead of staying horizontal
    Style style{}; // stroke, strokeWidth, dash, textColor, fontSize are used
};

using ImagePtr = std::shared_ptr<const Image>;

} // namespace trace::flow
