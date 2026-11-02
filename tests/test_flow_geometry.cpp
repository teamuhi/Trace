#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

#include "flow/flow_geometry.hpp"

namespace trace::flow {
namespace {

constexpr NodeKind kAllKinds[kNodeKindCount] = {
    NodeKind::Rectangle, NodeKind::Rounded, NodeKind::Ellipse, NodeKind::Diamond,   NodeKind::Parallelogram,
    NodeKind::Terminator, NodeKind::Document, NodeKind::Database, NodeKind::Hexagon, NodeKind::Circle,
    NodeKind::Sticky,    NodeKind::Text,     NodeKind::Image,    NodeKind::Container, NodeKind::Polygon,
};
constexpr Face kFour[4] = {Face::Top, Face::Right, Face::Bottom, Face::Left};

bool near(float a, float b, float eps = 1.0e-3f) { return std::abs(a - b) <= eps; }
bool near(Pt a, Pt b, float eps = 1.0e-3f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps); }
bool finite(Pt p) { return std::isfinite(p.x) && std::isfinite(p.y); }

Node makeNode(NodeId id, NodeKind kind, Rect rect) {
    Node n;
    n.id = id;
    n.kind = kind;
    n.rect = rect;
    return n;
}

// A small id -> node store with the lookup the geometry functions take.
struct Nodes {
    std::map<NodeId, Node> map;
    Node& add(NodeId id, NodeKind kind, Rect rect) { return map[id] = makeNode(id, kind, rect); }
    NodeLookup lookup() const {
        return [this](NodeId id) -> const Node* {
            const auto it = map.find(id);
            return it == map.end() ? nullptr : &it->second;
        };
    }
};

Edge makeEdge(NodeId a, Face fa, NodeId b, Face fb, Route route = Route::Curved) {
    Edge e;
    e.id = 1;
    e.from.node = a;
    e.from.face = fa;
    e.to.node = b;
    e.to.face = fb;
    e.route = route;
    return e;
}

float area(const std::vector<Pt>& poly) {
    double a = 0.0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Pt p = poly[i];
        const Pt q = poly[(i + 1) % poly.size()];
        a += static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y;
    }
    return static_cast<float>(std::abs(a) * 0.5);
}

bool axisAligned(const std::vector<Pt>& pts, float eps = 1.0e-2f) {
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (std::abs(pts[i].x - pts[i - 1].x) > eps && std::abs(pts[i].y - pts[i - 1].y) > eps) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("Every node kind has a non-degenerate outline inside its rect") {
    const Rect r{10, 20, 160, 80};
    for (NodeKind kind : kAllKinds) {
        const std::vector<Pt> poly = outlineFor(kind, r, 12.0f);
        INFO("kind ", static_cast<int>(kind));
        REQUIRE(poly.size() >= 3);
        CHECK(area(poly) > 100.0f);
        for (const Pt& p : poly) {
            CHECK(p.x >= r.x - 1.0e-3f);
            CHECK(p.x <= r.right() + 1.0e-3f);
            CHECK(p.y >= r.y - 1.0e-3f);
            CHECK(p.y <= r.bottom() + 1.0e-3f);
        }
    }
}

TEST_CASE("Outlines survive tiny and zero-size rects") {
    for (NodeKind kind : kAllKinds) {
        for (const Rect r : {Rect{5, 5, 0, 0}, Rect{5, 5, 8, 8}, Rect{5, 5, 0, 50}, Rect{0, 0, 3, 2}}) {
            const std::vector<Pt> poly = outlineFor(kind, r, 8.0f);
            CHECK(poly.size() >= 3);
            for (const Pt& p : poly) {
                CHECK(finite(p));
                CHECK(p.x >= r.x - 1.0e-3f);
                CHECK(p.x <= r.right() + 1.0e-3f);
                CHECK(p.y >= r.y - 1.0e-3f);
                CHECK(p.y <= r.bottom() + 1.0e-3f);
            }
        }
    }
}

TEST_CASE("Rectangle-like kinds outline their rect; Rounded clamps the radius") {
    const Rect r{0, 0, 100, 40};
    for (NodeKind kind : {NodeKind::Rectangle, NodeKind::Text, NodeKind::Image, NodeKind::Sticky, NodeKind::Container}) {
        CHECK(outlineFor(kind, r).size() == 4);
        CHECK(near(area(outlineFor(kind, r)), 4000.0f));
    }
    CHECK(outlineFor(NodeKind::Rounded, r, 0.0f).size() == 4); // radius 0 is a plain rect
    // A huge radius is clamped to half the shorter side: every point stays inside the rect.
    for (const Pt& p : outlineFor(NodeKind::Rounded, r, 1.0e9f)) {
        CHECK(p.x >= -1.0e-3f);
        CHECK(p.x <= 100.001f);
        CHECK(p.y >= -1.0e-3f);
        CHECK(p.y <= 40.001f);
    }
    // The clamped rounded rect cuts its corners, so it is smaller than the rect; the pill is the same shape.
    CHECK(area(outlineFor(NodeKind::Rounded, r, 1.0e9f)) < 4000.0f);
    CHECK(near(area(outlineFor(NodeKind::Rounded, r, 1.0e9f)), area(outlineFor(NodeKind::Terminator, r)), 0.5f));
}

TEST_CASE("Ellipse, Circle and Diamond outlines include their four extremes") {
    const Rect r{0, 0, 120, 60};
    for (NodeKind kind : {NodeKind::Ellipse, NodeKind::Circle, NodeKind::Diamond}) {
        const std::vector<Pt> poly = outlineFor(kind, r);
        for (Face f : kFour) {
            const Pt a = faceAnchor(kind, r, f).point;
            bool found = false;
            for (const Pt& p : poly) {
                found = found || near(p, a, 0.01f);
            }
            CHECK(found);
        }
    }
}

TEST_CASE("Faces are the side midpoints with outward normals") {
    const Rect r{100, 200, 80, 40};
    for (NodeKind kind : kAllKinds) {
        if (kind == NodeKind::Parallelogram || kind == NodeKind::Document || kind == NodeKind::Polygon) {
            continue; // these move an anchor onto their slanted side, wavy base or corner outline (tested below)
        }
        INFO("kind ", static_cast<int>(kind));
        CHECK(near(faceAnchor(kind, r, Face::Top).point, Pt{140, 200}));
        CHECK(near(faceAnchor(kind, r, Face::Right).point, Pt{180, 220}));
        CHECK(near(faceAnchor(kind, r, Face::Bottom).point, Pt{140, 240}));
        CHECK(near(faceAnchor(kind, r, Face::Left).point, Pt{100, 220}));
    }
    CHECK(faceAnchor(NodeKind::Rectangle, r, Face::Top).normal == Pt{0, -1});
    CHECK(faceAnchor(NodeKind::Rectangle, r, Face::Right).normal == Pt{1, 0});
    CHECK(faceAnchor(NodeKind::Rectangle, r, Face::Bottom).normal == Pt{0, 1});
    CHECK(faceAnchor(NodeKind::Rectangle, r, Face::Left).normal == Pt{-1, 0});
    CHECK(faceAnchor(NodeKind::Rectangle, r, Face::Auto).normal == Pt{1, 0}); // Auto behaves as Right
}

TEST_CASE("Parallelogram and Document anchors sit on their outline") {
    const Rect r{0, 0, 200, 100};
    for (NodeKind kind : {NodeKind::Parallelogram, NodeKind::Document}) {
        const std::vector<Pt> poly = outlineFor(kind, r);
        for (Face f : kFour) {
            const Pt a = faceAnchor(kind, r, f).point;
            float best = 1.0e9f;
            for (std::size_t i = 0; i < poly.size(); ++i) {
                const Pt p = poly[i];
                const Pt q = poly[(i + 1) % poly.size()];
                best = std::min(best, distanceToPath(std::vector<Pt>{p, q}, a));
            }
            INFO("kind ", static_cast<int>(kind), " face ", static_cast<int>(f));
            CHECK(best < 0.5f);
        }
    }
    // The parallelogram's left anchor is pulled in to the slanted side's midpoint.
    CHECK(faceAnchor(NodeKind::Parallelogram, r, Face::Left).point.x > 0.0f);
    CHECK(faceAnchor(NodeKind::Parallelogram, r, Face::Right).point.x < 200.0f);
}

TEST_CASE("Container title band is the top strip, clamped to the height") {
    CHECK(containerTitleBand(Rect{10, 20, 300, 200}) == Rect{10, 20, 300, kContainerTitleHeight});
    CHECK(containerTitleBand(Rect{10, 20, 300, 10}).h == 10.0f);
    Node c = makeNode(1, NodeKind::Container, Rect{0, 0, 300, 200});
    CHECK(hitContainerTitle(c, Pt{150, 10}));
    CHECK_FALSE(hitContainerTitle(c, Pt{150, 100}));
    CHECK_FALSE(hitContainerTitle(c, Pt{-5, 10}));
    c.kind = NodeKind::Rectangle;
    CHECK_FALSE(hitContainerTitle(c, Pt{150, 10}));
}

TEST_CASE("Auto faces pick the pair that minimises the route") {
    Nodes ns;
    ns.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 50});
    for (Route route : {Route::Curved, Route::Elbow, Route::Straight}) {
        INFO("route ", static_cast<int>(route));
        const Edge e = makeEdge(1, Face::Auto, 2, Face::Auto, route);
        ns.add(2, NodeKind::Rectangle, Rect{400, 0, 100, 50}); // to the right
        auto p = routeEdge(e, ns.lookup());
        REQUIRE(p);
        CHECK(p->fromFace == Face::Right);
        CHECK(p->toFace == Face::Left);

        ns.add(2, NodeKind::Rectangle, Rect{0, 300, 100, 50}); // below
        p = routeEdge(e, ns.lookup());
        REQUIRE(p);
        CHECK(p->fromFace == Face::Bottom);
        CHECK(p->toFace == Face::Top);

        ns.add(2, NodeKind::Rectangle, Rect{-400, 0, 100, 50}); // to the left
        p = routeEdge(e, ns.lookup());
        REQUIRE(p);
        CHECK(p->fromFace == Face::Left);
        CHECK(p->toFace == Face::Right);

        ns.add(2, NodeKind::Rectangle, Rect{0, -300, 100, 50}); // above
        p = routeEdge(e, ns.lookup());
        REQUIRE(p);
        CHECK(p->fromFace == Face::Top);
        CHECK(p->toFace == Face::Bottom);
    }
}

