#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "flow/flow_document.hpp"
#include "flow/flow_layout.hpp"

using namespace trace::flow;

namespace {

NodeId addBox(FlowDocument& doc, float x, float y, float w = 100, float h = 50, NodeKind kind = NodeKind::Rectangle) {
    return doc.addNode(kind, Rect{x, y, w, h});
}

Rect rectOf(const FlowDocument& doc, NodeId id) { return doc.node(id)->rect; }

// Three boxes of different sizes at different places.
struct Trio {
    FlowDocument doc;
    NodeId a, b, c;
    std::vector<NodeId> ids;
    Trio() {
        a = addBox(doc, 10, 100, 40, 20);   // right 50, bottom 120
        b = addBox(doc, 200, 20, 100, 60);  // right 300, bottom 80
        c = addBox(doc, 120, 300, 60, 100); // right 180, bottom 400
        ids = {a, b, c};
    }
};

} // namespace

TEST_CASE("alignNodes aligns to the bounds of the selection in every mode") {
    SUBCASE("left") {
        Trio t;
        CHECK(alignNodes(t.doc, t.ids, AlignMode::Left));
        for (const NodeId id : t.ids) CHECK(rectOf(t.doc, id).x == 10);
        CHECK(rectOf(t.doc, t.b).y == 20); // the other axis is untouched
    }
    SUBCASE("right") {
        Trio t;
        CHECK(alignNodes(t.doc, t.ids, AlignMode::Right));
        for (const NodeId id : t.ids) CHECK(rectOf(t.doc, id).right() == 300);
    }
    SUBCASE("center") {
        Trio t;
        CHECK(alignNodes(t.doc, t.ids, AlignMode::Center));
        for (const NodeId id : t.ids) CHECK(rectOf(t.doc, id).center().x == doctest::Approx(155.0f));
    }
    SUBCASE("top") {
        Trio t;
        CHECK(alignNodes(t.doc, t.ids, AlignMode::Top));
        for (const NodeId id : t.ids) CHECK(rectOf(t.doc, id).y == 20);
        CHECK(rectOf(t.doc, t.c).x == 120);
    }
    SUBCASE("bottom") {
        Trio t;
        CHECK(alignNodes(t.doc, t.ids, AlignMode::Bottom));
        for (const NodeId id : t.ids) CHECK(rectOf(t.doc, id).bottom() == 400);
    }
    SUBCASE("middle") {
        Trio t;
        CHECK(alignNodes(t.doc, t.ids, AlignMode::Middle));
        for (const NodeId id : t.ids) CHECK(rectOf(t.doc, id).center().y == doctest::Approx(210.0f));
    }
    SUBCASE("sizes never change") {
        Trio t;
        alignNodes(t.doc, t.ids, AlignMode::Center);
        CHECK(rectOf(t.doc, t.a).w == 40);
        CHECK(rectOf(t.doc, t.b).h == 60);
    }
}

TEST_CASE("alignNodes is a no-op when nothing would move or the selection is too small") {
    Trio t;
    REQUIRE(alignNodes(t.doc, t.ids, AlignMode::Left));
    const auto rev = t.doc.revision();
    CHECK_FALSE(alignNodes(t.doc, t.ids, AlignMode::Left)); // already aligned
    CHECK(t.doc.revision() == rev);

    CHECK_FALSE(alignNodes(t.doc, std::vector<NodeId>{t.a}, AlignMode::Top));
    CHECK_FALSE(alignNodes(t.doc, std::vector<NodeId>{}, AlignMode::Top));
    CHECK_FALSE(alignNodes(t.doc, std::vector<NodeId>{t.a, 9999}, AlignMode::Top)); // one real node only
    CHECK(t.doc.revision() == rev);
}

TEST_CASE("alignNodes aligns repeatedly without drifting at large coordinates") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 900000.0f, 10, 33.3f, 20);
    const NodeId b = addBox(doc, 123456.7f, 40, 71.1f, 20);
    const std::vector<NodeId> ids{a, b};
    REQUIRE(alignNodes(doc, ids, AlignMode::Center));
    const auto rev = doc.revision();
    CHECK_FALSE(alignNodes(doc, ids, AlignMode::Center));
    CHECK(doc.revision() == rev);
}

TEST_CASE("alignNodes ignores unknown ids and duplicates") {
    Trio t;
    const std::vector<NodeId> ids{t.a, 9999, t.b, t.a, t.b, kNoNode};
    CHECK(alignNodes(t.doc, ids, AlignMode::Top));
    CHECK(rectOf(t.doc, t.a).y == 20);
    CHECK(rectOf(t.doc, t.b).y == 20);
    CHECK(rectOf(t.doc, t.c).y == 300); // not listed
}

