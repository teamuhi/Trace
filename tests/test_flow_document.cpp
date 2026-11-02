#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "flow/flow_document.hpp"

using namespace trace::flow;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

NodeId addBox(FlowDocument& doc, float x, float y, float w = 100, float h = 50, NodeKind kind = NodeKind::Rectangle) {
    return doc.addNode(kind, Rect{x, y, w, h});
}

NodeId addContainer(FlowDocument& doc, float x, float y, float w = 400, float h = 300) {
    return doc.addNode(NodeKind::Container, Rect{x, y, w, h});
}

ImagePtr makeImage(int w = 4, int h = 4) { return std::make_shared<const trace::Image>(w, h); }

// Every invariant the header promises, checked after the hostile tests.
void checkInvariants(const FlowDocument& doc) {
    std::set<NodeId> ids;
    std::set<std::uint32_t> zs;
    std::map<GroupId, int> groups;
    for (const Node& n : doc.nodes()) {
        CHECK(n.id != kNoNode);
        CHECK(ids.insert(n.id).second);
        CHECK(zs.insert(n.z).second);
        CHECK(std::isfinite(n.rect.x));
        CHECK(std::isfinite(n.rect.y));
        CHECK(std::fabs(n.rect.x) <= kMaxCoord);
        CHECK(std::fabs(n.rect.y) <= kMaxCoord);
        CHECK(n.rect.w >= kMinNodeSize);
        CHECK(n.rect.h >= kMinNodeSize);
        CHECK(n.text.size() <= kMaxTextBytes);
        if (n.groupId != kNoGroup) ++groups[n.groupId];
        if (n.parentId != kNoNode) {
            const Node* p = doc.node(n.parentId);
            REQUIRE(p != nullptr);
            CHECK(p->kind == NodeKind::Container);
        }
        if (n.imageId != kNoImage) {
            CHECK(n.kind == NodeKind::Image);
            CHECK(doc.image(n.imageId).get() != nullptr);
        }
    }
    for (const auto& g : groups) CHECK(g.second >= 2);
    for (const Edge& e : doc.edges()) {
        if (e.from.node != kNoNode) CHECK(doc.node(e.from.node) != nullptr);
        if (e.to.node != kNoNode) CHECK(doc.node(e.to.node) != nullptr);
    }
}

} // namespace

TEST_CASE("flow document starts empty") {
    FlowDocument doc;
    CHECK(doc.empty());
    CHECK(doc.nodes().empty());
    CHECK(doc.edges().empty());
    CHECK(doc.revision() == 0);
    CHECK(doc.node(1) == nullptr);
    CHECK(doc.edge(1) == nullptr);
    CHECK(doc.image(1).get() == nullptr);
    CHECK(doc.nodesByZ().empty());
}

TEST_CASE("addNode assigns ids and z, bumps the revision and stores the data") {
    FlowDocument doc;
    Style st;
    st.fill = trace::Rgba8{1, 2, 3, 255};
    st.strokeWidth = 4.0f;
    const NodeId a = doc.addNode(NodeKind::Diamond, Rect{10, 20, 120, 60}, "yes?", st);
    const NodeId b = addBox(doc, 0, 0);
    CHECK(a == 1);
    CHECK(b == 2);
    CHECK(doc.revision() == 2);
    CHECK_FALSE(doc.empty());
    const Node* n = doc.node(a);
    REQUIRE(n != nullptr);
    CHECK(n->kind == NodeKind::Diamond);
    CHECK(n->rect == Rect{10, 20, 120, 60});
    CHECK(n->text == "yes?");
    CHECK(n->style == st);
    CHECK(n->z < doc.node(b)->z);
    CHECK(doc.nodes().size() == 2);
    CHECK(doc.nodes()[0].id == a); // insertion order
    checkInvariants(doc);
}

TEST_CASE("ids are never reused after removal") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 0, 0);
    const NodeId ids[] = {b};
    CHECK(doc.removeNodes(ids) == 1);
    const NodeId c = addBox(doc, 0, 0);
    CHECK(c > b);
    CHECK(c != a);
    const EdgeId e1 = doc.connect(End{a}, End{c});
    const EdgeId rm[] = {e1};
    CHECK(doc.removeEdges(rm) == 1);
    const EdgeId e2 = doc.connect(End{a}, End{c});
    CHECK(e2 > e1);
}

TEST_CASE("addNode sanitises hostile rects") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{kNaN, kInf, -kInf, kNaN});
    const Node* n = doc.node(a);
    REQUIRE(n != nullptr);
    CHECK(n->rect.x == 0.0f);
    CHECK(n->rect.y == kMaxCoord);
    CHECK(n->rect.w == kMinNodeSize);
    CHECK(n->rect.h == kMinNodeSize);

    const NodeId b = doc.addNode(NodeKind::Rectangle, Rect{-1e30f, 1e30f, 1e30f, 0.0f});
    const Node* m = doc.node(b);
    REQUIRE(m != nullptr);
    CHECK(m->rect.x == -kMaxCoord);
    CHECK(m->rect.y == kMaxCoord);
    CHECK(m->rect.w == kMaxCoord);
    CHECK(m->rect.h == kMinNodeSize);

    const NodeId c = doc.addNode(NodeKind::Rectangle, Rect{5, 5, 2, 3});
    CHECK(doc.node(c)->rect.w == kMinNodeSize);
    CHECK(doc.node(c)->rect.h == kMinNodeSize);
    checkInvariants(doc);
}

TEST_CASE("addNode clamps the style") {
    FlowDocument doc;
    Style st;
    st.strokeWidth = 1000.0f;
    st.cornerRadius = -5.0f;
    st.fontSize = 1.0f;
    const Style def;
    const Node* n = doc.node(doc.addNode(NodeKind::Rounded, Rect{0, 0, 50, 50}, {}, st));
    REQUIRE(n != nullptr);
    CHECK(n->style.strokeWidth == 32.0f);
    CHECK(n->style.cornerRadius == 0.0f);
    CHECK(n->style.fontSize == 6.0f);

    st.strokeWidth = 0.0f;
    st.cornerRadius = 1e9f;
    st.fontSize = kInf;
    n = doc.node(doc.addNode(NodeKind::Rounded, Rect{0, 0, 50, 50}, {}, st));
    CHECK(n->style.strokeWidth == 0.5f);
    CHECK(n->style.cornerRadius == 200.0f);
    CHECK(n->style.fontSize == 200.0f);

    st.strokeWidth = kNaN;
    st.cornerRadius = kNaN;
    st.fontSize = kNaN;
    st.dash = static_cast<Dash>(77);
    st.align = static_cast<TextAlign>(77);
    n = doc.node(doc.addNode(NodeKind::Rounded, Rect{0, 0, 50, 50}, {}, st));
    CHECK(n->style == def);
}