TEST_CASE("Auto is re-evaluated each call and a fixed face is respected") {
    Nodes ns;
    ns.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 50});
    ns.add(2, NodeKind::Rectangle, Rect{400, 0, 100, 50});
    const Edge e = makeEdge(1, Face::Auto, 2, Face::Auto);
    CHECK(routeEdge(e, ns.lookup())->fromFace == Face::Right);
    ns.add(2, NodeKind::Rectangle, Rect{0, 400, 100, 50}); // the node moved
    CHECK(routeEdge(e, ns.lookup())->fromFace == Face::Bottom);

    // One side fixed: the Auto side still adapts.
    const Edge half = makeEdge(1, Face::Top, 2, Face::Auto);
    const auto p = routeEdge(half, ns.lookup());
    REQUIRE(p);
    CHECK(p->fromFace == Face::Top);
    CHECK(near(p->points.front(), Pt{50, 0}));
}

TEST_CASE("A free point end takes a normal pointing at the other end") {
    Nodes ns;
    ns.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 50});
    Edge e = makeEdge(1, Face::Auto, kNoNode, Face::Auto);
    e.to.point = Pt{500, 25};
    auto p = routeEdge(e, ns.lookup());
    REQUIRE(p);
    CHECK(p->fromFace == Face::Right);
    CHECK(p->toFace == Face::Auto);
    CHECK(near(p->points.back(), Pt{500, 25}));
    CHECK(near(p->endTangent, Pt{1, 0})); // arrives travelling right

    e.to.point = Pt{50, -400};
    p = routeEdge(e, ns.lookup());
    REQUIRE(p);
    CHECK(p->fromFace == Face::Top);
    CHECK(near(p->endTangent, Pt{0, -1}));

    // Free to free.
    Edge ff;
    ff.from.point = Pt{0, 0};
    ff.to.point = Pt{200, 10};
    ff.route = Route::Elbow;
    p = routeEdge(ff, ns.lookup());
    REQUIRE(p);
    CHECK(near(p->points.front(), Pt{0, 0}));
    CHECK(near(p->points.back(), Pt{200, 10}));
    CHECK(axisAligned(p->points));
}

TEST_CASE("An edge to a missing node cannot be routed") {
    Nodes ns;
    ns.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 50});
    CHECK_FALSE(routeEdge(makeEdge(1, Face::Auto, 9, Face::Auto), ns.lookup()));
    CHECK_FALSE(routeEdge(makeEdge(9, Face::Auto, 1, Face::Auto), ns.lookup()));
    CHECK_FALSE(routeEdge(makeEdge(1, Face::Auto, 1, Face::Auto), NodeLookup{}));
}

TEST_CASE("Elbow paths are axis-aligned and start and end on the faces") {
    const Rect base{0, 0, 100, 60};
    const Rect others[] = {
        {300, 20, 100, 60},   {300, -200, 100, 60}, {300, 250, 100, 60}, {-300, 20, 100, 60}, {-300, 250, 100, 60},
        {20, 250, 100, 60},   {20, -250, 100, 60},  {40, 10, 100, 60},   {110, 70, 100, 60},  {0, 0, 100, 60},
        {200, 0, 100, 60},    {-10, 400, 100, 60},
    };
    for (const Rect& o : others) {
        Nodes ns;
        ns.add(1, NodeKind::Rectangle, base);
        ns.add(2, NodeKind::Rectangle, o);
        for (Face fa : kFour) {
            for (Face fb : kFour) {
                const auto p = routeEdge(makeEdge(1, fa, 2, fb, Route::Elbow), ns.lookup());
                REQUIRE(p);
                INFO("other ", o.x, ",", o.y, " faces ", static_cast<int>(fa), static_cast<int>(fb));
                REQUIRE(p->points.size() >= 2);
                CHECK(axisAligned(p->points));
                CHECK(near(p->points.front(), faceAnchor(ns.map[1], fa).point));
                CHECK(near(p->points.back(), faceAnchor(ns.map[2], fb).point));
                // It leaves along the first face's normal and enters against the second's.
                CHECK(near(p->startTangent, faceAnchor(ns.map[1], fa).normal));
                CHECK(near(p->endTangent, Pt{-faceAnchor(ns.map[2], fb).normal.x, -faceAnchor(ns.map[2], fb).normal.y}));
                for (const Pt& q : p->points) {
                    CHECK(finite(q));
                }
            }
        }
        // Auto too.
        const auto a = routeEdge(makeEdge(1, Face::Auto, 2, Face::Auto, Route::Elbow), ns.lookup());
        REQUIRE(a);
        CHECK(axisAligned(a->points));
    }
}