TEST_CASE("alignNodes treats a group as one block") {
    FlowDocument doc;
    const NodeId g1 = addBox(doc, 0, 0, 50, 50);
    const NodeId g2 = addBox(doc, 100, 20, 50, 50); // group bounds x 0..150, y 0..70
    const NodeId free = addBox(doc, 400, 300, 40, 40);
    REQUIRE(doc.group(std::vector<NodeId>{g1, g2}) != kNoGroup);

    SUBCASE("aligning the left edges moves the whole group by one delta") {
        const std::vector<NodeId> ids{g1, g2, free};
        CHECK(alignNodes(doc, ids, AlignMode::Left));
        CHECK(rectOf(doc, g1).x == 0);
        CHECK(rectOf(doc, g2).x == 100); // the group stays: its left edge is the leftmost
        CHECK(rectOf(doc, free).x == 0);
    }
    SUBCASE("members keep their relative layout") {
        const std::vector<NodeId> ids{g1, free};
        CHECK(alignNodes(doc, ids, AlignMode::Right)); // right of the group is 150, of free 440
        CHECK(rectOf(doc, free).right() == 440);
        CHECK(rectOf(doc, g2).x - rectOf(doc, g1).x == 100);
        CHECK(rectOf(doc, g2).right() == 440);
    }
    SUBCASE("a single selected member brings the whole group, but the group is still one unit") {
        const std::vector<NodeId> ids{g1, g2};
        CHECK_FALSE(alignNodes(doc, ids, AlignMode::Left)); // one unit: too few
    }
}

TEST_CASE("alignNodes does not double-move a selected container and its child") {
    FlowDocument doc;
    const NodeId box = doc.addNode(NodeKind::Container, Rect{100, 100, 300, 200});
    const NodeId child = addBox(doc, 150, 150, 50, 50);
    const NodeId other = addBox(doc, 0, 0, 40, 40);
    REQUIRE(doc.reparent(child, box));

    const std::vector<NodeId> ids{other, box, child};
    CHECK(alignNodes(doc, ids, AlignMode::Left)); // two units: other and the container
    CHECK(rectOf(doc, other).x == 0);
    CHECK(rectOf(doc, box).x == 0);
    CHECK(rectOf(doc, child).x == 50); // moved once, by the container's delta of -100
    CHECK(rectOf(doc, child).y == 150);
}

TEST_CASE("alignNodes: a child alone is its own unit and a nested chain is one unit") {
    FlowDocument doc;
    const NodeId outer = doc.addNode(NodeKind::Container, Rect{0, 0, 500, 500});
    const NodeId inner = doc.addNode(NodeKind::Container, Rect{50, 50, 300, 300});
    const NodeId leaf = addBox(doc, 80, 80, 40, 40);
    const NodeId other = addBox(doc, 700, 0, 40, 40);
    REQUIRE(doc.reparent(inner, outer));
    REQUIRE(doc.reparent(leaf, inner));

    SUBCASE("grandchild follows the selected outer container") {
        const std::vector<NodeId> ids{leaf, outer, other};
        CHECK(alignNodes(doc, ids, AlignMode::Right)); // right edges: outer 500, other 740
        CHECK(rectOf(doc, outer).right() == 740);
        CHECK(rectOf(doc, leaf).x == 80 + 240);
        CHECK(rectOf(doc, inner).x == 50 + 240);
    }
    SUBCASE("a lone child aligns like any node") {
        const std::vector<NodeId> ids{leaf, other};
        CHECK(alignNodes(doc, ids, AlignMode::Top));
        CHECK(rectOf(doc, leaf).y == 0);
        CHECK(rectOf(doc, other).y == 0);
    }
}

TEST_CASE("alignNodes merges units whose move closures overlap") {
    FlowDocument doc;
    const NodeId box = doc.addNode(NodeKind::Container, Rect{100, 100, 200, 200});
    const NodeId child = addBox(doc, 120, 150, 40, 40);
    const NodeId mate = addBox(doc, 600, 600, 40, 40); // grouped with the child, outside the container
    const NodeId far = addBox(doc, 0, 0, 40, 40);
    REQUIRE(doc.reparent(child, box));
    REQUIRE(doc.group(std::vector<NodeId>{child, mate}) != kNoGroup);

    // box (with child, which drags mate) and mate (a group member of child) are one block: moving it twice would
    // shift child and mate by two deltas.
    const std::vector<NodeId> ids{box, mate, far};
    CHECK(alignNodes(doc, ids, AlignMode::Left));
    CHECK(rectOf(doc, far).x == 0);
    CHECK(rectOf(doc, box).x == 0);
    CHECK(rectOf(doc, child).x == 20);   // moved by -100 once
    CHECK(rectOf(doc, mate).x == 500);   // moved by -100 once
}