TEST_CASE("addNode refuses an invalid kind") {
    FlowDocument doc;
    CHECK(doc.addNode(static_cast<NodeKind>(200), Rect{0, 0, 10, 10}) == kNoNode);
    CHECK(doc.addNode(static_cast<NodeKind>(kNodeKindCount), Rect{0, 0, 10, 10}) == kNoNode);
    CHECK(doc.empty());
    CHECK(doc.revision() == 0);
}

TEST_CASE("text is truncated without splitting a UTF-8 sequence") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 50, 50}, std::string(kMaxTextBytes + 500, 'a'));
    CHECK(doc.node(a)->text.size() == kMaxTextBytes);

    // Three-byte characters (euro sign): 16384 is not a multiple of 3, so the cut lands mid-sequence.
    std::string euros;
    while (euros.size() < kMaxTextBytes + 30) euros += "\xE2\x82\xAC";
    const NodeId b = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 50, 50}, euros);
    const std::string& t = doc.node(b)->text;
    CHECK(t.size() <= kMaxTextBytes);
    CHECK(t.size() % 3 == 0);
    CHECK(t.size() == (kMaxTextBytes / 3) * 3);
    CHECK(euros.compare(0, t.size(), t) == 0);

    // Exactly at the limit is kept whole.
    const NodeId c = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 50, 50}, std::string(kMaxTextBytes, 'b'));
    CHECK(doc.node(c)->text.size() == kMaxTextBytes);

    // Same rule for setText and edge labels.
    CHECK(doc.setText(a, euros));
    CHECK(doc.node(a)->text.size() == (kMaxTextBytes / 3) * 3);
    const EdgeId e = doc.connect(End{a}, End{b});
    CHECK(doc.setEdgeLabel(e, euros));
    CHECK(doc.edge(e)->label.size() == (kMaxTextBytes / 3) * 3);
}

TEST_CASE("addNode stops at the node cap") {
    FlowDocument doc;
    for (std::size_t i = 0; i < kMaxNodes; ++i) {
        REQUIRE(doc.addNode(NodeKind::Rectangle, Rect{0, 0, 10, 10}) != kNoNode);
    }
    const std::uint64_t rev = doc.revision();
    CHECK(doc.addNode(NodeKind::Rectangle, Rect{0, 0, 10, 10}) == kNoNode);
    CHECK(doc.revision() == rev);
    CHECK(doc.nodes().size() == kMaxNodes);
}

TEST_CASE("connect stops at the edge cap") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    for (std::size_t i = 0; i < kMaxEdges; ++i) {
        REQUIRE(doc.connect(End{a}, End{a}) != kNoEdge);
    }
    const std::uint64_t rev = doc.revision();
    CHECK(doc.connect(End{a}, End{a}) == kNoEdge);
    CHECK(doc.revision() == rev);
    CHECK(doc.edges().size() == kMaxEdges);
}

TEST_CASE("images: add, cap, null and empty") {
    FlowDocument doc;
    CHECK(doc.addImage(nullptr) == kNoImage);
    CHECK(doc.addImage(std::make_shared<const trace::Image>()) == kNoImage); // empty
    CHECK(doc.revision() == 0);

    const ImagePtr img = makeImage();
    const ImageId id = doc.addImage(img);
    CHECK(id != kNoImage);
    CHECK(doc.image(id).get() == img.get()); // shared, not copied
    CHECK(doc.revision() == 1);
    CHECK(doc.addImage(img) != id); // ids are unique even for the same pixels

    FlowDocument full;
    for (std::size_t i = 0; i < kMaxImages; ++i) REQUIRE(full.addImage(makeImage(1, 1)) != kNoImage);
    CHECK(full.addImage(makeImage(1, 1)) == kNoImage);
}

TEST_CASE("setNodeImage keeps only valid ids on Image nodes") {
    FlowDocument doc;
    const NodeId img = doc.addNode(NodeKind::Image, Rect{0, 0, 80, 80});
    const NodeId box = addBox(doc, 0, 0);
    const ImageId id = doc.addImage(makeImage());
    std::uint64_t rev = doc.revision();

    CHECK_FALSE(doc.setNodeImage(img, 999)); // unknown image
    CHECK_FALSE(doc.setNodeImage(box, id));  // not an Image node
    CHECK_FALSE(doc.setNodeImage(12345, id)); // unknown node
    CHECK(doc.revision() == rev);
    CHECK(doc.node(img)->imageId == kNoImage);

    CHECK(doc.setNodeImage(img, id));
    CHECK(doc.node(img)->imageId == id);
    CHECK(doc.revision() == rev + 1);
    rev = doc.revision();
    CHECK_FALSE(doc.setNodeImage(img, id)); // no-op
    CHECK(doc.revision() == rev);

    CHECK(doc.setNodeImage(img, kNoImage)); // clears
    CHECK(doc.node(img)->imageId == kNoImage);
    checkInvariants(doc);
}

TEST_CASE("setKind: leaving Image drops the image id, leaving Container detaches children") {
    FlowDocument doc;
    const NodeId img = doc.addNode(NodeKind::Image, Rect{0, 0, 80, 80});
    const ImageId id = doc.addImage(makeImage());
    REQUIRE(doc.setNodeImage(img, id));
    CHECK(doc.setKind(img, NodeKind::Rectangle));
    CHECK(doc.node(img)->imageId == kNoImage);
    CHECK(doc.image(id).get() != nullptr); // the picture itself stays in the document

    const NodeId c = addContainer(doc, 0, 0);
    const NodeId child = addBox(doc, 10, 10);
    REQUIRE(doc.reparent(child, c));
    std::uint64_t rev = doc.revision();
    CHECK_FALSE(doc.setKind(c, NodeKind::Container)); // same kind
    CHECK(doc.revision() == rev);
    CHECK_FALSE(doc.setKind(c, static_cast<NodeKind>(99)));
    CHECK_FALSE(doc.setKind(4242, NodeKind::Ellipse));
    CHECK(doc.setKind(c, NodeKind::Ellipse));
    CHECK(doc.node(c)->kind == NodeKind::Ellipse);
    CHECK(doc.node(child)->parentId == kNoNode);
    checkInvariants(doc);
}