TEST_CASE("Elbow between facing faces is a Z, an L between perpendicular ones, and detours around U-turns") {
    Nodes ns;
    ns.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 60});
    ns.add(2, NodeKind::Rectangle, Rect{300, 100, 100, 60});
    auto z = routeEdge(makeEdge(1, Face::Right, 2, Face::Left, Route::Elbow), ns.lookup());
    REQUIRE(z);
    CHECK(z->points.size() == 4); // out, down, in
    ns.add(2, NodeKind::Rectangle, Rect{300, 0, 100, 60});
    auto line = routeEdge(makeEdge(1, Face::Right, 2, Face::Left, Route::Elbow), ns.lookup());
    REQUIRE(line);
    CHECK(line->points.size() == 2); // aligned: a straight run

    ns.add(2, NodeKind::Rectangle, Rect{300, 200, 100, 60});
    auto l = routeEdge(makeEdge(1, Face::Right, 2, Face::Top, Route::Elbow), ns.lookup());
    REQUIRE(l);
    CHECK(l->points.size() == 3);
    CHECK(near(l->points[1], Pt{350, 30}));

    // Both leave to the right and B is down-left: out, down, then back along B's row.
    ns.add(2, NodeKind::Rectangle, Rect{-300, 200, 100, 60});
    auto u = routeEdge(makeEdge(1, Face::Right, 2, Face::Right, Route::Elbow), ns.lookup());
    REQUIRE(u);
    CHECK(axisAligned(u->points));
    CHECK(u->points.size() == 4);
    CHECK(u->points[1].x > 100.0f);
    // Right face of A to the Left face of B that sits behind A: leave, run along a lane, come back in.
    ns.add(2, NodeKind::Rectangle, Rect{-300, 0, 100, 60});
    auto back = routeEdge(makeEdge(1, Face::Right, 2, Face::Left, Route::Elbow), ns.lookup());
    REQUIRE(back);
    CHECK(axisAligned(back->points));
    CHECK(back->points.size() >= 5);
}

TEST_CASE("Curved routes leave and enter along the face normals") {
    Nodes ns;
    ns.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 60});
    ns.add(2, NodeKind::Rectangle, Rect{400, 200, 100, 60});
    for (Face fa : kFour) {
        for (Face fb : kFour) {
            const auto p = routeEdge(makeEdge(1, fa, 2, fb, Route::Curved), ns.lookup());
            REQUIRE(p);
            INFO("faces ", static_cast<int>(fa), static_cast<int>(fb));
            const Anchor a = faceAnchor(ns.map[1], fa);
            const Anchor b = faceAnchor(ns.map[2], fb);
            CHECK(near(p->points.front(), a.point));
            CHECK(near(p->points.back(), b.point));
            CHECK(near(p->startTangent, a.normal));
            CHECK(near(p->endTangent, Pt{-b.normal.x, -b.normal.y}));
            CHECK(p->points.size() >= 12);
            // The first samples move along the normal (the control arm is at least 24 long).
            const Pt d{p->points[1].x - a.point.x, p->points[1].y - a.point.y};
            CHECK(d.x * a.normal.x + d.y * a.normal.y > 0.0f);
            CHECK(p->length >= std::hypot(b.point.x - a.point.x, b.point.y - a.point.y) - 0.01f);
        }
    }
}

TEST_CASE("Curved control arms are clamped to 24..160") {
    // Two free points far apart on the same row: the curve is a straight line (arms collinear).
    Edge e;
    e.from.point = Pt{0, 0};
    e.to.point = Pt{5000, 0};
    const auto p = routeEdge(e, Nodes{}.lookup());
    REQUIRE(p);
    for (const Pt& q : p->points) {
        CHECK(near(q.y, 0.0f, 0.01f));
        CHECK(q.x >= -0.01f);
        CHECK(q.x <= 5000.01f);
    }
    // Close together the arm is 24, not dist * 0.4: the curve overshoots its end by a visible amount.
    Edge close;
    close.from.point = Pt{0, 0};
    close.to.point = Pt{-10, 0}; // normals: from points left (toward B), to points right (toward A)
    const auto c = routeEdge(close, Nodes{}.lookup());
    REQUIRE(c);
    CHECK(c->points.size() >= 12);
    CHECK(near(c->startTangent, Pt{-1, 0}));
}

TEST_CASE("Straight routes are one segment between the anchors") {
    Nodes ns;
    ns.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 60});
    ns.add(2, NodeKind::Rectangle, Rect{300, 0, 100, 60});
    const auto p = routeEdge(makeEdge(1, Face::Right, 2, Face::Left, Route::Straight), ns.lookup());
    REQUIRE(p);
    REQUIRE(p->points.size() == 2);
    CHECK(near(p->points[0], Pt{100, 30}));
    CHECK(near(p->points[1], Pt{300, 30}));
    CHECK(near(p->length, 200.0f));
    CHECK(near(p->labelPos, Pt{200, 30})); // arc-length midpoint
    CHECK(near(p->startTangent, Pt{1, 0}));
    CHECK(near(p->endTangent, Pt{1, 0}));
}

TEST_CASE("Label anchor is the arc-length midpoint of a bent path") {
    const std::vector<Pt> l{{0, 0}, {100, 0}, {100, 300}}; // total 400, midpoint 200 along: 100 down the second leg
    CHECK(near(pointAtFraction(l, 0.5f), Pt{100, 100}));
    CHECK(near(pointAtFraction(l, 0.0f), Pt{0, 0}));
    CHECK(near(pointAtFraction(l, 1.0f), Pt{100, 300}));
    CHECK(near(pointAtFraction(l, -4.0f), Pt{0, 0}));
    CHECK(near(pointAtFraction(l, 9.0f), Pt{100, 300}));
    CHECK(near(pointAtFraction({}, 0.5f), Pt{0, 0}));
    CHECK(near(polylineLength(l), 400.0f));
}

TEST_CASE("Self-loops wrap a corner for every route and face combination") {
    Nodes ns;
    const Rect r{0, 0, 160, 80};
    ns.add(1, NodeKind::Rectangle, r);
    for (Route route : {Route::Curved, Route::Elbow, Route::Straight}) {
        const auto p = routeEdge(makeEdge(1, Face::Auto, 1, Face::Auto, route), ns.lookup());
        REQUIRE(p);
        INFO("route ", static_cast<int>(route));
        CHECK(p->fromFace == Face::Right);
        CHECK(p->toFace == Face::Top);
        CHECK(near(p->points.front(), Pt{160, 40}));
        CHECK(near(p->points.back(), Pt{80, 0}));
        if (route != Route::Straight) {
            // The loop leaves the node: some point is beyond the right edge and some above the top edge.
            bool right = false, above = false;
            for (const Pt& q : p->points) {
                right = right || q.x > 160.5f;
                above = above || q.y < -0.5f;
            }
            CHECK(right);
            CHECK(above);
        }
        if (route == Route::Elbow) {
            CHECK(axisAligned(p->points));
        }
    }
    // Equal or opposite faces still wrap one corner; every combination routes and stays finite.
    for (Face fa : kFour) {
        for (Face fb : kFour) {
            for (Route route : {Route::Curved, Route::Elbow}) {
                const auto p = routeEdge(makeEdge(1, fa, 1, fb, route), ns.lookup());
                REQUIRE(p);
                CHECK(p->fromFace == fa);
                CHECK(p->toFace != p->fromFace);
                CHECK(p->toFace != Face::Auto);
                for (const Pt& q : p->points) {
                    CHECK(finite(q));
                }
                if (route == Route::Elbow) {
                    CHECK(axisAligned(p->points));
                }
            }
        }
    }
    // One Auto side adapts to the fixed one.
    CHECK(routeEdge(makeEdge(1, Face::Left, 1, Face::Auto), ns.lookup())->toFace != Face::Left);
    CHECK(routeEdge(makeEdge(1, Face::Auto, 1, Face::Bottom), ns.lookup())->fromFace != Face::Bottom);
}