TEST_CASE("distributeNodes spaces unequal sizes with equal gaps and keeps the ends") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0, 100, 40);    // 0..100
    const NodeId b = addBox(doc, 120, 0, 50, 40);   // 120..170
    const NodeId c = addBox(doc, 190, 0, 150, 40);  // 190..340
    const NodeId d = addBox(doc, 500, 0, 100, 40);  // 500..600
    // total sizes 400, span 600, gap = 200/3
    const std::vector<NodeId> ids{a, b, c, d};
    CHECK(distributeNodes(doc, ids, DistributeAxis::Horizontal));
    const float gap = 200.0f / 3.0f;
    CHECK(rectOf(doc, a).x == 0);
    CHECK(rectOf(doc, d).x == 500);
    CHECK(rectOf(doc, b).x - rectOf(doc, a).right() == doctest::Approx(gap));
    CHECK(rectOf(doc, c).x - rectOf(doc, b).right() == doctest::Approx(gap));
    CHECK(rectOf(doc, d).x - rectOf(doc, c).right() == doctest::Approx(gap));
}

TEST_CASE("distributeNodes sorts by position, not by selection order") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0, 20, 20);
    const NodeId b = addBox(doc, 50, 0, 20, 20);
    const NodeId c = addBox(doc, 180, 0, 20, 20);
    const std::vector<NodeId> ids{c, a, b};
    CHECK(distributeNodes(doc, ids, DistributeAxis::Horizontal));
    CHECK(rectOf(doc, a).x == 0);
    CHECK(rectOf(doc, c).x == 180);
    CHECK(rectOf(doc, b).x == doctest::Approx(90.0f)); // gap (200 - 60) / 2 = 70, so b sits at 20 + 70
}

TEST_CASE("distributeNodes works vertically and leaves the other axis alone") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 7, 0, 30, 10);
    const NodeId b = addBox(doc, 9, 15, 30, 30);
    const NodeId c = addBox(doc, 11, 200, 30, 20);
    const std::vector<NodeId> ids{a, b, c};
    CHECK(distributeNodes(doc, ids, DistributeAxis::Vertical));
    // total 60, span 220, gap 80
    CHECK(rectOf(doc, b).y == doctest::Approx(90.0f));
    CHECK(rectOf(doc, b).x == 9);
    CHECK(rectOf(doc, a).y == 0);
    CHECK(rectOf(doc, c).y == 200);
}

TEST_CASE("distributeNodes treats groups as units and does not double-move") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0, 20, 20);
    const NodeId g1 = addBox(doc, 40, 0, 20, 20);
    const NodeId g2 = addBox(doc, 70, 0, 20, 20); // group 40..90
    const NodeId z = addBox(doc, 300, 0, 20, 20);
    REQUIRE(doc.group(std::vector<NodeId>{g1, g2}) != kNoGroup);
    const std::vector<NodeId> ids{a, g1, g2, z};
    // units: a (20), group (50), z (20): total 90, span 320, gap 115
    CHECK(distributeNodes(doc, ids, DistributeAxis::Horizontal));
    CHECK(rectOf(doc, g1).x == doctest::Approx(135.0f));
    CHECK(rectOf(doc, g2).x == doctest::Approx(165.0f)); // 30 right of g1, as before
    CHECK(rectOf(doc, a).x == 0);
    CHECK(rectOf(doc, z).x == 300);
}

TEST_CASE("distributeNodes overlapping units spread evenly (negative gap)") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0, 100, 10);
    const NodeId b = addBox(doc, 5, 0, 100, 10);
    const NodeId c = addBox(doc, 50, 0, 100, 10); // span 150, total 300, gap -75
    const std::vector<NodeId> ids{a, b, c};
    CHECK(distributeNodes(doc, ids, DistributeAxis::Horizontal));
    CHECK(rectOf(doc, b).x == doctest::Approx(25.0f));
}

TEST_CASE("distributeNodes needs three units and is a no-op when already even") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0, 20, 20);
    const NodeId b = addBox(doc, 100, 0, 20, 20);
    const NodeId c = addBox(doc, 200, 0, 20, 20);
    const auto rev0 = doc.revision();
    CHECK_FALSE(distributeNodes(doc, std::vector<NodeId>{a, b}, DistributeAxis::Horizontal));
    CHECK_FALSE(distributeNodes(doc, std::vector<NodeId>{a, b, 9999}, DistributeAxis::Horizontal));
    CHECK_FALSE(distributeNodes(doc, std::vector<NodeId>{a, b, b, a}, DistributeAxis::Horizontal));
    CHECK_FALSE(distributeNodes(doc, std::vector<NodeId>{a, b, c}, DistributeAxis::Horizontal)); // already even
    CHECK_FALSE(distributeNodes(doc, std::vector<NodeId>{}, DistributeAxis::Vertical));
    CHECK(doc.revision() == rev0);
}

TEST_CASE("distributeNodes: two groups plus one node make three units, a single group makes one") {
    FlowDocument doc;
    const NodeId a1 = addBox(doc, 0, 0, 10, 10);
    const NodeId a2 = addBox(doc, 20, 0, 10, 10);
    const NodeId b1 = addBox(doc, 300, 0, 10, 10);
    const NodeId b2 = addBox(doc, 320, 0, 10, 10);
    REQUIRE(doc.group(std::vector<NodeId>{a1, a2}) != kNoGroup);
    REQUIRE(doc.group(std::vector<NodeId>{b1, b2}) != kNoGroup);
    CHECK_FALSE(distributeNodes(doc, std::vector<NodeId>{a1, a2, b1, b2}, DistributeAxis::Horizontal)); // 2 units
}