TEST_CASE("setText, setStyle and setNodeRect report no-ops without bumping the revision") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 50, 50}, "hi");
    std::uint64_t rev = doc.revision();

    CHECK_FALSE(doc.setText(a, "hi"));
    CHECK_FALSE(doc.setText(999, "x"));
    CHECK(doc.revision() == rev);
    CHECK(doc.setText(a, "bye"));
    CHECK(doc.node(a)->text == "bye");
    CHECK(doc.revision() == rev + 1);
    rev = doc.revision();

    Style st = doc.node(a)->style;
    CHECK_FALSE(doc.setStyle(a, st));
    CHECK_FALSE(doc.setStyle(999, st));
    st.fill = trace::Rgba8{9, 9, 9, 255};
    st.dash = Dash::Dashed;
    st.strokeWidth = 5.0f;
    CHECK(doc.setStyle(a, st));
    CHECK(doc.node(a)->style == st);
    CHECK(doc.revision() == rev + 1);
    rev = doc.revision();

    // A style that sanitises to the stored one is also a no-op.
    Style hostile = st;
    hostile.strokeWidth = kNaN;
    Style expect = st;
    expect.strokeWidth = Style{}.strokeWidth;
    CHECK(doc.setStyle(a, hostile));
    CHECK(doc.node(a)->style == expect);
    rev = doc.revision();
    CHECK_FALSE(doc.setStyle(a, hostile));
    CHECK(doc.revision() == rev);

    CHECK_FALSE(doc.setNodeRect(a, Rect{0, 0, 50, 50}));
    CHECK_FALSE(doc.setNodeRect(999, Rect{0, 0, 50, 50}));
    CHECK(doc.revision() == rev);
    CHECK(doc.setNodeRect(a, Rect{5, 6, 70, 80}));
    CHECK(doc.node(a)->rect == Rect{5, 6, 70, 80});
    CHECK(doc.revision() == rev + 1);
}

TEST_CASE("setNodeRect enforces the minimum size and finiteness, and children stay put") {
    FlowDocument doc;
    const NodeId c = addContainer(doc, 0, 0, 300, 300);
    const NodeId child = addBox(doc, 10, 10);
    REQUIRE(doc.reparent(child, c));
    const Rect before = doc.node(child)->rect;

    CHECK(doc.setNodeRect(c, Rect{kNaN, 4, 1, -50}));
    const Rect r = doc.node(c)->rect;
    CHECK(r.x == 0.0f);
    CHECK(r.y == 4.0f);
    CHECK(r.w == kMinNodeSize);
    CHECK(r.h == kMinNodeSize);
    CHECK(doc.node(child)->rect == before);
    checkInvariants(doc);
}

TEST_CASE("moveNodes moves nodes, reports no-ops and sanitises the delta") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 200, 0);
    std::uint64_t rev = doc.revision();

    const NodeId one[] = {a};
    CHECK(doc.moveNodes(one, 10.0f, -5.0f));
    CHECK(doc.node(a)->rect.x == 10.0f);
    CHECK(doc.node(a)->rect.y == -5.0f);
    CHECK(doc.node(b)->rect.x == 200.0f);
    CHECK(doc.revision() == rev + 1);
    rev = doc.revision();

    CHECK_FALSE(doc.moveNodes(one, 0.0f, 0.0f));
    CHECK_FALSE(doc.moveNodes(one, kNaN, 1.0f));
    CHECK_FALSE(doc.moveNodes(one, 1.0f, kNaN));
    const NodeId none[] = {999};
    CHECK_FALSE(doc.moveNodes(none, 5.0f, 5.0f));
    CHECK_FALSE(doc.moveNodes({}, 5.0f, 5.0f));
    CHECK(doc.revision() == rev);

    // Infinity clamps to the board limit rather than producing inf.
    CHECK(doc.moveNodes(one, kInf, -kInf));
    CHECK(doc.node(a)->rect.x == kMaxCoord);
    CHECK(doc.node(a)->rect.y == -kMaxCoord);
    rev = doc.revision();
    // Already pinned against the wall: pushing further changes nothing.
    CHECK_FALSE(doc.moveNodes(one, 100.0f, -100.0f));
    CHECK(doc.revision() == rev);
    checkInvariants(doc);
}

TEST_CASE("moveNodes carries children recursively and group members, each node once") {
    FlowDocument doc;
    const NodeId outer = addContainer(doc, 0, 0, 800, 800);
    const NodeId inner = addContainer(doc, 50, 50, 300, 300);
    const NodeId leaf = addBox(doc, 60, 60);
    const NodeId buddy = addBox(doc, 500, 500);
    const NodeId stranger = addBox(doc, -300, -300);
    REQUIRE(doc.reparent(inner, outer));
    REQUIRE(doc.reparent(leaf, inner));
    const NodeId gm[] = {leaf, buddy};
    REQUIRE(doc.group(gm) != kNoGroup);

    // Moving the outer container and its own child together must not double-move the child.
    const NodeId both[] = {outer, inner, leaf};
    CHECK(doc.moveNodes(both, 10.0f, 20.0f));
    CHECK(doc.node(outer)->rect.x == 10.0f);
    CHECK(doc.node(inner)->rect.x == 60.0f);
    CHECK(doc.node(inner)->rect.y == 70.0f);
    CHECK(doc.node(leaf)->rect.x == 70.0f);
    CHECK(doc.node(leaf)->rect.y == 80.0f);
    CHECK(doc.node(buddy)->rect.x == 510.0f); // group member of leaf
    CHECK(doc.node(buddy)->rect.y == 520.0f);
    CHECK(doc.node(stranger)->rect.x == -300.0f);
    checkInvariants(doc);
}

TEST_CASE("moveNodes on a group member moves the whole group and its children") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addContainer(doc, 200, 0, 300, 300);
    const NodeId kid = addBox(doc, 210, 10);
    REQUIRE(doc.reparent(kid, b));
    const NodeId pair[] = {a, b};
    REQUIRE(doc.group(pair) != kNoGroup);
    const NodeId one[] = {a};
    CHECK(doc.moveNodes(one, 7.0f, 0.0f));
    CHECK(doc.node(a)->rect.x == 7.0f);
    CHECK(doc.node(b)->rect.x == 207.0f);
    CHECK(doc.node(kid)->rect.x == 217.0f);
}