TEST_CASE("Arrowheads align with the direction and sit on the tip") {
    const Pt tip{100, 50};
    const ArrowHead a = arrowHead(Arrow::Arrow, tip, Pt{1, 0}, 2.0f);
    REQUIRE(a.points.size() == 3);
    CHECK(a.filled);
    CHECK(a.closed);
    CHECK(near(a.points[0], tip));
    CHECK(a.points[1].x < tip.x);
    CHECK(a.points[2].x < tip.x);
    CHECK(near(a.points[1].x, a.points[2].x));
    CHECK(near(a.points[1].y + a.points[2].y, 100.0f)); // symmetric about the axis
    CHECK(a.inset > 0.0f);

    const ArrowHead down = arrowHead(Arrow::Arrow, tip, Pt{0, 10}, 2.0f); // direction need not be unit
    CHECK(near(down.points[0], tip));
    CHECK(down.points[1].y < tip.y);

    const ArrowHead open = arrowHead(Arrow::Open, tip, Pt{1, 0}, 2.0f);
    REQUIRE(open.points.size() == 3);
    CHECK_FALSE(open.filled);
    CHECK_FALSE(open.closed);
    CHECK(near(open.points[1], tip));

    const ArrowHead dot = arrowHead(Arrow::Dot, tip, Pt{1, 0}, 2.0f);
    CHECK(dot.points.size() >= 8);
    CHECK(dot.filled);
    float maxX = -1.0e9f;
    for (const Pt& p : dot.points) {
        maxX = std::max(maxX, p.x);
    }
    CHECK(near(maxX, tip.x, 0.05f)); // touches the tip

    CHECK(arrowHead(Arrow::None, tip, Pt{1, 0}, 2.0f).points.empty());
    // A bigger stroke gives a bigger head.
    const ArrowHead big = arrowHead(Arrow::Arrow, tip, Pt{1, 0}, 16.0f);
    CHECK(tip.x - big.points[1].x > tip.x - a.points[1].x);
    // Degenerate direction and NaN width stay finite.
    const ArrowHead odd = arrowHead(Arrow::Arrow, tip, Pt{0, 0}, std::numeric_limits<float>::quiet_NaN());
    CHECK(odd.points.size() == 3);
    for (const Pt& p : odd.points) {
        CHECK(finite(p));
    }
}

TEST_CASE("arrowHeadFor points the start head away from the path") {
    Nodes ns;
    ns.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 60});
    ns.add(2, NodeKind::Rectangle, Rect{300, 0, 100, 60});
    const auto p = routeEdge(makeEdge(1, Face::Right, 2, Face::Left, Route::Straight), ns.lookup());
    REQUIRE(p);
    const ArrowHead end = arrowHeadFor(*p, false, Arrow::Arrow, 2.0f);
    const ArrowHead start = arrowHeadFor(*p, true, Arrow::Arrow, 2.0f);
    CHECK(near(end.points[0], Pt{300, 30}));
    CHECK(end.points[1].x < 300.0f);
    CHECK(near(start.points[0], Pt{100, 30}));
    CHECK(start.points[1].x > 100.0f);
    CHECK(arrowHeadFor(EdgePath{}, true, Arrow::Arrow, 2.0f).points.empty());
}

TEST_CASE("Node hit tests follow the outline, not the bounding box") {
    const Node ell = makeNode(1, NodeKind::Ellipse, Rect{0, 0, 100, 100});
    CHECK(hitNode(ell, Pt{50, 50}));
    CHECK_FALSE(hitNode(ell, Pt{2, 2})); // inside the bbox corner, outside the ellipse
    CHECK(hitNode(ell, Pt{2, 2}, 20.0f)); // tolerance reaches it
    CHECK_FALSE(hitNode(ell, Pt{-30, 50}, 5.0f));

    const Node dia = makeNode(2, NodeKind::Diamond, Rect{0, 0, 100, 100});
    CHECK(hitNode(dia, Pt{50, 50}));
    CHECK_FALSE(hitNode(dia, Pt{10, 10}));
    CHECK(hitNode(dia, Pt{30, 50}));

    const Node rect = makeNode(3, NodeKind::Rectangle, Rect{0, 0, 100, 100});
    CHECK(hitNode(rect, Pt{0, 0}));
    CHECK(hitNode(rect, Pt{100, 100}));
    CHECK_FALSE(hitNode(rect, Pt{100.5f, 50}));
    CHECK(hitNode(rect, Pt{104.0f, 50}, 5.0f));

    const Node db = makeNode(4, NodeKind::Database, Rect{0, 0, 100, 100});
    CHECK(hitNode(db, Pt{50, 50}));
    CHECK_FALSE(hitNode(db, Pt{1, 1}));
}

TEST_CASE("Node hits at tolerance extremes") {
    const Node n = makeNode(1, NodeKind::Hexagon, Rect{0, 0, 100, 100});
    CHECK(hitNode(n, Pt{50, 50}, 0.0f));
    CHECK_FALSE(hitNode(n, Pt{50, 150}, 0.0f));
    CHECK_FALSE(hitNode(n, Pt{50, 150}, -5.0f)); // negative acts as 0
    CHECK_FALSE(hitNode(n, Pt{50, 150}, std::numeric_limits<float>::quiet_NaN()));
    CHECK(hitNode(n, Pt{50, 101}, 2.0f)); // just outside, within a tight tolerance
    CHECK_FALSE(hitNode(n, Pt{50, 110}, 2.0f));
    CHECK(hitNode(n, Pt{50, 150}, 1.0e30f)); // a huge tolerance is capped, not an overflow
    CHECK(hitNode(n, Pt{50, 150}, std::numeric_limits<float>::infinity()));
}

TEST_CASE("pointInPolygon basics") {
    const std::vector<Pt> tri{{0, 0}, {10, 0}, {0, 10}};
    CHECK(pointInPolygon(tri, Pt{2, 2}));
    CHECK_FALSE(pointInPolygon(tri, Pt{8, 8}));
    CHECK_FALSE(pointInPolygon(std::vector<Pt>{{0, 0}, {1, 1}}, Pt{0, 0}));
}

TEST_CASE("Resize handles: eight positions, nearest wins, radius is respected") {
    const Rect r{0, 0, 100, 50};
    CHECK(resizeHandlePos(r, ResizeHandle::TopLeft) == Pt{0, 0});
    CHECK(resizeHandlePos(r, ResizeHandle::Top) == Pt{50, 0});
    CHECK(resizeHandlePos(r, ResizeHandle::TopRight) == Pt{100, 0});
    CHECK(resizeHandlePos(r, ResizeHandle::Right) == Pt{100, 25});
    CHECK(resizeHandlePos(r, ResizeHandle::BottomRight) == Pt{100, 50});
    CHECK(resizeHandlePos(r, ResizeHandle::Bottom) == Pt{50, 50});
    CHECK(resizeHandlePos(r, ResizeHandle::BottomLeft) == Pt{0, 50});
    CHECK(resizeHandlePos(r, ResizeHandle::Left) == Pt{0, 25});

    for (int h = 1; h <= 8; ++h) {
        const auto handle = static_cast<ResizeHandle>(h);
        CHECK(hitResizeHandle(r, resizeHandlePos(r, handle), 6.0f) == handle);
    }
    CHECK(hitResizeHandle(r, Pt{3, 3}, 6.0f) == ResizeHandle::TopLeft);
    CHECK(hitResizeHandle(r, Pt{50, 25}, 6.0f) == ResizeHandle::None);
    CHECK(hitResizeHandle(r, Pt{10, 3}, 6.0f) == ResizeHandle::None);
    CHECK(hitResizeHandle(r, Pt{10, 3}, 0.0f) == ResizeHandle::None);
    CHECK(hitResizeHandle(r, Pt{0, 0}, 0.0f) == ResizeHandle::TopLeft); // exact hit with radius 0

    // A zero-size rect: all handles coincide, the corner wins.
    CHECK(hitResizeHandle(Rect{5, 5, 0, 0}, Pt{5, 5}, 4.0f) == ResizeHandle::TopLeft);
    // Zoom extremes only change the passed radius: 0.05 zoom -> radius 120 world units, 8 zoom -> 0.75.
    CHECK(hitResizeHandle(r, Pt{60, 30}, 120.0f) != ResizeHandle::None);
    CHECK(hitResizeHandle(r, Pt{1, 0}, 0.75f) == ResizeHandle::None);
    CHECK(hitResizeHandle(r, Pt{0.5f, 0}, 0.75f) == ResizeHandle::TopLeft);
}