TEST_CASE("matchSize copies width or height of the first node, keeping x and y") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 10, 20, 150, 90);
    const NodeId b = addBox(doc, 300, 400, 40, 30);
    const NodeId c = addBox(doc, -50, 70, 80, 200);

    SUBCASE("width") {
        CHECK(matchSize(doc, std::vector<NodeId>{a, b, c}, MatchMode::Width));
        CHECK(rectOf(doc, b) == (Rect{300, 400, 150, 30}));
        CHECK(rectOf(doc, c) == (Rect{-50, 70, 150, 200}));
        CHECK(rectOf(doc, a) == (Rect{10, 20, 150, 90}));
    }
    SUBCASE("height") {
        CHECK(matchSize(doc, std::vector<NodeId>{a, b, c}, MatchMode::Height));
        CHECK(rectOf(doc, b) == (Rect{300, 400, 40, 90}));
        CHECK(rectOf(doc, c) == (Rect{-50, 70, 80, 90}));
    }
    SUBCASE("the reference is the first id that exists, not the first node in the document") {
        CHECK(matchSize(doc, std::vector<NodeId>{9999, c, a}, MatchMode::Height));
        CHECK(rectOf(doc, a).h == 200);
        CHECK(rectOf(doc, b).h == 30); // not listed
    }
    SUBCASE("duplicates and unknown ids are ignored") {
        CHECK(matchSize(doc, std::vector<NodeId>{a, a, 9999, b, kNoNode, b}, MatchMode::Width));
        CHECK(rectOf(doc, b).w == 150);
    }
}

TEST_CASE("matchSize resizes containers without moving children, and stretches images") {
    FlowDocument doc;
    const NodeId ref = addBox(doc, 0, 0, 500, 300);
    const NodeId box = doc.addNode(NodeKind::Container, Rect{0, 400, 200, 100});
    const NodeId child = addBox(doc, 20, 450, 30, 30);
    const NodeId pic = doc.addNode(NodeKind::Image, Rect{0, 700, 64, 32});
    REQUIRE(doc.reparent(child, box));

    CHECK(matchSize(doc, std::vector<NodeId>{ref, box, pic}, MatchMode::Width));
    CHECK(rectOf(doc, box).w == 500);
    CHECK(rectOf(doc, pic).w == 500);
    CHECK(rectOf(doc, pic).h == 32);
    CHECK(rectOf(doc, child) == (Rect{20, 450, 30, 30}));
}

TEST_CASE("matchSize is a no-op when sizes already match or fewer than two nodes exist") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0, 100, 50);
    const NodeId b = addBox(doc, 200, 200, 100, 70);
    const auto rev = doc.revision();
    CHECK_FALSE(matchSize(doc, std::vector<NodeId>{a, b}, MatchMode::Width));
    CHECK_FALSE(matchSize(doc, std::vector<NodeId>{a}, MatchMode::Width));
    CHECK_FALSE(matchSize(doc, std::vector<NodeId>{a, a}, MatchMode::Height));
    CHECK_FALSE(matchSize(doc, std::vector<NodeId>{a, 9999}, MatchMode::Height));
    CHECK_FALSE(matchSize(doc, std::vector<NodeId>{}, MatchMode::Height));
    CHECK(doc.revision() == rev);
}

TEST_CASE("layout near the coordinate limit stays finite and inside the clamp") {
    FlowDocument doc;
    const NodeId a = addBox(doc, -999000, 0, 100, 50);
    const NodeId b = addBox(doc, 999000, 0, 100, 50);
    const NodeId c = addBox(doc, 0, 0, 100, 50);
    const std::vector<NodeId> ids{a, b, c};
    alignNodes(doc, ids, AlignMode::Center);
    distributeNodes(doc, ids, DistributeAxis::Horizontal);
    for (const NodeId id : ids) {
        const Rect r = rectOf(doc, id);
        CHECK(r.x >= -kMaxCoord);
        CHECK(r.x <= kMaxCoord);
    }
}

TEST_CASE("layoutUnitCount counts groups and container members as single units") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 200, 0);
    const NodeId c = addBox(doc, 400, 0);
    const NodeId box = doc.addNode(NodeKind::Container, Rect{0, 300, 300, 300});
    const NodeId inside = addBox(doc, 20, 340, 40, 40);
    REQUIRE(doc.reparent(inside, box));
    REQUIRE(doc.group(std::vector<NodeId>{a, b}) != kNoGroup);

    CHECK(layoutUnitCount(doc, std::vector<NodeId>{a, b, c}) == 2);
    CHECK(layoutUnitCount(doc, std::vector<NodeId>{a, b, c, box, inside}) == 3);
    CHECK(layoutUnitCount(doc, std::vector<NodeId>{inside}) == 1);
    CHECK(layoutUnitCount(doc, std::vector<NodeId>{9999, kNoNode}) == 0);
    CHECK(layoutUnitCount(doc, std::vector<NodeId>{}) == 0);
}