TEST_CASE("removeNodes cascades to edges and counts only real nodes") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 200, 0);
    const NodeId c = addBox(doc, 400, 0);
    const EdgeId ab = doc.connect(End{a}, End{b});
    const EdgeId bc = doc.connect(End{b}, End{c});
    const EdgeId ca = doc.connect(End{c}, End{a});
    const EdgeId loop = doc.connect(End{b}, End{b});
    const EdgeId freeEdge = doc.connect(End{kNoNode, Face::Auto, Pt{1, 2}}, End{c});
    std::uint64_t rev = doc.revision();

    const NodeId nothing[] = {999, 0};
    CHECK(doc.removeNodes(nothing) == 0);
    CHECK(doc.removeNodes({}) == 0);
    CHECK(doc.revision() == rev);

    const NodeId rm[] = {b, b, 999}; // duplicates and unknowns are ignored
    CHECK(doc.removeNodes(rm) == 1);
    CHECK(doc.revision() == rev + 1);
    CHECK(doc.node(b) == nullptr);
    CHECK(doc.node(a) != nullptr);
    CHECK(doc.node(c) != nullptr); // indices rebuilt correctly
    CHECK(doc.edge(ab) == nullptr);
    CHECK(doc.edge(bc) == nullptr);
    CHECK(doc.edge(loop) == nullptr);
    CHECK(doc.edge(ca) != nullptr);
    CHECK(doc.edge(freeEdge) != nullptr);
    CHECK(doc.edges().size() == 2);
    checkInvariants(doc);
}

TEST_CASE("removeNodes detaches children of a removed container and keeps them") {
    FlowDocument doc;
    const NodeId c = addContainer(doc, 0, 0);
    const NodeId k1 = addBox(doc, 10, 10);
    const NodeId k2 = addBox(doc, 20, 20);
    REQUIRE(doc.reparent(k1, c));
    REQUIRE(doc.reparent(k2, c));
    const NodeId rm[] = {c};
    CHECK(doc.removeNodes(rm) == 1);
    CHECK(doc.node(k1)->parentId == kNoNode);
    CHECK(doc.node(k2)->parentId == kNoNode);
    CHECK(doc.node(k1)->rect.x == 10.0f); // not moved
    checkInvariants(doc);
}

TEST_CASE("removeNodes dissolves groups left with fewer than two members") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 0, 0);
    const NodeId c = addBox(doc, 0, 0);
    const NodeId d = addBox(doc, 0, 0);
    const NodeId three[] = {a, b, c};
    const GroupId g = doc.group(three);
    REQUIRE(g != kNoGroup);

    const NodeId rmA[] = {a};
    doc.removeNodes(rmA);
    CHECK(doc.groupMembers(g).size() == 2); // still a group of two
    const NodeId rmB[] = {b};
    doc.removeNodes(rmB);
    CHECK(doc.groupMembers(g).empty()); // dissolved
    CHECK(doc.node(c)->groupId == kNoGroup);
    CHECK(doc.node(d)->groupId == kNoGroup);

    // Removing several at once also repairs.
    const NodeId e = addBox(doc, 0, 0);
    const NodeId pair[] = {c, d, e};
    const GroupId g2 = doc.group(pair);
    const NodeId rm2[] = {c, d};
    CHECK(doc.removeNodes(rm2) == 2);
    CHECK(doc.node(e)->groupId == kNoGroup);
    CHECK(doc.groupMembers(g2).empty());
    checkInvariants(doc);
}

TEST_CASE("connect: docked and free ends, self loops, unknown nodes") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 200, 0);
    std::uint64_t rev = doc.revision();

    const EdgeId e = doc.connect(End{a, Face::Right}, End{b, Face::Left});
    REQUIRE(e != kNoEdge);
    CHECK(doc.revision() == rev + 1);
    const Edge* edge = doc.edge(e);
    REQUIRE(edge != nullptr);
    CHECK(edge->from.node == a);
    CHECK(edge->from.face == Face::Right);
    CHECK(edge->to.face == Face::Left);
    CHECK(edge->route == Route::Curved);
    CHECK(edge->arrowEnd == Arrow::Arrow);
    CHECK(edge->arrowStart == Arrow::None);
    rev = doc.revision();

    CHECK(doc.connect(End{a}, End{a}) != kNoEdge); // self loop
    CHECK(doc.connect(End{kNoNode, Face::Auto, Pt{5, 6}}, End{kNoNode, Face::Auto, Pt{50, 60}}) != kNoEdge);
    rev = doc.revision();

    CHECK(doc.connect(End{999}, End{a}) == kNoEdge);
    CHECK(doc.connect(End{a}, End{999}) == kNoEdge);
    CHECK(doc.revision() == rev);
    CHECK(doc.edges().size() == 3);

    // Free points are sanitised.
    const EdgeId h = doc.connect(End{kNoNode, Face::Top, Pt{kNaN, kInf}}, End{kNoNode, Face::Auto, Pt{-1e20f, 3}});
    const Edge* he = doc.edge(h);
    REQUIRE(he != nullptr);
    CHECK(he->from.point.x == 0.0f);
    CHECK(he->from.point.y == kMaxCoord);
    CHECK(he->to.point.x == -kMaxCoord);
    CHECK(he->to.point.y == 3.0f);
    checkInvariants(doc);
}

TEST_CASE("connect normalises an invalid face") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const EdgeId e = doc.connect(End{a, static_cast<Face>(42)}, End{a});
    CHECK(doc.edge(e)->from.face == Face::Auto);
}

TEST_CASE("reconnect moves one end and refuses bad input") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 200, 0);
    const NodeId c = addBox(doc, 400, 0);
    const EdgeId e = doc.connect(End{a}, End{b});
    std::uint64_t rev = doc.revision();

    CHECK(doc.reconnect(e, false, End{c, Face::Top}));
    CHECK(doc.edge(e)->to.node == c);
    CHECK(doc.edge(e)->to.face == Face::Top);
    CHECK(doc.edge(e)->from.node == a);
    CHECK(doc.revision() == rev + 1);
    rev = doc.revision();

    CHECK_FALSE(doc.reconnect(e, false, End{c, Face::Top})); // same
    CHECK_FALSE(doc.reconnect(e, true, End{999}));           // unknown node
    CHECK_FALSE(doc.reconnect(999, true, End{a}));           // unknown edge
    CHECK(doc.revision() == rev);
    CHECK(doc.edge(e)->to.node == c);

    // Detach to a free point, with sanitising.
    CHECK(doc.reconnect(e, true, End{kNoNode, Face::Auto, Pt{kNaN, 1e9f}}));
    CHECK(doc.edge(e)->from.node == kNoNode);
    CHECK(doc.edge(e)->from.point.x == 0.0f);
    CHECK(doc.edge(e)->from.point.y == kMaxCoord);
    checkInvariants(doc);
}