TEST_CASE("Face handles sit on the anchors") {
    const Node n = makeNode(1, NodeKind::Rectangle, Rect{0, 0, 100, 50});
    CHECK(hitFaceHandle(n, Pt{50, 1}, 6.0f) == Face::Top);
    CHECK(hitFaceHandle(n, Pt{99, 25}, 6.0f) == Face::Right);
    CHECK(hitFaceHandle(n, Pt{50, 49}, 6.0f) == Face::Bottom);
    CHECK(hitFaceHandle(n, Pt{1, 25}, 6.0f) == Face::Left);
    CHECK_FALSE(hitFaceHandle(n, Pt{50, 25}, 6.0f));
    CHECK_FALSE(hitFaceHandle(n, Pt{50, 1}, 0.0f));
    CHECK(hitFaceHandle(n, Pt{50, 0}, 0.0f) == Face::Top);
    CHECK_FALSE(hitFaceHandle(n, Pt{50, 25}, std::numeric_limits<float>::quiet_NaN()));
}

TEST_CASE("Edge proximity is the distance to the sampled path") {
    Nodes ns;
    ns.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 60});
    ns.add(2, NodeKind::Rectangle, Rect{300, 0, 100, 60});
    const auto p = routeEdge(makeEdge(1, Face::Right, 2, Face::Left, Route::Straight), ns.lookup());
    REQUIRE(p);
    CHECK(near(distanceToPath(p->points, Pt{200, 40}), 10.0f));
    CHECK(near(distanceToPath(p->points, Pt{50, 30}), 50.0f)); // before the start: distance to the end point
    CHECK(hitEdge(*p, Pt{200, 40}, 10.5f));
    CHECK_FALSE(hitEdge(*p, Pt{200, 40}, 9.5f));
    CHECK(hitEdge(*p, Pt{200, 30}, 0.0f)); // exactly on it, tolerance 0
    CHECK_FALSE(hitEdge(*p, Pt{200, 30.5f}, 0.0f));
    CHECK(hitEdge(*p, Pt{200, 4000}, 1.0e30f));
    CHECK_FALSE(hitEdge(*p, Pt{200, 40}, -3.0f));
    CHECK_FALSE(hitEdge(*p, Pt{200, 40}, std::numeric_limits<float>::quiet_NaN()));
    CHECK(distanceToPath({}, Pt{1, 1}) > 1.0e20f);
    CHECK_FALSE(hitEdge(EdgePath{}, Pt{1, 1}, 5.0f));

    // A curved edge is hit near its middle, not on the straight line between faces.
    const auto c = routeEdge(makeEdge(1, Face::Bottom, 2, Face::Bottom, Route::Curved), ns.lookup());
    REQUIRE(c);
    CHECK(hitEdge(*c, c->labelPos, 0.5f));
    CHECK_FALSE(hitEdge(*c, Pt{200, 30}, 2.0f));
}

TEST_CASE("Edge endpoint handles pick the nearer end, To on a tie") {
    EdgePath p;
    p.points = {{0, 0}, {100, 0}};
    CHECK(hitEdgeEndpoint(p, Pt{2, 1}, 6.0f) == EdgeEnd::From);
    CHECK(hitEdgeEndpoint(p, Pt{99, 0}, 6.0f) == EdgeEnd::To);
    CHECK(hitEdgeEndpoint(p, Pt{50, 0}, 6.0f) == EdgeEnd::None);
    CHECK(hitEdgeEndpoint(p, Pt{50, 0}, 60.0f) == EdgeEnd::To); // equidistant
    CHECK(hitEdgeEndpoint(p, Pt{0, 0}, 0.0f) == EdgeEnd::From);
    CHECK(hitEdgeEndpoint(p, Pt{1, 0}, 0.0f) == EdgeEnd::None);
    CHECK(hitEdgeEndpoint(EdgePath{}, Pt{0, 0}, 5.0f) == EdgeEnd::None);
}

TEST_CASE("Grid snapping") {
    CHECK(snapToGrid(13.0f, 10.0f) == 10.0f);
    CHECK(snapToGrid(16.0f, 10.0f) == 20.0f);
    CHECK(snapToGrid(-14.0f, 10.0f) == -10.0f);
    CHECK(snapToGrid(-16.0f, 10.0f) == -20.0f);
    CHECK(snapToGrid(13.0f, 0.0f) == 13.0f);
    CHECK(snapToGrid(13.0f, -4.0f) == 13.0f);
    CHECK(snapToGrid(13.0f, std::numeric_limits<float>::quiet_NaN()) == 13.0f);
    CHECK(snapToGrid(13.0f, std::numeric_limits<float>::infinity()) == 13.0f);
    CHECK(snapToGrid(Pt{23, 47}, 20.0f) == Pt{20, 40});

    const Rect r = snapRectPosition(Rect{13, 27, 55, 33}, 10.0f);
    CHECK(r == Rect{10, 30, 55, 33});
    const Rect e = snapRectEdges(Rect{13, 27, 55, 33}, 10.0f);
    CHECK(e == Rect{10, 30, 60, 30}); // right 68 -> 70, bottom 60 -> 60
    // Edges that collapse onto one grid line give a zero size, never negative.
    const Rect tiny = snapRectEdges(Rect{11, 11, 2, 2}, 10.0f);
    CHECK(tiny.w == 0.0f);
    CHECK(tiny.h == 0.0f);
    // A huge value is clamped, then snapped.
    CHECK(std::abs(snapToGrid(1.0e30f, 7.0f)) <= kMaxCoord);
}