// --- autoLayout ---

namespace {

EdgeId link(FlowDocument& doc, NodeId from, NodeId to) { return doc.connect(End{from}, End{to}); }

bool overlaps(const Rect& a, const Rect& b) {
    constexpr float eps = 1e-3f;
    return a.x < b.right() - eps && b.x < a.right() - eps && a.y < b.bottom() - eps && b.y < a.bottom() - eps;
}

Rect boundsOfIds(const FlowDocument& doc, const std::vector<NodeId>& ids) { return doc.boundsOf(ids); }

// Three boxes scattered about, joined A -> B -> C.
struct Chain {
    FlowDocument doc;
    NodeId a, b, c;
    std::vector<NodeId> ids;
    Chain() {
        a = addBox(doc, 300, 200);
        b = addBox(doc, 0, 0);
        c = addBox(doc, 500, 50, 60, 70);
        ids = {a, b, c};
        link(doc, a, b);
        link(doc, b, c);
    }
};

} // namespace

TEST_CASE("autoLayout is a no-op for an empty document, a single node and bad spacing") {
    FlowDocument empty;
    CHECK_FALSE(autoLayout(empty, {}, LayoutDirection::TopDown));
    CHECK(empty.revision() == 0);

    FlowDocument one;
    const NodeId a = addBox(one, 30, 40);
    const std::uint64_t rev = one.revision();
    CHECK_FALSE(autoLayout(one, {}, LayoutDirection::TopDown));
    CHECK_FALSE(autoLayout(one, std::vector<NodeId>{a}, LayoutDirection::LeftRight));
    CHECK_FALSE(autoLayout(one, std::vector<NodeId>{9999}, LayoutDirection::TopDown));
    CHECK(one.revision() == rev);
    CHECK(rectOf(one, a) == Rect{30, 40, 100, 50});

    Chain t;
    const std::uint64_t before = t.doc.revision();
    CHECK_FALSE(autoLayout(t.doc, {}, LayoutDirection::TopDown, std::nanf("")));
    CHECK_FALSE(autoLayout(t.doc, {}, LayoutDirection::TopDown, INFINITY));
    CHECK_FALSE(autoLayout(t.doc, {}, LayoutDirection::TopDown, -INFINITY));
    CHECK(t.doc.revision() == before);
}

TEST_CASE("autoLayout puts a chain in increasing layers with the spacing between them") {
    SUBCASE("top down") {
        Chain t;
        REQUIRE(autoLayout(t.doc, t.ids, LayoutDirection::TopDown, 60.0f));
        const Rect a = rectOf(t.doc, t.a), b = rectOf(t.doc, t.b), c = rectOf(t.doc, t.c);
        CHECK(a.y < b.y);
        CHECK(b.y < c.y);
        CHECK(b.y - a.bottom() == doctest::Approx(60.0f));
        CHECK(c.y - b.bottom() == doctest::Approx(60.0f));
        CHECK(a.center().x == doctest::Approx(b.center().x));
        CHECK(b.center().x == doctest::Approx(c.center().x));
        // The top-left of the bounds stays where it was (0, 0 here).
        const Rect all = boundsOfIds(t.doc, t.ids);
        CHECK(all.x == doctest::Approx(0.0f));
        CHECK(all.y == doctest::Approx(0.0f));
        // Sizes never change.
        CHECK(c.w == 60);
        CHECK(c.h == 70);
    }
    SUBCASE("left to right") {
        Chain t;
        REQUIRE(autoLayout(t.doc, t.ids, LayoutDirection::LeftRight, 40.0f));
        const Rect a = rectOf(t.doc, t.a), b = rectOf(t.doc, t.b), c = rectOf(t.doc, t.c);
        CHECK(a.x < b.x);
        CHECK(b.x < c.x);
        CHECK(b.x - a.right() == doctest::Approx(40.0f));
        CHECK(c.x - b.right() == doctest::Approx(40.0f));
        CHECK(a.center().y == doctest::Approx(b.center().y));
        CHECK(b.center().y == doctest::Approx(c.center().y));
    }
    SUBCASE("negative spacing clamps to zero") {
        Chain t;
        REQUIRE(autoLayout(t.doc, t.ids, LayoutDirection::TopDown, -50.0f));
        CHECK(rectOf(t.doc, t.b).y - rectOf(t.doc, t.a).bottom() == doctest::Approx(0.0f));
    }
}

TEST_CASE("autoLayout breaks a cycle so only the DFS back edge points back") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 400, 400);
    const NodeId b = addBox(doc, 0, 0);
    const NodeId c = addBox(doc, 200, 100);
    link(doc, a, b);
    link(doc, b, c);
    link(doc, c, a); // the back edge
    const std::vector<NodeId> ids{a, b, c};
    REQUIRE(autoLayout(doc, ids, LayoutDirection::TopDown));
    CHECK(rectOf(doc, a).y < rectOf(doc, b).y);
    CHECK(rectOf(doc, b).y < rectOf(doc, c).y);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) CHECK_FALSE(overlaps(rectOf(doc, ids[i]), rectOf(doc, ids[j])));
    }

    // Self loops and two-way connectors are fine too.
    FlowDocument two;
    const NodeId p = addBox(two, 0, 0);
    const NodeId q = addBox(two, 0, 0);
    link(two, p, p);
    link(two, p, q);
    link(two, q, p);
    REQUIRE(autoLayout(two, {}, LayoutDirection::LeftRight));
    CHECK(rectOf(two, p).x < rectOf(two, q).x);
}