TEST_CASE("edge properties: route and arrows, label, style") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const EdgeId e = doc.connect(End{a}, End{a});
    std::uint64_t rev = doc.revision();

    CHECK_FALSE(doc.setEdgeRoute(e, Route::Curved, Arrow::None, Arrow::Arrow)); // already the default
    CHECK_FALSE(doc.setEdgeRoute(999, Route::Elbow, Arrow::None, Arrow::None));
    CHECK_FALSE(doc.setEdgeRoute(e, static_cast<Route>(9), Arrow::None, Arrow::None));
    CHECK_FALSE(doc.setEdgeRoute(e, Route::Elbow, static_cast<Arrow>(9), Arrow::None));
    CHECK_FALSE(doc.setEdgeRoute(e, Route::Elbow, Arrow::None, static_cast<Arrow>(9)));
    CHECK(doc.revision() == rev);
    CHECK(doc.setEdgeRoute(e, Route::Elbow, Arrow::Dot, Arrow::Open));
    CHECK(doc.edge(e)->route == Route::Elbow);
    CHECK(doc.edge(e)->arrowStart == Arrow::Dot);
    CHECK(doc.edge(e)->arrowEnd == Arrow::Open);
    CHECK(doc.revision() == rev + 1);
    rev = doc.revision();

    CHECK_FALSE(doc.setEdgeLabel(e, ""));
    CHECK_FALSE(doc.setEdgeLabel(999, "x"));
    CHECK(doc.setEdgeLabel(e, "yes"));
    CHECK(doc.edge(e)->label == "yes");
    CHECK_FALSE(doc.setEdgeLabel(e, "yes"));
    CHECK(doc.revision() == rev + 1);
    rev = doc.revision();

    Style st;
    st.stroke = trace::Rgba8{255, 0, 0, 255};
    st.strokeWidth = 99.0f;
    CHECK_FALSE(doc.setEdgeStyle(999, st));
    CHECK(doc.setEdgeStyle(e, st));
    CHECK(doc.edge(e)->style.strokeWidth == 32.0f);
    CHECK(doc.edge(e)->style.stroke == st.stroke);
    CHECK_FALSE(doc.setEdgeStyle(e, st)); // sanitises to the stored value
    CHECK(doc.revision() == rev + 1);
}

TEST_CASE("removeEdges") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const EdgeId e1 = doc.connect(End{a}, End{a});
    const EdgeId e2 = doc.connect(End{a}, End{a});
    const EdgeId e3 = doc.connect(End{a}, End{a});
    std::uint64_t rev = doc.revision();

    const EdgeId none[] = {999, 0};
    CHECK(doc.removeEdges(none) == 0);
    CHECK(doc.removeEdges({}) == 0);
    CHECK(doc.revision() == rev);

    const EdgeId rm[] = {e1, e3, e1, 999};
    CHECK(doc.removeEdges(rm) == 2);
    CHECK(doc.revision() == rev + 1);
    CHECK(doc.edges().size() == 1);
    CHECK(doc.edge(e2) != nullptr);
    CHECK(doc.edge(e1) == nullptr);
    CHECK(doc.edge(e3) == nullptr);
    CHECK(doc.node(a) != nullptr);
}

TEST_CASE("edge lookup works after removals in the middle") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    std::vector<EdgeId> ids;
    for (int i = 0; i < 10; ++i) ids.push_back(doc.connect(End{a}, End{a}));
    const EdgeId rm[] = {ids[2], ids[5], ids[6]};
    doc.removeEdges(rm);
    for (int i = 0; i < 10; ++i) {
        const bool gone = i == 2 || i == 5 || i == 6;
        CHECK((doc.edge(ids[static_cast<std::size_t>(i)]) == nullptr) == gone);
    }
}

TEST_CASE("z-order: ids get distinct increasing z and nodesByZ is back to front") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 0, 0);
    const NodeId c = addBox(doc, 0, 0);
    const std::vector<NodeId> expect{a, b, c};
    CHECK(doc.nodesByZ() == expect);
}

TEST_CASE("bringToFront and sendToBack move a block and keep its relative order") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 0, 0);
    const NodeId c = addBox(doc, 0, 0);
    const NodeId d = addBox(doc, 0, 0);
    std::uint64_t rev = doc.revision();

    const NodeId ab[] = {b, a}; // order given does not matter, z order is kept
    CHECK(doc.bringToFront(ab));
    CHECK(doc.nodesByZ() == std::vector<NodeId>{c, d, a, b});
    CHECK(doc.revision() == rev + 1);
    rev = doc.revision();

    CHECK_FALSE(doc.bringToFront(ab)); // already on top
    CHECK_FALSE(doc.bringToFront({}));
    const NodeId unknown[] = {999};
    CHECK_FALSE(doc.bringToFront(unknown));
    CHECK(doc.revision() == rev);

    const NodeId cd[] = {c, d};
    CHECK_FALSE(doc.sendToBack(cd)); // already at the back
    CHECK(doc.nodesByZ() == std::vector<NodeId>{c, d, a, b});
}

TEST_CASE("sendToBack and bringToFront detect no-ops and reorder otherwise") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 0, 0);
    const NodeId c = addBox(doc, 0, 0);
    std::uint64_t rev = doc.revision();

    const NodeId one[] = {a};
    CHECK_FALSE(doc.sendToBack(one));
    CHECK(doc.revision() == rev);

    const NodeId cc[] = {c};
    CHECK(doc.sendToBack(cc));
    CHECK(doc.nodesByZ() == std::vector<NodeId>{c, a, b});
    CHECK(doc.revision() == rev + 1);
    const NodeId aa[] = {a, c};
    CHECK(doc.bringToFront(aa));
    CHECK(doc.nodesByZ() == std::vector<NodeId>{b, c, a});

    // z stays unique and a new node lands on top.
    const NodeId d = addBox(doc, 0, 0);
    CHECK(doc.nodesByZ().back() == d);
    checkInvariants(doc);
}

TEST_CASE("bringForward steps past the next overlapping node only") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0, 100, 100);      // z1
    const NodeId far = addBox(doc, 1000, 1000, 50, 50); // z2, does not overlap a
    const NodeId b = addBox(doc, 50, 50, 100, 100);    // z3, overlaps a
    const NodeId c = addBox(doc, 60, 60, 100, 100);    // z4, overlaps a
    const std::uint64_t rev = doc.revision();

    const NodeId one[] = {a};
    CHECK(doc.bringForward(one));
    CHECK(doc.nodesByZ() == std::vector<NodeId>{far, b, a, c}); // jumped over far and b, stopped under c
    CHECK(doc.revision() == rev + 1);

    CHECK(doc.bringForward(one));
    CHECK(doc.nodesByZ() == std::vector<NodeId>{far, b, c, a});
    const std::uint64_t rev2 = doc.revision();
    CHECK_FALSE(doc.bringForward(one)); // at the top already
    CHECK(doc.revision() == rev2);

    // Nothing overlapping above: no change.
    const NodeId f[] = {far};
    CHECK_FALSE(doc.bringForward(f));
}