TEST_CASE("Smart guides snap to edges and centers within the threshold") {
    const Rect other{100, 100, 100, 50};
    const std::vector<Rect> others{other};

    // Left edge 3 px off the other's left edge.
    SnapResult s = snapToGuides(Rect{97, 300, 60, 40}, others, 5.0f);
    CHECK(s.snappedX);
    CHECK_FALSE(s.snappedY);
    CHECK(near(s.delta.x, 3.0f));
    CHECK(near(s.delta.y, 0.0f));
    REQUIRE(s.guides.size() == 1);
    CHECK(s.guides[0].vertical);
    CHECK(near(s.guides[0].pos, 100.0f));
    CHECK(near(s.guides[0].from, 100.0f)); // spans both rects vertically
    CHECK(near(s.guides[0].to, 340.0f));

    // Outside the threshold: nothing.
    s = snapToGuides(Rect{80, 300, 60, 40}, others, 5.0f);
    CHECK_FALSE(s.snappedX);
    CHECK(s.guides.empty());
    CHECK(near(s.delta.x, 0.0f));

    // Centers align: moving center x = 150 + 2.
    s = snapToGuides(Rect{122, 300, 60, 40}, others, 5.0f); // center 152 vs other center 150, right 182 no
    CHECK(s.snappedX);
    CHECK(near(s.delta.x, -2.0f));

    // Both axes at once, with a horizontal guide too: top 148 vs other bottom 150.
    s = snapToGuides(Rect{100, 148, 60, 40}, others, 5.0f);
    CHECK(s.snappedX);
    CHECK(s.snappedY);
    CHECK(near(s.delta.y, 2.0f));
    bool horizontal = false;
    for (const Guide& g : s.guides) {
        horizontal = horizontal || (!g.vertical && near(g.pos, 150.0f));
    }
    CHECK(horizontal);

    // The nearest alignment wins when several are in range.
    const std::vector<Rect> two{Rect{100, 0, 10, 10}, Rect{104, 0, 10, 10}};
    s = snapToGuides(Rect{103, 300, 50, 10}, two, 5.0f);
    CHECK(near(s.delta.x, 1.0f)); // 104 is 1 away, 100 is 3 away

    // Threshold 0 only reports exact alignments.
    s = snapToGuides(Rect{100, 300, 60, 40}, others, 0.0f);
    CHECK(s.snappedX);
    CHECK(near(s.delta.x, 0.0f));
    CHECK_FALSE(snapToGuides(Rect{101, 300, 60, 40}, others, 0.0f).snappedX);

    // No others.
    CHECK_FALSE(snapToGuides(Rect{0, 0, 10, 10}, {}, 8.0f).snappedX);

    // Several rects aligned on one line merge into a single guide that spans all of them.
    const std::vector<Rect> row{Rect{100, 0, 20, 20}, Rect{100, 200, 20, 20}};
    s = snapToGuides(Rect{101, 100, 30, 20}, row, 5.0f);
    REQUIRE(s.guides.size() == 1);
    CHECK(near(s.guides[0].from, 0.0f));
    CHECK(near(s.guides[0].to, 220.0f));
}

TEST_CASE("NaN, infinity and huge inputs never produce NaN or hang") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(sanitizeCoord(nan) == 0.0f);
    CHECK(sanitizeCoord(inf) == kMaxCoord);
    CHECK(sanitizeCoord(-inf) == -kMaxCoord);
    CHECK(sanitizeCoord(1.0e30f) == kMaxCoord);
    const Rect sr = sanitize(Rect{nan, inf, -5.0f, nan});
    CHECK(sr == Rect{0, kMaxCoord, 0, 0});

    const Rect hostile[] = {{nan, nan, nan, nan}, {inf, -inf, inf, inf}, {1e30f, -1e30f, 1e30f, 1e30f}, {0, 0, -50, -50}};
    for (const Rect& r : hostile) {
        for (NodeKind kind : kAllKinds) {
            for (const Pt& p : outlineFor(kind, r, nan)) {
                CHECK(finite(p));
                CHECK(std::abs(p.x) <= kMaxCoord * 2.01f);
            }
            for (Face f : kFour) {
                const Anchor a = faceAnchor(kind, r, f);
                CHECK(finite(a.point));
            }
        }
        Nodes ns;
        ns.add(1, NodeKind::Diamond, r);
        ns.add(2, NodeKind::Ellipse, Rect{10, 10, 50, 50});
        for (Route route : {Route::Curved, Route::Elbow, Route::Straight}) {
            for (Face fa : {Face::Auto, Face::Top}) {
                const auto p = routeEdge(makeEdge(1, fa, 2, Face::Auto, route), ns.lookup());
                REQUIRE(p);
                CHECK(finite(p->labelPos));
                CHECK(finite(p->startTangent));
                CHECK(finite(p->endTangent));
                CHECK(std::isfinite(p->length));
                for (const Pt& q : p->points) {
                    CHECK(finite(q));
                }
            }
            const auto self = routeEdge(makeEdge(1, Face::Auto, 1, Face::Auto, route), ns.lookup());
            REQUIRE(self);
            for (const Pt& q : self->points) {
                CHECK(finite(q));
            }
        }
        const Node n = ns.map[1];
        CHECK_NOTHROW(hitNode(n, Pt{nan, nan}, nan));
        CHECK_NOTHROW(hitResizeHandle(r, Pt{nan, inf}, inf));
        CHECK_NOTHROW(hitFaceHandle(n, Pt{inf, inf}, nan));
    }

    // Free points with hostile coordinates.
    Edge e;
    e.from.point = Pt{nan, inf};
    e.to.point = Pt{-inf, 1e30f};
    for (Route route : {Route::Curved, Route::Elbow, Route::Straight}) {
        e.route = route;
        const auto p = routeEdge(e, NodeLookup{});
        REQUIRE(p);
        CHECK(finite(p->labelPos));
        CHECK(std::isfinite(p->length));
        for (const Pt& q : p->points) {
            CHECK(finite(q));
        }
        CHECK(hitEdge(*p, Pt{nan, nan}, nan) == hitEdge(*p, Pt{0, 0}, 0.0f));
        CHECK_NOTHROW(hitEdgeEndpoint(*p, Pt{nan, nan}, inf));
    }

    // Snapping.
    const SnapResult s = snapToGuides(Rect{nan, inf, nan, 5}, std::vector<Rect>{Rect{nan, 0, inf, 1}}, nan);
    CHECK(finite(s.delta));
    for (const Guide& g : s.guides) {
        CHECK(std::isfinite(g.pos));
        CHECK(std::isfinite(g.from));
        CHECK(std::isfinite(g.to));
    }
    CHECK(finite(snapToGrid(Pt{nan, inf}, 10.0f)));
    const Rect g = snapRectEdges(Rect{nan, inf, nan, -inf}, 10.0f);
    CHECK(std::isfinite(g.w));
    CHECK(std::isfinite(g.h));

    const ArrowHead h = arrowHead(Arrow::Dot, Pt{nan, inf}, Pt{inf, nan}, inf);
    for (const Pt& p : h.points) {
        CHECK(finite(p));
    }
}

TEST_CASE("Routing a very large board stays fast and finite") {
    Nodes ns;
    ns.add(1, NodeKind::Rectangle, Rect{-kMaxCoord, -kMaxCoord, 100, 100});
    ns.add(2, NodeKind::Rectangle, Rect{kMaxCoord - 100, kMaxCoord - 100, 100, 100});
    for (Route route : {Route::Curved, Route::Elbow, Route::Straight}) {
        const auto p = routeEdge(makeEdge(1, Face::Auto, 2, Face::Auto, route), ns.lookup());
        REQUIRE(p);
        CHECK(p->points.size() < 200);
        for (const Pt& q : p->points) {
            CHECK(finite(q));
        }
    }
}

TEST_CASE("A polygon has one corner per side, inside its rect, with the first corner at the top") {
    for (int sides = kMinPolygonSides; sides <= kMaxPolygonSides; ++sides) {
        CAPTURE(sides);
        const Rect r{10, 20, 200, 100};
        const std::vector<Pt> pts = outlineFor(NodeKind::Polygon, r, 8.0f, sides);
        REQUIRE(pts.size() == static_cast<std::size_t>(sides));
        CHECK(near(pts[0], Pt{110, 20}));
        for (const Pt& q : pts) {
            CHECK(q.x >= r.x - 1e-3f);
            CHECK(q.x <= r.right() + 1e-3f);
            CHECK(q.y >= r.y - 1e-3f);
            CHECK(q.y <= r.bottom() + 1e-3f);
        }
    }
    // Out-of-range counts are clamped, never crash.
    CHECK(outlineFor(NodeKind::Polygon, Rect{0, 0, 50, 50}, 8.0f, 0).size() == static_cast<std::size_t>(kMinPolygonSides));
    CHECK(outlineFor(NodeKind::Polygon, Rect{0, 0, 50, 50}, 8.0f, 1000).size() == static_cast<std::size_t>(kMaxPolygonSides));
}