TEST_CASE("autoLayout centers the two branches of a diamond under the root") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 500, 500);
    const NodeId b = addBox(doc, 10, 0);
    const NodeId c = addBox(doc, 20, 900);
    const NodeId d = addBox(doc, 700, 20);
    link(doc, a, b);
    link(doc, a, c);
    link(doc, b, d);
    link(doc, c, d);
    REQUIRE(autoLayout(doc, {}, LayoutDirection::TopDown, 60.0f));
    const Rect ra = rectOf(doc, a), rb = rectOf(doc, b), rc = rectOf(doc, c), rd = rectOf(doc, d);
    CHECK(rb.y == doctest::Approx(rc.y));
    CHECK(rb.y > ra.y);
    CHECK(rd.y > rb.y);
    CHECK(rb.x < rc.x); // document order breaks the tie
    CHECK(rc.x - rb.right() == doctest::Approx(60.0f * 2.0f / 3.0f));
    CHECK(ra.center().x == doctest::Approx((rb.center().x + rc.center().x) * 0.5f));
    CHECK(rd.center().x == doctest::Approx(ra.center().x));
}

TEST_CASE("autoLayout reorders layers to remove a crossing") {
    FlowDocument doc;
    const NodeId p = addBox(doc, 0, 0);
    const NodeId q = addBox(doc, 0, 0);
    const NodeId r = addBox(doc, 0, 0);
    const NodeId s = addBox(doc, 0, 0);
    link(doc, p, s);
    link(doc, q, r);
    REQUIRE(autoLayout(doc, {}, LayoutDirection::TopDown));
    // The naive order (p, q over r, s) would cross; the barycenter order does not.
    CHECK(rectOf(doc, p).x < rectOf(doc, q).x);
    CHECK(rectOf(doc, s).x < rectOf(doc, r).x);
    CHECK(rectOf(doc, p).y == doctest::Approx(rectOf(doc, q).y));
    CHECK(rectOf(doc, r).y == doctest::Approx(rectOf(doc, s).y));
}

TEST_CASE("autoLayout sets disconnected components side by side") {
    FlowDocument doc;
    const NodeId a1 = addBox(doc, 0, 0), a2 = addBox(doc, 0, 0), a3 = addBox(doc, 0, 0);
    const NodeId b1 = addBox(doc, 0, 0), b2 = addBox(doc, 0, 0);
    const NodeId lone = addBox(doc, 0, 0, 30, 30);
    link(doc, a1, a2);
    link(doc, a1, a3);
    link(doc, b1, b2);
    REQUIRE(autoLayout(doc, {}, LayoutDirection::TopDown, 60.0f));
    const Rect ca = boundsOfIds(doc, {a1, a2, a3});
    const Rect cb = boundsOfIds(doc, {b1, b2});
    const Rect cl = rectOf(doc, lone);
    CHECK(ca.x == doctest::Approx(0.0f));
    CHECK(cb.x >= ca.right() + 60.0f - 0.01f);
    CHECK(cl.x >= cb.right() + 60.0f - 0.01f);
    CHECK_FALSE(overlaps(ca, cb));
    CHECK_FALSE(overlaps(cb, cl));
    // They all start on the layer axis' origin (same top).
    CHECK(rectOf(doc, a1).y == doctest::Approx(rectOf(doc, b1).y));

    // LeftRight stacks them downwards instead.
    REQUIRE(autoLayout(doc, {}, LayoutDirection::LeftRight, 60.0f));
    const Rect ra = boundsOfIds(doc, {a1, a2, a3});
    const Rect rb = boundsOfIds(doc, {b1, b2});
    CHECK(rb.y >= ra.bottom() + 60.0f - 0.01f);
}