TEST_CASE("sendBackward steps past the next overlapping node below") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0, 100, 100);      // z1
    const NodeId b = addBox(doc, 50, 50, 100, 100);    // z2 overlaps a
    const NodeId far = addBox(doc, 1000, 1000, 50, 50); // z3
    const NodeId c = addBox(doc, 60, 60, 100, 100);    // z4 overlaps a and b

    const NodeId one[] = {c};
    CHECK(doc.sendBackward(one));
    CHECK(doc.nodesByZ() == std::vector<NodeId>{a, c, b, far}); // hopped far (no overlap) and b
    CHECK(doc.sendBackward(one));
    CHECK(doc.nodesByZ() == std::vector<NodeId>{c, a, b, far});
    CHECK_FALSE(doc.sendBackward(one));
}

TEST_CASE("bringForward and sendBackward keep a block's relative order and never cross each other") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0, 100, 100);
    const NodeId b = addBox(doc, 10, 10, 100, 100);
    const NodeId c = addBox(doc, 20, 20, 100, 100);
    const NodeId d = addBox(doc, 30, 30, 100, 100);

    const NodeId ab[] = {a, b};
    CHECK(doc.bringForward(ab));
    CHECK(doc.nodesByZ() == std::vector<NodeId>{c, a, b, d}); // b moved past c, then a moved past c: a stays under b
    CHECK(doc.bringForward(ab));
    CHECK(doc.nodesByZ() == std::vector<NodeId>{c, d, a, b});
    CHECK_FALSE(doc.bringForward(ab));

    const NodeId ba[] = {a, b};
    CHECK(doc.sendBackward(ba));
    CHECK(doc.nodesByZ() == std::vector<NodeId>{c, a, b, d});
    checkInvariants(doc);
}

TEST_CASE("group and ungroup") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 0, 0);
    const NodeId c = addBox(doc, 0, 0);
    const std::uint64_t rev = doc.revision();

    const NodeId one[] = {a};
    const NodeId same[] = {a, a};
    const NodeId unknown[] = {a, 999};
    CHECK(doc.group(one) == kNoGroup);
    CHECK(doc.group(same) == kNoGroup); // needs two distinct nodes
    CHECK(doc.group(unknown) == kNoGroup);
    CHECK(doc.group({}) == kNoGroup);
    CHECK(doc.revision() == rev);

    const NodeId ab[] = {a, b};
    const GroupId g = doc.group(ab);
    REQUIRE(g != kNoGroup);
    CHECK(doc.revision() == rev + 1);
    CHECK(doc.node(a)->groupId == g);
    CHECK(doc.node(b)->groupId == g);
    CHECK(doc.node(c)->groupId == kNoGroup);
    CHECK(doc.groupMembers(g) == std::vector<NodeId>{a, b});
    CHECK(doc.groupMembers(kNoGroup).empty());

    // Grouping the same set again changes nothing and returns the existing group.
    CHECK(doc.group(ab) == g);
    CHECK(doc.revision() == rev + 1);

    CHECK_FALSE(doc.ungroup(kNoGroup));
    CHECK_FALSE(doc.ungroup(999));
    CHECK(doc.revision() == rev + 1);
    CHECK(doc.ungroup(g));
    CHECK(doc.node(a)->groupId == kNoGroup);
    CHECK(doc.node(b)->groupId == kNoGroup);
    CHECK(doc.revision() == rev + 2);
    CHECK_FALSE(doc.ungroup(g));
}

TEST_CASE("grouping pulls members out of their old groups and dissolves leftovers") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 0, 0);
    const NodeId c = addBox(doc, 0, 0);
    const NodeId d = addBox(doc, 0, 0);
    const NodeId ab[] = {a, b};
    const GroupId g1 = doc.group(ab);
    const NodeId cd[] = {c, d};
    const GroupId g2 = doc.group(cd);
    REQUIRE(g1 != g2);

    // b joins c: g1 is left with only a (dissolves), g2 is left with only d (dissolves).
    const NodeId bc[] = {b, c};
    const GroupId g3 = doc.group(bc);
    REQUIRE(g3 != kNoGroup);
    CHECK(g3 != g1);
    CHECK(g3 != g2);
    CHECK(doc.node(a)->groupId == kNoGroup);
    CHECK(doc.node(d)->groupId == kNoGroup);
    CHECK(doc.groupMembers(g3) == std::vector<NodeId>{b, c});

    // Pulling one node out of a bigger group leaves that group alive.
    const NodeId e = addBox(doc, 0, 0);
    const NodeId f = addBox(doc, 0, 0);
    const NodeId big[] = {a, d, e};
    const GroupId g4 = doc.group(big);
    const NodeId ef[] = {e, f};
    const GroupId g5 = doc.group(ef);
    CHECK(doc.groupMembers(g4) == std::vector<NodeId>{a, d});
    CHECK(doc.groupMembers(g5) == std::vector<NodeId>{e, f});
    checkInvariants(doc);
}

TEST_CASE("reparent into a container and out again") {
    FlowDocument doc;
    const NodeId c = addContainer(doc, 0, 0);
    const NodeId a = addBox(doc, 10, 10);
    const std::uint64_t rev = doc.revision();

    CHECK(doc.reparent(a, c));
    CHECK(doc.node(a)->parentId == c);
    CHECK(doc.childrenOf(c) == std::vector<NodeId>{a});
    CHECK(doc.revision() == rev + 1);
    CHECK_FALSE(doc.reparent(a, c)); // same parent

    const NodeId c2 = addContainer(doc, 500, 0);
    CHECK(doc.reparent(a, c2)); // directly to another container
    CHECK(doc.childrenOf(c).empty());
    CHECK(doc.childrenOf(c2) == std::vector<NodeId>{a});

    CHECK(doc.reparent(a, kNoNode));
    CHECK(doc.node(a)->parentId == kNoNode);
    CHECK_FALSE(doc.reparent(a, kNoNode)); // already free
    CHECK(doc.childrenOf(kNoNode).empty());
}