TEST_CASE("Polygon face anchors lie on the outline along each axis") {
    for (int sides : {3, 4, 5, 6, 7, 12}) {
        CAPTURE(sides);
        const Rect r{0, 0, 120, 100};
        const Pt c = r.center();
        for (Face f : kFour) {
            const Anchor a = faceAnchor(NodeKind::Polygon, r, f, sides);
            CHECK(finite(a.point));
            // On the axis through the center: the anchor shares x (Top, Bottom) or y (Left, Right) with it.
            if (f == Face::Top || f == Face::Bottom) {
                CHECK(near(a.point.x, c.x, 1e-2f));
            } else {
                CHECK(near(a.point.y, c.y, 1e-2f));
            }
            // Inside the rect, and on the outside of the center along the normal.
            CHECK(a.point.x >= -1e-2f);
            CHECK(a.point.x <= 120 + 1e-2f);
            CHECK(a.point.y >= -1e-2f);
            CHECK(a.point.y <= 100 + 1e-2f);
            CHECK((a.point.x - c.x) * a.normal.x + (a.point.y - c.y) * a.normal.y > 0.0f);
        }
    }
    // A square polygon's top is its first corner.
    CHECK(near(faceAnchor(NodeKind::Polygon, Rect{0, 0, 100, 100}, Face::Top, 5).point, Pt{50, 0}));
}

TEST_CASE("A polygon node uses its own side count for outline and anchors") {
    Node n = makeNode(1, NodeKind::Polygon, Rect{0, 0, 100, 100});
    n.sides = 8;
    CHECK(nodeOutline(n).size() == 8);
    CHECK(near(faceAnchor(n, Face::Right).point, faceAnchor(NodeKind::Polygon, n.rect, Face::Right, 8).point));
    CHECK(hitNode(n, Pt{50, 50}, 0.0f));
    CHECK_FALSE(hitNode(n, Pt{1, 1}, 0.0f)); // an octagon cuts the corner of its box
}

TEST_CASE("Label placement follows the line, beside it or on it") {
    const std::vector<Pt> horizontal{{0, 0}, {100, 0}};
    const std::vector<Pt> vertical{{0, 0}, {0, 100}};

    const LabelPlacement on = placeLabel(horizontal, 0.5f, false, LabelSide::On, 20, 10);
    CHECK(near(on.center, Pt{50, 0}));
    CHECK(near(on.angle, 0.0f));

    const LabelPlacement above = placeLabel(horizontal, 0.5f, false, LabelSide::Above, 20, 10);
    const LabelPlacement below = placeLabel(horizontal, 0.5f, false, LabelSide::Below, 20, 10);
    CHECK(above.center.y < -10.0f); // clear of the line by at least the half height
    CHECK(below.center.y > 10.0f);
    CHECK(near(above.center.x, 50.0f));
    CHECK(near(above.center.y, -below.center.y));

    // Beside a vertical line, a horizontal label moves sideways by its half width.
    const LabelPlacement side = placeLabel(vertical, 0.5f, false, LabelSide::Above, 20, 10);
    CHECK(side.center.x >= 20.0f);
    CHECK(near(side.center.y, 50.0f));

    // Turned labels keep reading left to right or top to bottom, never upside down.
    CHECK(near(placeLabel(vertical, 0.5f, true, LabelSide::On, 20, 10).angle, 1.5707963f, 1e-4f));
    const std::vector<Pt> up{{0, 100}, {0, 0}};
    CHECK(near(placeLabel(up, 0.5f, true, LabelSide::On, 20, 10).angle, 1.5707963f, 1e-4f));
    const std::vector<Pt> leftward{{100, 0}, {0, 0}};
    CHECK(near(placeLabel(leftward, 0.5f, true, LabelSide::On, 20, 10).angle, 0.0f, 1e-4f));
    const std::vector<Pt> diagonal{{0, 0}, {100, 100}};
    CHECK(near(placeLabel(diagonal, 0.5f, true, LabelSide::On, 20, 10).angle, 0.7853982f, 1e-4f));

    // Out-of-range and NaN positions are clamped; empty paths do not crash.
    CHECK(near(placeLabel(horizontal, -3.0f, false, LabelSide::On, 1, 1).center, Pt{0, 0}));
    CHECK(near(placeLabel(horizontal, 9.0f, false, LabelSide::On, 1, 1).center, Pt{100, 0}));
    CHECK(near(placeLabel(horizontal, std::nanf(""), false, LabelSide::On, 1, 1).center, Pt{50, 0}));
    CHECK(finite(placeLabel({}, 0.5f, true, LabelSide::Above, 1, 1).center));
}

TEST_CASE("Projecting a point on a path gives its fraction and side") {
    const std::vector<Pt> path{{0, 0}, {100, 0}, {100, 100}};
    const PathProjection a = projectOnPath(path, Pt{50, -30});
    CHECK(near(a.t, 0.25f, 1e-3f));
    CHECK(near(a.distance, 30.0f));
    CHECK(a.side > 0.0f); // screen-up is the Above side
    const PathProjection b = projectOnPath(path, Pt{50, 30});
    CHECK(b.side < 0.0f);
    CHECK(near(projectOnPath(path, Pt{100, 150}).t, 1.0f));
    CHECK(near(projectOnPath(path, Pt{-50, 0}).t, 0.0f));
    CHECK(near(projectOnPath({}, Pt{1, 1}).t, 0.5f));
    CHECK(finite(projectOnPath(path, Pt{1e30f, -1e30f}).nearest));
    CHECK(near(tangentAtFraction(path, 0.25f), Pt{1, 0}));
    CHECK(near(tangentAtFraction(path, 0.75f), Pt{0, 1}));
}

// --- manual routes (Edge::via) ---------------------------------------------------------------------------------

TEST_CASE("A straight connector bends through its offset midpoint") {
    Nodes n;
    n.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 50});
    n.add(2, NodeKind::Rectangle, Rect{300, 0, 100, 50});
    Edge e = makeEdge(1, Face::Right, 2, Face::Left, Route::Straight);
    const std::optional<EdgePath> automatic = routeEdge(e, n.lookup());
    REQUIRE(automatic.has_value());
    REQUIRE(automatic->points.size() == 2);
    CHECK(automatic->hasHandle);
    CHECK(near(automatic->handle, Pt{200, 25})); // the handle sits on the middle of the line
    CHECK(near(automatic->handleBase, automatic->handle));

    e.via = Pt{0, -60};
    const std::optional<EdgePath> bent = routeEdge(e, n.lookup());
    REQUIRE(bent.has_value());
    REQUIRE(bent->points.size() == 3);
    CHECK(near(bent->points[0], Pt{100, 25}));
    CHECK(near(bent->points[1], Pt{200, -35}));
    CHECK(near(bent->points[2], Pt{300, 25}));
    CHECK(near(bent->handle, Pt{200, -35}));
    CHECK(bent->length > automatic->length);
    // The tangents follow the two legs.
    CHECK(bent->startTangent.y < 0.0f);
    CHECK(bent->endTangent.y > 0.0f);
    // The pointer position that makes this offset gives it back.
    CHECK(near(viaFromPoint(*automatic, Pt{200, -35}), Pt{0, -60}));
}