TEST_CASE("autoLayout is deterministic and idempotent") {
    const auto build = [](FlowDocument& doc) {
        std::vector<NodeId> n;
        for (int i = 0; i < 8; ++i) n.push_back(addBox(doc, static_cast<float>(i * 37 % 200), static_cast<float>(i * 91 % 300)));
        link(doc, n[0], n[1]);
        link(doc, n[0], n[2]);
        link(doc, n[1], n[3]);
        link(doc, n[2], n[3]);
        link(doc, n[3], n[0]);
        link(doc, n[4], n[5]);
        link(doc, n[5], n[6]);
        link(doc, n[6], n[4]);
        link(doc, n[1], n[7]);
    };
    FlowDocument d1, d2;
    build(d1);
    build(d2);
    REQUIRE(autoLayout(d1, {}, LayoutDirection::TopDown));
    REQUIRE(autoLayout(d2, {}, LayoutDirection::TopDown));
    REQUIRE(d1.nodes().size() == d2.nodes().size());
    for (std::size_t i = 0; i < d1.nodes().size(); ++i) CHECK(d1.nodes()[i].rect == d2.nodes()[i].rect);

    // Laying out its own output changes nothing, in either direction.
    const std::uint64_t rev = d1.revision();
    CHECK_FALSE(autoLayout(d1, {}, LayoutDirection::TopDown));
    CHECK(d1.revision() == rev);
    REQUIRE(autoLayout(d1, {}, LayoutDirection::LeftRight));
    const std::uint64_t rev2 = d1.revision();
    CHECK_FALSE(autoLayout(d1, {}, LayoutDirection::LeftRight));
    CHECK(d1.revision() == rev2);
}

TEST_CASE("autoLayout lays out a container with its children as one block") {
    FlowDocument doc;
    const NodeId start = addBox(doc, 900, 900);
    const NodeId box = doc.addNode(NodeKind::Container, Rect{0, 0, 300, 200});
    const NodeId inside = addBox(doc, 20, 50, 40, 40);
    const NodeId other = addBox(doc, 20, 100, 40, 40);
    REQUIRE(doc.reparent(inside, box));
    REQUIRE(doc.reparent(other, box));
    link(doc, start, box);
    link(doc, inside, other); // inside one unit: ignored
    const Rect boxBefore = rectOf(doc, box);
    const Rect inBefore = rectOf(doc, inside);
    const Rect otherBefore = rectOf(doc, other);

    SUBCASE("everything selected") {
        REQUIRE(autoLayout(doc, {}, LayoutDirection::LeftRight, 50.0f));
    }
    SUBCASE("children listed explicitly") {
        const std::vector<NodeId> ids{start, box, inside, other};
        REQUIRE(autoLayout(doc, ids, LayoutDirection::LeftRight, 50.0f));
    }
    const Rect rb = rectOf(doc, box);
    CHECK(rb.w == boxBefore.w);
    CHECK(rb.h == boxBefore.h);
    CHECK(rectOf(doc, inside).x - rb.x == doctest::Approx(inBefore.x - boxBefore.x));
    CHECK(rectOf(doc, inside).y - rb.y == doctest::Approx(inBefore.y - boxBefore.y));
    CHECK(rectOf(doc, other).x - rb.x == doctest::Approx(otherBefore.x - boxBefore.x));
    CHECK(rectOf(doc, other).y - rb.y == doctest::Approx(otherBefore.y - boxBefore.y));
    CHECK(rb.x >= rectOf(doc, start).right() + 50.0f - 0.01f);
    CHECK_FALSE(overlaps(rb, rectOf(doc, start)));
}

TEST_CASE("autoLayout moves a group as a block") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0, 100, 50);
    const NodeId b = addBox(doc, 150, 80, 100, 50);
    const NodeId c = addBox(doc, 700, 700, 100, 50);
    REQUIRE(doc.group(std::vector<NodeId>{a, b}) != kNoGroup);
    link(doc, b, c);
    const float dx = rectOf(doc, b).x - rectOf(doc, a).x;
    const float dy = rectOf(doc, b).y - rectOf(doc, a).y;
    // Only `a` is listed; its group mate b comes along as part of the same unit.
    REQUIRE(autoLayout(doc, std::vector<NodeId>{a, c}, LayoutDirection::TopDown, 60.0f));
    CHECK(rectOf(doc, b).x - rectOf(doc, a).x == doctest::Approx(dx));
    CHECK(rectOf(doc, b).y - rectOf(doc, a).y == doctest::Approx(dy));
    const Rect group = boundsOfIds(doc, {a, b});
    CHECK(rectOf(doc, c).y >= group.bottom() + 60.0f - 0.01f);
    CHECK_FALSE(overlaps(group, rectOf(doc, c)));
}

TEST_CASE("autoLayout with a selection moves only those units") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 400, 300);
    const NodeId b = addBox(doc, 0, 0);
    const NodeId c = addBox(doc, 800, 100);
    const NodeId d = addBox(doc, 50, 600);
    link(doc, a, b);
    const EdgeId bc = link(doc, b, c);
    link(doc, c, d);
    REQUIRE(doc.reconnect(bc, true, End{b, Face::Right, {}}));
    const Rect cBefore = rectOf(doc, c), dBefore = rectOf(doc, d);
    REQUIRE(autoLayout(doc, std::vector<NodeId>{a, b}, LayoutDirection::TopDown, 60.0f));
    CHECK(rectOf(doc, c) == cBefore);
    CHECK(rectOf(doc, d) == dBefore);
    CHECK(rectOf(doc, b).y - rectOf(doc, a).bottom() == doctest::Approx(60.0f));
    // Anchored at the top-left of the two selected nodes' old bounds (0, 0).
    const Rect ab = boundsOfIds(doc, {a, b});
    CHECK(ab.x == doctest::Approx(0.0f));
    CHECK(ab.y == doctest::Approx(0.0f));
    // The connector to the node left out keeps its face.
    CHECK(doc.edge(bc)->from.face == Face::Right);
}