TEST_CASE("reparent refuses unknown ids, non-containers, self and cycles") {
    FlowDocument doc;
    const NodeId a = addContainer(doc, 0, 0);
    const NodeId b = addContainer(doc, 10, 10, 100, 100);
    const NodeId c = addContainer(doc, 20, 20, 50, 50);
    const NodeId box = addBox(doc, 0, 0);
    REQUIRE(doc.reparent(b, a));
    REQUIRE(doc.reparent(c, b));
    const std::uint64_t rev = doc.revision();

    CHECK_FALSE(doc.reparent(999, a));
    CHECK_FALSE(doc.reparent(a, 999));
    CHECK_FALSE(doc.reparent(a, box)); // not a container
    CHECK_FALSE(doc.reparent(a, a));   // self
    CHECK_FALSE(doc.reparent(a, b));   // a is b's parent
    CHECK_FALSE(doc.reparent(a, c));   // a is c's grandparent
    CHECK_FALSE(doc.reparent(b, c));
    CHECK(doc.revision() == rev);
    CHECK(doc.node(a)->parentId == kNoNode);
    CHECK(doc.node(b)->parentId == a);
    CHECK(doc.node(c)->parentId == b);

    // Moving a branch elsewhere is fine.
    const NodeId other = addContainer(doc, 900, 0);
    CHECK(doc.reparent(b, other));
    CHECK(doc.reparent(a, c)); // no longer a cycle: c is under other, not under a
    checkInvariants(doc);
}

TEST_CASE("boundsOf unions the valid nodes") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0, 100, 50);
    const NodeId b = addBox(doc, 200, -40, 20, 30);
    const NodeId ab[] = {a, b};
    const Rect r = doc.boundsOf(ab);
    CHECK(r == Rect{0, -40, 220, 90});

    const NodeId mixed[] = {999, b};
    CHECK(doc.boundsOf(mixed) == doc.node(b)->rect);
    const NodeId bad[] = {999, 0};
    CHECK(doc.boundsOf(bad) == Rect{});
    CHECK(doc.boundsOf({}) == Rect{});
}

TEST_CASE("approxBytes grows with content") {
    FlowDocument doc;
    const std::size_t empty = doc.approxBytes();
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 50, 50}, std::string(5000, 'x'));
    const std::size_t one = doc.approxBytes();
    CHECK(one > empty + 5000);
    for (int i = 0; i < 100; ++i) addBox(doc, 0, 0);
    CHECK(doc.approxBytes() > one);
    const EdgeId e = doc.connect(End{a}, End{a});
    doc.setEdgeLabel(e, std::string(3000, 'y'));
    CHECK(doc.approxBytes() > one + 3000);
}

TEST_CASE("polygon sides are clamped, stored and only editable on polygons") {
    FlowDocument doc;
    const NodeId poly = doc.addNode(NodeKind::Polygon, Rect{0, 0, 100, 100}, {}, {}, 6);
    const NodeId low = doc.addNode(NodeKind::Polygon, Rect{0, 0, 100, 100}, {}, {}, -4);
    const NodeId high = doc.addNode(NodeKind::Polygon, Rect{0, 0, 100, 100}, {}, {}, 9999);
    const NodeId box = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 100});
    CHECK(doc.node(poly)->sides == 6);
    CHECK(doc.node(low)->sides == kMinPolygonSides);
    CHECK(doc.node(high)->sides == kMaxPolygonSides);
    CHECK(doc.node(box)->sides == kDefaultPolygonSides);

    const std::uint64_t rev = doc.revision();
    CHECK_FALSE(doc.setSides(poly, 6)); // no change
    CHECK(doc.revision() == rev);
    CHECK(doc.setSides(poly, 9));
    CHECK(doc.node(poly)->sides == 9);
    CHECK(doc.setSides(poly, 1));
    CHECK(doc.node(poly)->sides == kMinPolygonSides);
    CHECK(doc.setSides(poly, 500));
    CHECK(doc.node(poly)->sides == kMaxPolygonSides);
    CHECK_FALSE(doc.setSides(box, 7)); // not a polygon
    CHECK_FALSE(doc.setSides(kNoNode, 7));
    CHECK_FALSE(doc.setSides(9999, 7));
}

TEST_CASE("edge label placement is stored, clamped and counts as a change only when it differs") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 50, 50});
    const NodeId b = doc.addNode(NodeKind::Rectangle, Rect{200, 0, 50, 50});
    const EdgeId e = doc.connect(End{a, Face::Right, {}}, End{b, Face::Left, {}});
    REQUIRE(e != kNoEdge);
    CHECK(doc.edge(e)->labelPos == 0.5f);
    CHECK(doc.edge(e)->labelSide == LabelSide::On);
    CHECK_FALSE(doc.edge(e)->labelAlong);

    const std::uint64_t rev = doc.revision();
    CHECK_FALSE(doc.setEdgeLabelPlacement(e, 0.5f, LabelSide::On, false));
    CHECK(doc.revision() == rev);
    CHECK(doc.setEdgeLabelPlacement(e, 0.25f, LabelSide::Above, true));
    CHECK(doc.edge(e)->labelPos == 0.25f);
    CHECK(doc.edge(e)->labelSide == LabelSide::Above);
    CHECK(doc.edge(e)->labelAlong);
    CHECK(doc.setEdgeLabelPlacement(e, 7.0f, LabelSide::Below, true));
    CHECK(doc.edge(e)->labelPos == 1.0f);
    CHECK(doc.setEdgeLabelPlacement(e, -7.0f, LabelSide::Below, true));
    CHECK(doc.edge(e)->labelPos == 0.0f);
    CHECK(doc.setEdgeLabelPlacement(e, std::nanf(""), LabelSide::Below, true));
    CHECK(doc.edge(e)->labelPos == 0.5f);
    CHECK_FALSE(doc.setEdgeLabelPlacement(e, 0.5f, static_cast<LabelSide>(9), true));
    CHECK_FALSE(doc.setEdgeLabelPlacement(kNoEdge, 0.5f, LabelSide::On, false));
    CHECK_FALSE(doc.setEdgeLabelPlacement(999, 0.5f, LabelSide::On, false));
}

TEST_CASE("a copy is independent and shares image pixels") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const ImageId id = doc.addImage(makeImage());
    FlowDocument copy = doc;
    CHECK(copy.setText(a, "changed"));
    CHECK(doc.node(a)->text.empty());
    CHECK(copy.image(id).get() == doc.image(id).get());
    // The copy keeps its own ids going where the original left off.
    CHECK(copy.addNode(NodeKind::Rectangle, Rect{0, 0, 10, 10}) == doc.addNode(NodeKind::Rectangle, Rect{0, 0, 10, 10}));
}