TEST_CASE("A curved connector with a zero offset is the automatic curve; an offset moves its middle") {
    Nodes n;
    n.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 50});
    n.add(2, NodeKind::Rectangle, Rect{300, 150, 100, 50});
    Edge e = makeEdge(1, Face::Right, 2, Face::Left, Route::Curved);
    const std::optional<EdgePath> automatic = routeEdge(e, n.lookup());
    e.via = Pt{0, 0};
    const std::optional<EdgePath> zero = routeEdge(e, n.lookup());
    REQUIRE(automatic.has_value());
    REQUIRE(zero.has_value());
    // Same curve, sampled differently: every point of one is close to the other.
    for (const Pt& p : zero->points)
        CHECK(distanceToPath(automatic->points, p) < 1.5f);
    CHECK(near(zero->points.front(), automatic->points.front()));
    CHECK(near(zero->points.back(), automatic->points.back()));

    e.via = Pt{20, -80};
    const std::optional<EdgePath> bent = routeEdge(e, n.lookup());
    REQUIRE(bent.has_value());
    CHECK(near(bent->points.front(), automatic->points.front()));
    CHECK(near(bent->points.back(), automatic->points.back()));
    CHECK(near(bent->handle, Pt{automatic->handleBase.x + 20, automatic->handleBase.y - 80}));
    CHECK(distanceToPath(bent->points, bent->handle) < 1.0f); // the curve passes through its handle
    CHECK(near(bent->startTangent, Pt{1, 0}));                // it still leaves and enters through the faces
    CHECK(near(bent->endTangent, Pt{1, 0}));
    for (const Pt& p : bent->points)
        CHECK(finite(p));
}

TEST_CASE("An elbow slides its middle segment along its normal and stays orthogonal") {
    Nodes n;
    n.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 50});
    n.add(2, NodeKind::Rectangle, Rect{300, 150, 100, 50});
    Edge e = makeEdge(1, Face::Right, 2, Face::Left, Route::Elbow);
    const std::optional<EdgePath> automatic = routeEdge(e, n.lookup());
    REQUIRE(automatic.has_value());
    REQUIRE(automatic->hasHandle);
    CHECK(near(automatic->handleAxis, Pt{1, 0})); // a vertical segment slides sideways
    CHECK(near(automatic->handle.x, 200.0f));

    e.via = Pt{50, 999}; // only the part along the axis counts
    const std::optional<EdgePath> moved = routeEdge(e, n.lookup());
    REQUIRE(moved.has_value());
    CHECK(axisAligned(moved->points));
    CHECK(near(moved->handle.x, 250.0f));
    CHECK(near(moved->points.front(), automatic->points.front()));
    CHECK(near(moved->points.back(), automatic->points.back()));
    bool onSegment = false;
    for (std::size_t i = 1; i < moved->points.size(); ++i)
        onSegment = onSegment || (near(moved->points[i].x, 250.0f) && near(moved->points[i - 1].x, 250.0f));
    CHECK(onSegment);

    // It cannot be pushed past the faces: the segment stays between the two nodes.
    e.via = Pt{5000, 0};
    const std::optional<EdgePath> far = routeEdge(e, n.lookup());
    REQUIRE(far.has_value());
    CHECK(axisAligned(far->points));
    CHECK(far->handle.x < 300.0f);
    CHECK(far->handle.x > 100.0f);

    // The pointer position that makes this offset gives it back, whatever the pointer does across the segment.
    CHECK(near(viaFromPoint(*automatic, Pt{250, -400}), Pt{50, 0}));
}

TEST_CASE("Elbow detours and loops move too, a plain L has no handle") {
    Nodes n;
    n.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 50});
    n.add(2, NodeKind::Rectangle, Rect{300, 150, 100, 50});
    // L: a right face to a top face, B ahead and below.
    Edge l = makeEdge(1, Face::Right, 2, Face::Top, Route::Elbow);
    l.via = Pt{30, 30};
    const std::optional<EdgePath> lp = routeEdge(l, n.lookup());
    REQUIRE(lp.has_value());
    CHECK_FALSE(lp->hasHandle);
    l.via.reset();
    CHECK(routeEdge(l, n.lookup())->points == lp->points); // the offset changed nothing

    // U: both faces look the same way.
    Edge u = makeEdge(1, Face::Right, 2, Face::Right, Route::Elbow);
    const std::optional<EdgePath> ua = routeEdge(u, n.lookup());
    u.via = Pt{40, 0};
    const std::optional<EdgePath> ub = routeEdge(u, n.lookup());
    REQUIRE(ua.has_value());
    REQUIRE(ub.has_value());
    CHECK(ua->hasHandle);
    CHECK(axisAligned(ub->points));
    CHECK(near(ub->handle.x, ua->handle.x + 40.0f));

    // Detour: B is behind A, the lane between them can move across.
    Nodes back;
    back.add(1, NodeKind::Rectangle, Rect{300, 0, 100, 50});
    back.add(2, NodeKind::Rectangle, Rect{0, 20, 100, 50});
    Edge d = makeEdge(1, Face::Right, 2, Face::Left, Route::Elbow);
    const std::optional<EdgePath> da = routeEdge(d, back.lookup());
    d.via = Pt{0, 30};
    const std::optional<EdgePath> db = routeEdge(d, back.lookup());
    REQUIRE(da.has_value());
    REQUIRE(db.has_value());
    CHECK(da->hasHandle);
    CHECK(axisAligned(db->points));
    CHECK(near(db->handle.y, da->handle.y + 30.0f));
}

TEST_CASE("A manual route follows the shapes when they move") {
    Nodes n;
    n.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 50});
    n.add(2, NodeKind::Rectangle, Rect{300, 150, 100, 50});
    for (const Route route : {Route::Straight, Route::Curved, Route::Elbow}) {
        Edge e = makeEdge(1, Face::Right, 2, Face::Left, route);
        e.via = route == Route::Elbow ? Pt{30, 0} : Pt{25, -40};
        const std::optional<EdgePath> before = routeEdge(e, n.lookup());
        REQUIRE(before.has_value());
        // Move the whole chart: the handle moves by exactly that much.
        Nodes moved;
        moved.add(1, NodeKind::Rectangle, Rect{500, 100, 100, 50});
        moved.add(2, NodeKind::Rectangle, Rect{800, 250, 100, 50});
        const std::optional<EdgePath> after = routeEdge(e, moved.lookup());
        REQUIRE(after.has_value());
        INFO("route ", static_cast<int>(route));
        CHECK(near(after->handle, Pt{before->handle.x + 500, before->handle.y + 100}, 0.05f));
    }
}

TEST_CASE("Hostile offsets never give NaN or infinity") {
    Nodes n;
    n.add(1, NodeKind::Rectangle, Rect{0, 0, 100, 50});
    n.add(2, NodeKind::Rectangle, Rect{300, 150, 100, 50});
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const Route route : {Route::Straight, Route::Curved, Route::Elbow}) {
        for (const Pt via : {Pt{nan, nan}, Pt{inf, -inf}, Pt{1e30f, 1e30f}, Pt{-5e6f, 3e-30f}}) {
            Edge e = makeEdge(1, Face::Auto, 2, Face::Auto, route);
            e.via = via;
            const std::optional<EdgePath> p = routeEdge(e, n.lookup());
            REQUIRE(p.has_value());
            for (const Pt& q : p->points)
                CHECK(finite(q));
            CHECK(finite(p->handle));
            CHECK(std::isfinite(p->length));
        }
    }
    EdgePath empty;
    CHECK(finite(viaFromPoint(empty, Pt{nan, inf})));
    // A free-ended connector can be bent too.
    Edge freeEnded;
    freeEnded.from.point = Pt{0, 0};
    freeEnded.to.point = Pt{200, 100};
    freeEnded.route = Route::Straight;
    freeEnded.via = Pt{0, 50};
    const std::optional<EdgePath> fp = routeEdge(freeEnded, n.lookup());
    REQUIRE(fp.has_value());
    CHECK(fp->points.size() == 3);
}

} // namespace trace::flow