TEST_CASE("autoLayout sets docked connector ends among the laid-out units to Auto") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 300, 0);
    const NodeId b = addBox(doc, 0, 0);
    const NodeId g1 = addBox(doc, 0, 400);
    const NodeId g2 = addBox(doc, 200, 400);
    const NodeId out = addBox(doc, 900, 900);
    REQUIRE(doc.group(std::vector<NodeId>{g1, g2}) != kNoGroup);
    const EdgeId ab = doc.connect(End{a, Face::Right, {}}, End{b, Face::Left, {}});
    const EdgeId freeEnd = doc.connect(End{a, Face::Bottom, {}}, End{kNoNode, Face::Auto, Pt{5, 6}});
    const EdgeId inGroup = doc.connect(End{g1, Face::Bottom, {}}, End{g2, Face::Top, {}});
    const EdgeId toOut = doc.connect(End{b, Face::Top, {}}, End{out, Face::Left, {}});
    REQUIRE((ab && freeEnd && inGroup && toOut));

    const std::vector<NodeId> ids{a, b, g1, g2};
    REQUIRE(autoLayout(doc, ids, LayoutDirection::TopDown));
    CHECK(doc.edge(ab)->from.face == Face::Auto);
    CHECK(doc.edge(ab)->to.face == Face::Auto);
    CHECK(doc.edge(freeEnd)->from.face == Face::Bottom);
    CHECK(doc.edge(freeEnd)->to.point == Pt{5, 6});
    CHECK(doc.edge(inGroup)->from.face == Face::Bottom);
    CHECK(doc.edge(inGroup)->to.face == Face::Top);
    CHECK(doc.edge(toOut)->from.face == Face::Top);
    CHECK(doc.edge(toOut)->to.face == Face::Left);
}

TEST_CASE("autoLayout handles a large random DAG quickly without overlaps") {
    FlowDocument doc;
    std::uint32_t state = 12345u;
    const auto next = [&state](std::uint32_t mod) {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) % mod;
    };
    constexpr std::uint32_t kNodes = 2000;
    constexpr int kEdges = 4000;
    std::vector<NodeId> ids;
    for (std::uint32_t i = 0; i < kNodes; ++i) {
        ids.push_back(addBox(doc, static_cast<float>(next(5000)), static_cast<float>(next(5000)),
                             40.0f + static_cast<float>(next(120)), 30.0f + static_cast<float>(next(60))));
    }
    for (int e = 0; e < kEdges; ++e) {
        const std::uint32_t i = next(kNodes - 1);
        const std::uint32_t j = i + 1 + next(kNodes - 1 - i);
        link(doc, ids[i], ids[j]);
    }

    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE(autoLayout(doc, {}, LayoutDirection::TopDown, 60.0f));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(seconds < 10.0);

    for (const Node& n : doc.nodes()) {
        CHECK(std::isfinite(n.rect.x));
        CHECK(std::isfinite(n.rect.y));
        CHECK(std::fabs(n.rect.x) <= kMaxCoord);
        CHECK(std::fabs(n.rect.y) <= kMaxCoord);
    }
    // Every pair among a sample.
    std::vector<NodeId> sample;
    for (int i = 0; i < 300; ++i) sample.push_back(ids[next(kNodes)]);
    int overlapping = 0;
    for (std::size_t i = 0; i < sample.size(); ++i) {
        for (std::size_t j = i + 1; j < sample.size(); ++j) {
            if (sample[i] != sample[j] && overlaps(rectOf(doc, sample[i]), rectOf(doc, sample[j]))) ++overlapping;
        }
    }
    CHECK(overlapping == 0);
    // The graph has no cycle, so every connector points down.
    for (const Edge& e : doc.edges()) CHECK(doc.node(e.to.node)->rect.y > doc.node(e.from.node)->rect.y);
}

TEST_CASE("autoLayout routes the laid-out connectors automatically again") {
    FlowDocument doc;
    const NodeId a = addBox(doc, 0, 0);
    const NodeId b = addBox(doc, 500, 300);
    const NodeId c = addBox(doc, 40, 600);
    const EdgeId ab = doc.connect(End{a, Face::Auto, {}}, End{b, Face::Auto, {}});
    const EdgeId bc = doc.connect(End{b, Face::Auto, {}}, End{c, Face::Auto, {}});
    REQUIRE(doc.setEdgeVia(ab, Pt{12, 12}));
    REQUIRE(doc.setEdgeVia(bc, Pt{-8, 4}));
    const std::vector<NodeId> ids{a, b, c};
    CHECK(autoLayout(doc, ids, LayoutDirection::TopDown));
    CHECK_FALSE(doc.edge(ab)->via.has_value());
    CHECK_FALSE(doc.edge(bc)->via.has_value());
}