TEST_CASE("revision only moves forward and only on real change") {
    FlowDocument doc;
    CHECK(doc.revision() == 0);
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 0, 0);
    const std::uint64_t r0 = doc.revision();

    // A battery of no-ops.
    const NodeId ab[] = {a, b};
    doc.moveNodes(ab, 0, 0);
    doc.setText(a, "");
    doc.setStyle(a, Style{});
    doc.setNodeRect(a, doc.node(a)->rect);
    doc.setKind(a, NodeKind::Rectangle);
    doc.removeNodes({});
    doc.removeEdges({});
    doc.bringToFront(ab); // a, b already in z order with b on top and both picked
    doc.ungroup(5);
    doc.reparent(a, kNoNode);
    CHECK(doc.revision() == r0);

    // Each real change bumps exactly once.
    std::uint64_t prev = doc.revision();
    const auto bumped = [&] {
        const bool up = doc.revision() == prev + 1;
        prev = doc.revision();
        return up;
    };
    doc.moveNodes(ab, 1, 1);
    CHECK(bumped());
    doc.setText(a, "x");
    CHECK(bumped());
    const EdgeId e = doc.connect(End{a}, End{b});
    CHECK(bumped());
    doc.setEdgeLabel(e, "l");
    CHECK(bumped());
    doc.group(ab);
    CHECK(bumped());
    const NodeId rm[] = {a};
    doc.removeNodes(rm); // cascades edge and group in a single bump
    CHECK(bumped());
}

TEST_CASE("operations on a hostile sequence keep every invariant") {
    FlowDocument doc;
    std::vector<NodeId> ids;
    for (int i = 0; i < 40; ++i) {
        const NodeKind k = (i % 5 == 0) ? NodeKind::Container : NodeKind::Rectangle;
        ids.push_back(doc.addNode(k, Rect{kNaN, static_cast<float>(i) * 1e6f, kInf, -1.0f}, std::string(20000, 'q')));
    }
    for (std::size_t i = 1; i < ids.size(); ++i) {
        doc.reparent(ids[i], ids[i - 1]);
        doc.reparent(ids[i - 1], ids[i]); // cycle attempts
        doc.connect(End{ids[i], static_cast<Face>(i + 50)}, End{ids[(i * 7) % ids.size()]});
    }
    doc.group(std::span<const NodeId>(ids.data(), 10));
    doc.moveNodes(ids, kInf, 123456789.0f);
    doc.moveNodes(ids, -kInf, -kInf);
    doc.bringForward(std::span<const NodeId>(ids.data(), 5));
    doc.sendBackward(std::span<const NodeId>(ids.data() + 10, 5));
    doc.removeNodes(std::span<const NodeId>(ids.data() + 3, 11));
    checkInvariants(doc);
}

// --- manual routes -------------------------------------------------------------------------------------------

TEST_CASE("setEdgeVia stores a clamped offset, bumps the revision only on a change and can reset") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 300, 0);
    const EdgeId e = doc.connect(End{a, Face::Auto, {}}, End{b, Face::Auto, {}});
    CHECK_FALSE(doc.edge(e)->via.has_value());

    const std::uint64_t rev = doc.revision();
    CHECK(doc.setEdgeVia(e, Pt{10, -20}));
    CHECK(doc.revision() > rev);
    CHECK(doc.edge(e)->via == std::optional<Pt>(Pt{10, -20}));
    const std::uint64_t rev2 = doc.revision();
    CHECK_FALSE(doc.setEdgeVia(e, Pt{10, -20})); // no change
    CHECK(doc.revision() == rev2);

    CHECK(doc.setEdgeVia(e, Pt{kNaN, kInf}));
    CHECK(std::isfinite(doc.edge(e)->via->x));
    CHECK(std::isfinite(doc.edge(e)->via->y));
    CHECK(doc.edge(e)->via->y <= kMaxCoord);

    CHECK(doc.setEdgeVia(e, std::nullopt));
    CHECK_FALSE(doc.edge(e)->via.has_value());
    CHECK_FALSE(doc.setEdgeVia(e, std::nullopt));
    CHECK_FALSE(doc.setEdgeVia(kNoEdge, Pt{1, 1}));
    CHECK_FALSE(doc.setEdgeVia(9999, Pt{1, 1}));
}

TEST_CASE("A manual route is dropped when the route type changes or an end moves, not for arrowheads") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 300, 0);
    const NodeId c = addBox(doc, 300, 200);
    const EdgeId e = doc.connect(End{a, Face::Auto, {}}, End{b, Face::Auto, {}});
    doc.setEdgeRoute(e, Route::Elbow, Arrow::None, Arrow::Arrow);
    REQUIRE(doc.setEdgeVia(e, Pt{15, 0}));

    CHECK(doc.setEdgeRoute(e, Route::Elbow, Arrow::Dot, Arrow::Arrow)); // same route, other arrowhead
    CHECK(doc.edge(e)->via.has_value());

    CHECK(doc.setEdgeRoute(e, Route::Straight, Arrow::Dot, Arrow::Arrow));
    CHECK_FALSE(doc.edge(e)->via.has_value());

    REQUIRE(doc.setEdgeVia(e, Pt{15, 0}));
    CHECK(doc.reconnect(e, false, End{c, Face::Auto, {}}));
    CHECK_FALSE(doc.edge(e)->via.has_value());

    REQUIRE(doc.setEdgeVia(e, Pt{15, 0}));
    CHECK_FALSE(doc.reconnect(e, false, End{c, Face::Auto, {}})); // same end: nothing changes
    CHECK(doc.edge(e)->via.has_value());
}

TEST_CASE("A manual route moves with its shapes and goes with its connector") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 300, 0);
    const EdgeId e = doc.connect(End{a, Face::Auto, {}}, End{b, Face::Auto, {}});
    REQUIRE(doc.setEdgeVia(e, Pt{5, 6}));
    const NodeId ids[] = {a};
    REQUIRE(doc.moveNodes(ids, 40, 40));
    CHECK(doc.edge(e)->via == std::optional<Pt>(Pt{5, 6})); // relative to the route, so it moves with it
    REQUIRE(doc.removeNodes(ids) == 1);
    CHECK(doc.edges().empty());
}

TEST_CASE("setStyle keeps and clamps the vertical text alignment") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    Style st = doc.node(a)->style;
    CHECK(st.valign == VAlign::Middle);
    st.valign = VAlign::Bottom;
    CHECK(doc.setStyle(a, st));
    CHECK(doc.node(a)->style.valign == VAlign::Bottom);
    st.valign = static_cast<VAlign>(77);
    CHECK(doc.setStyle(a, st));
    CHECK(doc.node(a)->style.valign == VAlign::Middle); // an invalid value falls back to the default
    st = doc.node(a)->style;
    st.align = TextAlign::Right;
    CHECK(doc.setStyle(a, st));
    CHECK(doc.node(a)->style.align == TextAlign::Right);
    CHECK_FALSE(doc.setStyle(a, st));
}
