#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/symmetry.hpp"

using namespace trace;

namespace {

constexpr double kPi = 3.14159265358979323846;

// The rotation counts every plan test walks through (1 = no rotation item).
const int kRotationCounts[] = {1, 2, 3, 4, 6, 8};

SymmetryItem mirrorAt(int angle, int px = kSymmetryCenter, int py = kSymmetryCenter) {
    SymmetryItem it = makeMirrorX();
    it.angle = angle;
    it.posX = px;
    it.posY = py;
    return it;
}

SymmetryItem rotateAt(int count, int px = kSymmetryCenter, int py = kSymmetryCenter) {
    SymmetryItem it = makeRotate(count);
    it.posX = px;
    it.posY = py;
    return it;
}

SymmetrySet setOf(std::initializer_list<SymmetryItem> items) {
    SymmetrySet set;
    for (const SymmetryItem& it : items)
        addSymmetryItem(set, it);
    return set;
}

// The old "x, y, axes, rotations" setups as a list: Mirror X, Mirror Y, then a Rotate about where the axes cross.
SymmetrySet legacySet(bool x, bool y, int rotations, int axisX, int axisY) {
    SymmetrySet set;
    if (x)
        addSymmetryItem(set, mirrorAt(0, axisX, axisY));
    if (y)
        addSymmetryItem(set, mirrorAt(90, axisX, axisY));
    if (sanitizeRotations(rotations) > 1)
        addSymmetryItem(set, rotateAt(rotations, axisX, axisY));
    return set;
}

SymmetryPlan planFor(bool x, bool y, int rotations, int w = 30, int h = 20, int axisX = kSymmetryCenter,
                     int axisY = kSymmetryCenter) {
    return resolveSymmetry(legacySet(x, y, rotations, axisX, axisY), w, h);
}

bool sameTransform(const SymmetryPlan::Affine& p, const SymmetryPlan::Affine& q) {
    return std::abs(p.a - q.a) < 1e-6 && std::abs(p.b - q.b) < 1e-6 && std::abs(p.c - q.c) < 1e-6 &&
           std::abs(p.d - q.d) < 1e-6 && std::abs(p.tx - q.tx) < 1e-4 && std::abs(p.ty - q.ty) < 1e-4;
}

// True when every copy of `a` is a copy of `b` and the other way round (the order does not matter).
bool sameCopies(const SymmetryPlan& a, const SymmetryPlan& b) {
    if (a.count() != b.count())
        return false;
    for (std::size_t i = 0; i < a.count(); ++i) {
        bool found = false;
        for (std::size_t j = 0; j < b.count() && !found; ++j)
            found = sameTransform(a.transform(i), b.transform(j));
        if (!found)
            return false;
    }
    return true;
}

bool allDistinct(const SymmetryPlan& plan) {
    for (std::size_t i = 0; i < plan.count(); ++i)
        for (std::size_t j = i + 1; j < plan.count(); ++j)
            if (sameTransform(plan.transform(i), plan.transform(j)))
                return false;
    return true;
}

std::set<std::pair<int, int>> imagesOf(const SymmetryPlan& plan, std::size_t k, const IRect& r) {
    std::set<std::pair<int, int>> out;
    for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x) {
            const PixelPos p = plan.pixel(k, x, y);
            out.insert({p.x, p.y});
        }
    return out;
}

std::set<std::pair<int, int>> coveredBy(const IRect& m) {
    std::set<std::pair<int, int>> out;
    for (int y = m.y0; y < m.y1; ++y)
        for (int x = m.x0; x < m.x1; ++x)
            out.insert({x, y});
    return out;
}

} // namespace

// --- the SymmetrySet list ---------------------------------------------------------------------------

TEST_CASE("Rotation counts and mirror angles are sanitized") {
    for (const int n : {2, 3, 4, 6, 8})
        CHECK(sanitizeRotateCount(n) == n);
    for (const int n : {0, 1, 5, 7, 9, 16, -4, 1 << 30})
        CHECK(sanitizeRotateCount(n) == 4);
    CHECK(normalizeMirrorAngle(0) == 0);
    CHECK(normalizeMirrorAngle(179) == 179);
    CHECK(normalizeMirrorAngle(180) == 0);
    CHECK(normalizeMirrorAngle(200) == 20);
    CHECK(normalizeMirrorAngle(-45) == 135);
    CHECK(normalizeMirrorAngle(-180) == 0);
    CHECK(normalizeMirrorAngle(std::numeric_limits<int>::min()) >= 0);
    CHECK(normalizeMirrorAngle(std::numeric_limits<int>::min()) < 180);
    CHECK(normalizeMirrorAngle(std::numeric_limits<int>::max()) < 180);
}

TEST_CASE("sanitizedItem forces every field into range") {
    SymmetryItem it;
    it.angle = -45;
    it.count = 7;
    it.preset = 9;
    const SymmetryItem fixed = sanitizedItem(it);
    CHECK(fixed.angle == 135);
    CHECK(fixed.count == 4);
    CHECK(fixed.preset == 0);
    CHECK(makeMirrorX().angle == 0);
    CHECK(makeMirrorY().angle == 90);
    CHECK(makeMirrorY().preset == 1);
    CHECK(makeRotate(6).count == 6);
    CHECK(makeRotate(5).count == 4);
}

TEST_CASE("Items are added and removed in order, up to the item cap") {
    SymmetrySet set;
    CHECK(set.count == 0);
    CHECK_FALSE(symmetryActive(set));
    CHECK(addSymmetryItem(set, makeMirrorX()));
    CHECK(addSymmetryItem(set, makeMirrorY()));
    CHECK(addSymmetryItem(set, makeRotate(3)));
    CHECK(set.count == 3);
    CHECK(set.items[2].kind == SymmetryKind::Rotate);

    CHECK(removeSymmetryItem(set, 0));
    CHECK(set.count == 2);
    CHECK(set.items[0].preset == 1); // Mirror Y moved up
    CHECK(set.items[1].kind == SymmetryKind::Rotate);
    CHECK(set.items[2] == SymmetryItem{}); // the freed slot is reset
    CHECK_FALSE(removeSymmetryItem(set, 2));
    CHECK_FALSE(removeSymmetryItem(set, -1));
    CHECK(removeSymmetryItem(set, 1));
    CHECK(removeSymmetryItem(set, 0));
    CHECK(set.count == 0);
    CHECK(set == SymmetrySet{});

    // The copy cap bites first for enabled mirrors: the estimate is 2 per mirror, so only six fit.
    for (int i = 0; i < 6; ++i)
        CHECK(addSymmetryItem(set, makeMirrorX()));
    CHECK_FALSE(addSymmetryItem(set, makeMirrorX())); // 2^7 copies
    CHECK(set.count == 6);
}

TEST_CASE("The item list is capped at kMaxSymmetryItems") {
    SymmetrySet set;
    SymmetryItem off = makeMirrorX();
    off.enabled = false; // disabled items do not count toward the copies
    for (int i = 0; i < kMaxSymmetryItems; ++i)
        CHECK(addSymmetryItem(set, off));
    CHECK(set.count == kMaxSymmetryItems);
    CHECK_FALSE(addSymmetryItem(set, off));
    CHECK_FALSE(symmetryActive(set));
    CHECK(symmetryCopyEstimate(set) == 1);
}

TEST_CASE("The copy estimate multiplies mirrors by two and rotations by their count") {
    SymmetrySet set;
    CHECK(symmetryCopyEstimate(set) == 1);
    addSymmetryItem(set, makeMirrorX());
    CHECK(symmetryCopyEstimate(set) == 2);
    addSymmetryItem(set, makeMirrorY());
    CHECK(symmetryCopyEstimate(set) == 4);
    addSymmetryItem(set, makeRotate(8));
    CHECK(symmetryCopyEstimate(set) == 32);
    CHECK(canAddSymmetryItem(set, makeMirrorX()));  // 64
    CHECK_FALSE(canAddSymmetryItem(set, makeRotate(3)));
    addSymmetryItem(set, makeMirrorX());
    CHECK(symmetryCopyEstimate(set) == 64);
    CHECK_FALSE(addSymmetryItem(set, makeMirrorY()));
    CHECK(set.count == 4);

    SymmetryItem off = makeRotate(8);
    off.enabled = false;
    CHECK(canAddSymmetryItem(set, off)); // a disabled item adds no copies
}

TEST_CASE("symmetryActive needs one enabled item") {
    SymmetrySet set = setOf({makeMirrorX(), makeRotate(4)});
    CHECK(symmetryActive(set));
    set.items[0].enabled = false;
    CHECK(symmetryActive(set));
    set.items[1].enabled = false;
    CHECK_FALSE(symmetryActive(set));
    // A corrupt count is clamped instead of read past the array.
    set.count = 99; // read up to the array's end (the unused slots are default mirrors), never past it
    CHECK(symmetryActive(set));
    set.count = -3;
    CHECK_FALSE(symmetryActive(set));
    CHECK(symmetryCopyEstimate(set) == 1);
}

// --- the plan: items to copies ---------------------------------------------------------------------

TEST_CASE("resolveSymmetry puts a center position in the middle of the canvas") {
    const SymmetryPlan plan = resolveSymmetry(setOf({mirrorAt(0), mirrorAt(90)}), 20, 13);
    REQUIRE(plan.count() == 4);
    CHECK(plan.transform(1).tx == 20.0); // x' = 20 - x: half pixels 20, between pixels 9 and 10
    CHECK(plan.transform(2).ty == 13.0); // an odd height: through the middle of pixel 6
}

TEST_CASE("resolveSymmetry clamps an explicit position onto the canvas") {
    const auto txOf = [](int pos, int size) { return resolveSymmetry(setOf({mirrorAt(0, pos)}), size, 20).transform(1).tx; };
    CHECK(txOf(7, 20) == 7.0);
    CHECK(txOf(999, 20) == 40.0);
    CHECK(txOf(0, 20) == 0.0);
    CHECK(txOf(40, 20) == 40.0);
    CHECK(txOf(41, 20) == 40.0);
    CHECK(txOf(std::numeric_limits<int>::max(), 20) == 40.0);
    // Any negative value means the center.
    CHECK(txOf(-7, 20) == 20.0);
    CHECK(txOf(std::numeric_limits<int>::min(), 20) == 20.0);
    const SymmetryPlan y = resolveSymmetry(setOf({mirrorAt(90, kSymmetryCenter, 9)}), 20, 20);
    CHECK(y.transform(1).ty == 9.0);
}

TEST_CASE("An empty canvas or an empty list gives a plan of the original only") {
    CHECK_FALSE(resolveSymmetry(SymmetrySet{}, 8, 8).active());
    CHECK(resolveSymmetry(SymmetrySet{}, 8, 8).count() == 1);
    const SymmetrySet set = setOf({mirrorAt(0), mirrorAt(90), rotateAt(4)});
    CHECK_FALSE(resolveSymmetry(set, 0, 5).active());
    CHECK_FALSE(resolveSymmetry(set, 5, 0).active());
    CHECK_FALSE(resolveSymmetry(set, 0, 0).active());
    CHECK(resolveSymmetry(set, -3, 5).count() == 1);
}

TEST_CASE("A plan is inactive and has one copy unless an item is enabled") {
    CHECK_FALSE(planFor(false, false, 1).active());
    CHECK(planFor(false, false, 1).count() == 1);
    CHECK(planFor(true, false, 1).count() == 2);
    CHECK(planFor(false, true, 1).count() == 2);
    CHECK(planFor(true, true, 1).count() == 4);

    SymmetrySet set = setOf({mirrorAt(0), mirrorAt(90), rotateAt(8)});
    for (int i = 0; i < set.count; ++i)
        set.items[static_cast<std::size_t>(i)].enabled = false;
    CHECK_FALSE(resolveSymmetry(set, 20, 20).active());
    CHECK(resolveSymmetry(set, 20, 20).count() == 1);
    set.items[1].enabled = true; // only Mirror Y
    CHECK(resolveSymmetry(set, 20, 20).count() == 2);
}

TEST_CASE("Disabled items are skipped wherever they sit in the list") {
    SymmetrySet set = setOf({mirrorAt(0), mirrorAt(90), rotateAt(4)});
    set.items[1].enabled = false;
    const SymmetryPlan plan = resolveSymmetry(set, 20, 20);
    CHECK(sameCopies(plan, resolveSymmetry(setOf({mirrorAt(0), rotateAt(4)}), 20, 20)));
    CHECK(plan.count() == 8);
}

TEST_CASE("A mirror appends the reflection of every copy, in blocks: original, X, Y, both") {
    const SymmetryPlan both = resolveSymmetry(setOf({mirrorAt(0), mirrorAt(90)}), 20, 20);
    REQUIRE(both.count() == 4);
    CHECK(both.pixel(0, 3, 4) == PixelPos{3, 4});
    CHECK(both.pixel(1, 3, 4) == PixelPos{16, 4});  // across X
    CHECK(both.pixel(2, 3, 4) == PixelPos{3, 15});  // across Y
    CHECK(both.pixel(3, 3, 4) == PixelPos{16, 15}); // across both

    const SymmetryPlan onlyY = resolveSymmetry(setOf({mirrorAt(90)}), 20, 20);
    CHECK(onlyY.pixel(1, 3, 4) == PixelPos{3, 15});
}

TEST_CASE("Item order decides the order of the copies and, off the mirror line, the copies themselves") {
    // Rotate about (7, 4) and mirror across x = 10: turning first and mirroring after is a different picture from
    // mirroring first and turning after, because the center is not on the line.
    const SymmetryPlan turnThenMirror = resolveSymmetry(setOf({rotateAt(4, 14, 8), mirrorAt(0, 20)}), 20, 20);
    const SymmetryPlan mirrorThenTurn = resolveSymmetry(setOf({mirrorAt(0, 20), rotateAt(4, 14, 8)}), 20, 20);
    CHECK(turnThenMirror.count() == 8);
    CHECK(mirrorThenTurn.count() == 8);
    CHECK_FALSE(sameCopies(turnThenMirror, mirrorThenTurn));
    // Copy 1 is the first turn in one list and the mirror in the other.
    CHECK(turnThenMirror.pixel(1, 3, 4) != mirrorThenTurn.pixel(1, 3, 4));
    CHECK(mirrorThenTurn.pixel(1, 3, 4) == PixelPos{16, 4});

    // With the center on the line the two orders make the same set of copies.
    const SymmetryPlan a = resolveSymmetry(setOf({rotateAt(4, 20, 20), mirrorAt(0, 20)}), 20, 20);
    const SymmetryPlan b = resolveSymmetry(setOf({mirrorAt(0, 20), rotateAt(4, 20, 20)}), 20, 20);
    CHECK(sameCopies(a, b));
    CHECK(a.pixel(1, 3, 4) != b.pixel(1, 3, 4)); // ...in a different order
}

TEST_CASE("Two mirrors of the same kind at different positions make a translation") {
    // Lines at x = 5 and x = 10: the copies are the original, its reflections across each line, and the
    // reflection of one in the other (a shift of 10).
    const SymmetryPlan plan = resolveSymmetry(setOf({mirrorAt(0, 10), mirrorAt(0, 20)}), 30, 10);
    REQUIRE(plan.count() == 4);
    CHECK(plan.pixel(1, 2, 3) == PixelPos{7, 3});   // 10 - 2 - 1
    CHECK(plan.pixel(2, 2, 3) == PixelPos{17, 3});  // 20 - 2 - 1
    CHECK(plan.pixel(3, 2, 3) == PixelPos{12, 3});  // reflected in x = 5, then in x = 10: shifted by +10
    for (std::size_t k = 0; k < 4; ++k)
        CHECK(plan.exact(k));
    CHECK(plan.transform(3).a == 1.0);
    CHECK(plan.transform(3).tx == 10.0);

    // Mirror Y twice at different heights, with a Mirror X: 8 copies.
    CHECK(resolveSymmetry(setOf({mirrorAt(0), mirrorAt(90, kSymmetryCenter, 8), mirrorAt(90, kSymmetryCenter, 20)}), 20, 20)
              .count() == 8);
}

TEST_CASE("A copy that coincides with an existing one is dropped") {
    // The same mirror twice: its reflection of the original is copy 1, its reflection of copy 1 is the original.
    const SymmetryPlan twice = resolveSymmetry(setOf({mirrorAt(0), mirrorAt(0)}), 20, 20);
    CHECK(twice.count() == 2);
    CHECK(sameCopies(twice, resolveSymmetry(setOf({mirrorAt(0)}), 20, 20)));
    // 0 and 180 degrees are the same line; so are two positions the clamp puts together.
    CHECK(resolveSymmetry(setOf({mirrorAt(0, 20), mirrorAt(180, 20)}), 20, 20).count() == 2);
    CHECK(resolveSymmetry(setOf({mirrorAt(0, 500), mirrorAt(0, 40)}), 20, 20).count() == 2);
    CHECK(resolveSymmetry(setOf({mirrorAt(0, kSymmetryCenter), mirrorAt(0, 20)}), 20, 20).count() == 2);
    // Rotation counts that share turns.
    CHECK(resolveSymmetry(setOf({rotateAt(4), rotateAt(2)}), 20, 20).count() == 4);
    CHECK(resolveSymmetry(setOf({rotateAt(8), rotateAt(4)}), 20, 20).count() == 8);
    CHECK(resolveSymmetry(setOf({rotateAt(4), rotateAt(4)}), 20, 20).count() == 4);
    // The half turn the two mirrors already make.
    CHECK(resolveSymmetry(setOf({mirrorAt(0), mirrorAt(90), rotateAt(2)}), 20, 20).count() == 4);
    CHECK(resolveSymmetry(setOf({mirrorAt(0), mirrorAt(90), rotateAt(4)}), 20, 20).count() == 8);
    // Different centers are different copies.
    CHECK(resolveSymmetry(setOf({rotateAt(2, 10, 10), rotateAt(2, 20, 20)}), 20, 20).count() == 4);
    CHECK(allDistinct(planFor(true, true, 8)));
}

TEST_CASE("Two mirrors at odd angles that make a half turn snap to an exact copy") {
    // Mirrors through the center at 30 and 120 degrees are 90 degrees apart: together a half turn.
    const SymmetryPlan plan = resolveSymmetry(setOf({mirrorAt(30), mirrorAt(120)}), 20, 20);
    REQUIRE(plan.count() == 4);
    CHECK_FALSE(plan.exact(1));
    CHECK_FALSE(plan.exact(2));
    CHECK(plan.exact(3));
    CHECK(plan.pixel(3, 3, 4) == PixelPos{16, 15});
}

TEST_CASE("Items past the copy cap are ignored") {
    // Rotating 6 and 6 about different centers makes 36 copies; a third rotation of 3 would make 108.
    const SymmetryPlan two = resolveSymmetry(setOf({rotateAt(6, 20, 20), rotateAt(6, 10, 10)}), 40, 40);
    REQUIRE(two.count() * 3 > kMaxSymmetryCopies); // the third rotation really would not fit
    CHECK(two.count() <= kMaxSymmetryCopies);
    SymmetrySet three = setOf({rotateAt(6, 20, 20), rotateAt(6, 10, 10)});
    three.items[2] = rotateAt(3, 30, 30); // bypass the list's own guard, as a corrupt save could
    three.count = 3;
    const SymmetryPlan capped = resolveSymmetry(three, 40, 40);
    CHECK(capped.count() == two.count());
    CHECK(sameCopies(capped, two));

    // The same for mirrors past the cap: 2^7 copies.
    SymmetrySet many;
    for (int i = 0; i < kMaxSymmetryItems; ++i) {
        many.items[static_cast<std::size_t>(i)] = mirrorAt(0, 4 + 2 * i);
        many.items[static_cast<std::size_t>(i)].angle = (i * 23) % 180;
    }
    many.count = kMaxSymmetryItems;
    const SymmetryPlan big = resolveSymmetry(many, 40, 40);
    CHECK(big.count() <= kMaxSymmetryCopies);
    CHECK(big.count() > 1);
    CHECK(allDistinct(big));
    CHECK(big.pixel(0, 5, 5) == PixelPos{5, 5});
}

TEST_CASE("The copy count of mirrors and a rotation about the same point") {
    // The group generated by the items: D2 (4), D4 (8), D8 (16), D6 (12), D3 (12).
    const struct {
        int n;
        std::size_t mirrorsOnly;
        std::size_t both;
    } cases[] = {{1, 2, 4}, {2, 4, 4}, {3, 6, 12}, {4, 8, 8}, {6, 12, 12}, {8, 16, 16}};
    for (const auto& cs : cases) {
        INFO("rotations " << cs.n);
        CHECK(planFor(false, false, cs.n, 41, 41).count() == static_cast<std::size_t>(cs.n));
        CHECK(planFor(true, false, cs.n, 41, 41).count() == cs.mirrorsOnly);
        CHECK(planFor(true, true, cs.n, 41, 41).count() == cs.both);
        CHECK(allDistinct(planFor(true, true, cs.n, 41, 41)));
    }
}

TEST_CASE("Copy 0 is the identity for every plan") {
    for (const int n : kRotationCounts) {
        for (const bool x : {false, true}) {
            for (const bool y : {false, true}) {
                const SymmetryPlan plan = planFor(x, y, n, 25, 17, 11, 20);
                const Vec2f v{3.25f, -4.5f};
                CHECK(plan.point(0, v).x == v.x);
                CHECK(plan.point(0, v).y == v.y);
                CHECK(plan.exact(0));
                for (int j = -3; j < 20; ++j)
                    for (int i = -3; i < 28; ++i)
                        CHECK(plan.pixel(0, i, j) == PixelPos{i, j});
                const IRect r{2, 3, 9, 5};
                CHECK(plan.rect(0, r) == r);
            }
        }
    }
    const SymmetryPlan odd = resolveSymmetry(setOf({mirrorAt(30), mirrorAt(45, 7, 9), rotateAt(3)}), 20, 20);
    CHECK(odd.pixel(0, 4, 7) == PixelPos{4, 7});
    CHECK(odd.rect(0, IRect{1, 2, 3, 4}) == IRect{1, 2, 3, 4});
}

// --- mirrors ---------------------------------------------------------------------------------------

TEST_CASE("Pixels mirror across an edge position and map to themselves through a center position") {
    const SymmetryPlan even = resolveSymmetry(setOf({mirrorAt(0), mirrorAt(90)}), 20, 20);
    CHECK(even.pixel(1, 0, 5) == PixelPos{19, 5});
    CHECK(even.pixel(1, 9, 5) == PixelPos{10, 5});
    CHECK(even.pixel(1, 10, 5) == PixelPos{9, 5});
    CHECK(even.pixel(2, 4, 3) == PixelPos{4, 16});

    const SymmetryPlan odd = resolveSymmetry(setOf({mirrorAt(0), mirrorAt(90)}), 21, 21);
    CHECK(odd.pixel(1, 10, 4) == PixelPos{10, 4}); // the middle pixel is on the line
    CHECK(odd.pixel(1, 9, 4) == PixelPos{11, 4});
    CHECK(odd.pixel(3, 0, 0) == PixelPos{20, 20});
}

TEST_CASE("Mirroring a pixel twice gives the pixel back, at every exact angle and position") {
    for (const int angle : {0, 45, 90, 135}) {
        for (const int pos : {0, 1, 7, 20, 41}) {
            // A diagonal line is only a pixel-to-pixel map when px + py is even (otherwise it sits half a pixel off the grid).
            const SymmetryPlan plan = resolveSymmetry(setOf({mirrorAt(angle, pos, (angle % 90 == 0 ? 41 : 40) - pos)}), 21, 21);
            REQUIRE(plan.count() == 2);
            for (int j = -3; j < 25; ++j)
                for (int i = -3; i < 25; ++i) {
                    const PixelPos m = plan.pixel(1, i, j);
                    CHECK(plan.pixel(1, m.x, m.y) == PixelPos{i, j});
                }
        }
    }
}

TEST_CASE("Points mirror continuously") {
    const SymmetryPlan plan = resolveSymmetry(setOf({mirrorAt(0), mirrorAt(90)}), 20, 10);
    const Vec2f v{3.25f, 2.5f};
    CHECK(plan.point(0, v).x == 3.25f);
    CHECK(plan.point(1, v).x == doctest::Approx(16.75f)); // 20 - 3.25
    CHECK(plan.point(1, v).y == 2.5f);
    CHECK(plan.point(2, v).y == doctest::Approx(7.5f));
    CHECK(plan.point(3, v).x == doctest::Approx(16.75f));
    CHECK(plan.point(3, v).y == doctest::Approx(7.5f));
    // The center of a pixel maps to the center of its mirror pixel.
    CHECK(plan.point(1, Vec2f{0.5f, 0.5f}).x == 19.5f);
}

TEST_CASE("A half-pixel position through a pixel keeps that pixel in place") {
    const SymmetryPlan plan = resolveSymmetry(setOf({mirrorAt(0, 13)}), 20, 20);
    CHECK(plan.pixel(1, 6, 0) == PixelPos{6, 0});
    CHECK(plan.pixel(1, 5, 0) == PixelPos{7, 0});
    CHECK(plan.rect(1, IRect{6, 0, 7, 1}) == IRect{6, 0, 7, 1});
    CHECK(plan.point(1, Vec2f{6.5f, 1.0f}).x == 6.5f);
}

TEST_CASE("Mirrors at 45 and 135 degrees reflect across the diagonals, exactly") {
    // 45: the flip direction is (1, 1), so the line is the anti-diagonal through the point: (x, y) -> (20 - y, 20 - x)
    // through the center (10, 10).
    const SymmetryPlan anti = resolveSymmetry(setOf({mirrorAt(45)}), 20, 20);
    REQUIRE(anti.count() == 2);
    CHECK(anti.exact(1));
    CHECK(anti.pixel(1, 3, 4) == PixelPos{15, 16});
    CHECK(anti.point(1, Vec2f{3.5f, 4.5f}).x == doctest::Approx(15.5f));
    CHECK(anti.point(1, Vec2f{3.5f, 4.5f}).y == doctest::Approx(16.5f));
    // 135: the main diagonal through the center: (x, y) -> (y, x).
    const SymmetryPlan diag = resolveSymmetry(setOf({mirrorAt(135)}), 20, 20);
    CHECK(diag.exact(1));
    CHECK(diag.pixel(1, 3, 4) == PixelPos{4, 3});
    CHECK(diag.pixel(1, 5, 5) == PixelPos{5, 5}); // on the line
    // Off-center: the 135 line through (7, 4) is y = x - 3, so (x, y) -> (y + 3, x - 3).
    const SymmetryPlan moved = resolveSymmetry(setOf({mirrorAt(135, 14, 8)}), 20, 20);
    CHECK(moved.pixel(1, 3, 4) == PixelPos{7, 0});
    CHECK(moved.pixel(1, 10, 7) == PixelPos{10, 7}); // on the line
    // The 45 line through (7, 4) has x + y = 11: (x, y) -> (11 - y, 11 - x) on centers, (10 - y, 10 - x) on pixels.
    const SymmetryPlan antiMoved = resolveSymmetry(setOf({mirrorAt(45, 14, 8)}), 20, 20);
    CHECK(antiMoved.pixel(1, 3, 4) == PixelPos{6, 7});
}

TEST_CASE("A rectangle mirrors to exactly the mirror of its pixels, at every exact angle") {
    const IRect r{1, 2, 6, 5};
    const SymmetrySet sets[] = {
        setOf({mirrorAt(0, 13), mirrorAt(90, 13, 9)}),  // a line through pixel 6, one on an edge
        setOf({mirrorAt(45)}),
        setOf({mirrorAt(135)}),
        setOf({mirrorAt(45, 13, 9)}),                   // x + y odd in half pixels: the box lands off the pixel grid's parity
        setOf({mirrorAt(135, 13, 8)}),
        setOf({mirrorAt(0, 7), mirrorAt(135, 14, 8)}),
    };
    for (const SymmetrySet& set : sets) {
        const SymmetryPlan plan = resolveSymmetry(set, 20, 20);
        for (std::size_t k = 0; k < plan.count(); ++k) {
            INFO("copy " << k);
            const IRect m = plan.rect(k, r);
            CHECK(m.x0 < m.x1);
            CHECK(m.y0 < m.y1);
            CHECK(imagesOf(plan, k, r) == coveredBy(m));
        }
    }
}

TEST_CASE("Rectangles mirror edge to edge and stay normalized") {
    const SymmetryPlan plan = resolveSymmetry(setOf({mirrorAt(0), mirrorAt(90)}), 20, 12);
    const IRect r{2, 3, 7, 5};
    CHECK(plan.rect(0, r) == r);
    CHECK(plan.rect(1, r) == IRect{13, 3, 18, 5});
    CHECK(plan.rect(2, r) == IRect{2, 7, 7, 9});
    CHECK(plan.rect(3, r) == IRect{13, 7, 18, 9});
    // A diagonal swaps width and height.
    const SymmetryPlan diag = resolveSymmetry(setOf({mirrorAt(135)}), 20, 20);
    CHECK(diag.rect(1, r) == IRect{3, 2, 5, 7});
}

TEST_CASE("A mirror at an odd angle is a true reflection that the pixels only approximate") {
    for (const int angle : {1, 17, 30, 60, 100, 160}) {
        const SymmetryPlan plan = resolveSymmetry(setOf({mirrorAt(angle, 27, 19)}), 40, 40);
        REQUIRE(plan.count() == 2);
        CHECK_FALSE(plan.exact(1));
        const double a = angle * kPi / 180.0;
        const double nx = std::cos(a);
        const double ny = std::sin(a);
        const double px = 13.5; // half pixels 27 and 19
        const double py = 9.5;
        for (const Vec2f v : {Vec2f{3.0f, 4.0f}, Vec2f{20.5f, 7.25f}, Vec2f{13.5f, 9.5f}, Vec2f{30.0f, 30.0f}}) {
            const Vec2f m = plan.point(1, v);
            // The segment between a point and its image crosses the line at right angles (it runs along n) and is
            // bisected by it.
            const double dx = m.x - v.x;
            const double dy = m.y - v.y;
            CHECK(std::abs(dx * ny - dy * nx) < 1e-3);
            const double mid = ((v.x + m.x) * 0.5 - px) * nx + ((v.y + m.y) * 0.5 - py) * ny;
            CHECK(std::abs(mid) < 1e-3);
            // And it is an involution.
            const Vec2f back = plan.point(1, m);
            CHECK(std::abs(back.x - v.x) < 1e-3);
            CHECK(std::abs(back.y - v.y) < 1e-3);
        }
        // A pixel is within a pixel of its true image.
        for (int j = 5; j < 35; j += 4)
            for (int i = 5; i < 35; i += 4) {
                const PixelPos p = plan.pixel(1, i, j);
                const Vec2f t = plan.point(1, Vec2f{static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f});
                CHECK(std::abs(p.x + 0.5 - t.x) <= 0.5 + 1e-3);
                CHECK(std::abs(p.y + 0.5 - t.y) <= 0.5 + 1e-3);
            }
        // Rectangles keep their size around the mapped center.
        const IRect box{4, 6, 10, 8};
        CHECK(plan.rect(1, box).width() == 6);
        CHECK(plan.rect(1, box).height() == 2);
    }
}

TEST_CASE("Mirrored values far off the canvas stay in range") {
    const SymmetryPlan plan = resolveSymmetry(setOf({mirrorAt(0), mirrorAt(90)}), 1024, 1024);
    CHECK(plan.pixel(1, 1000000, 0).x == 1024 - 1000000 - 1);
    CHECK(plan.pixel(2, 0, -1000000).y == 1024 + 1000000 - 1);
    const IRect m = plan.rect(3, IRect{-1000000, -1000000, 1000000, 1000000});
    CHECK(m.x0 == 1024 - 1000000);
    CHECK(m.x1 == 1024 + 1000000);
}

// --- rotation --------------------------------------------------------------------------------------

TEST_CASE("Rotation alone makes a plan active, with or without mirrors") {
    for (const int n : {2, 3, 4, 6, 8}) {
        CHECK(planFor(false, false, n).active());
        CHECK(planFor(true, false, n).active());
        CHECK(planFor(false, false, n).count() == static_cast<std::size_t>(n));
    }
    // An invalid count in a list is read as 4, not skipped.
    SymmetrySet set;
    set.items[0] = rotateAt(4);
    set.items[0].count = 5;
    set.count = 1;
    CHECK(resolveSymmetry(set, 20, 20).count() == 4);
}

TEST_CASE("Exact copies are the turns that are multiples of 90 degrees") {
    CHECK(planFor(false, false, 1).exact(0));

    const SymmetryPlan two = planFor(false, false, 2);
    CHECK(two.exact(0));
    CHECK(two.exact(1));

    const SymmetryPlan three = planFor(false, false, 3);
    CHECK(three.exact(0));
    CHECK_FALSE(three.exact(1));
    CHECK_FALSE(three.exact(2));

    const SymmetryPlan four = planFor(false, false, 4);
    for (std::size_t k = 0; k < 4; ++k)
        CHECK(four.exact(k));

    const SymmetryPlan six = planFor(false, false, 6);
    for (std::size_t k = 0; k < 6; ++k)
        CHECK(six.exact(k) == (k == 0 || k == 3));

    const SymmetryPlan eight = planFor(false, false, 8);
    for (std::size_t k = 0; k < 8; ++k)
        CHECK(eight.exact(k) == (k % 2 == 0));

    // A mirror first does not change which turns are quarters: the copies after the first block are turns j,
    // two copies each.
    const SymmetryPlan mirrored = planFor(true, false, 8);
    for (std::size_t k = 0; k < mirrored.count(); ++k)
        CHECK(mirrored.exact(k) == ((k / 2) % 2 == 0));
}

TEST_CASE("Quarter turns map the four pixels around a canvas center exactly") {
    // 20x20: the center (10, 10) is a pixel corner. Clockwise on screen, the top left pixel of the middle 2x2
    // block goes to the top right, then bottom right, then bottom left.
    const SymmetryPlan even = planFor(false, false, 4, 20, 20);
    const PixelPos ring[4] = {{9, 9}, {10, 9}, {10, 10}, {9, 10}};
    for (std::size_t k = 0; k < 4; ++k)
        for (std::size_t i = 0; i < 4; ++i)
            CHECK(even.pixel(k, ring[i].x, ring[i].y) == ring[(i + k) % 4]);

    // 21x21: the center is the middle of pixel (10, 10), which stays; its left neighbour goes to the top.
    const SymmetryPlan odd = planFor(false, false, 4, 21, 21);
    for (std::size_t k = 0; k < 4; ++k)
        CHECK(odd.pixel(k, 10, 10) == PixelPos{10, 10});
    const PixelPos cross[4] = {{9, 10}, {10, 9}, {11, 10}, {10, 11}}; // left, top, right, bottom
    for (std::size_t k = 0; k < 4; ++k)
        for (std::size_t i = 0; i < 4; ++i)
            CHECK(odd.pixel(k, cross[i].x, cross[i].y) == cross[(i + k) % 4]);

    // 2-fold is the half turn.
    const SymmetryPlan two = planFor(false, false, 2, 20, 20);
    CHECK(two.pixel(1, 3, 4) == PixelPos{16, 15});
    CHECK(two.pixel(1, 9, 9) == PixelPos{10, 10});
    const SymmetryPlan twoOdd = planFor(false, false, 2, 21, 21);
    CHECK(twoOdd.pixel(1, 3, 4) == PixelPos{17, 16});
    CHECK(twoOdd.pixel(1, 10, 10) == PixelPos{10, 10});

    // A custom center keeps the same rule: positions (14, 8) put the center at (7, 4).
    const SymmetryPlan custom = planFor(false, false, 4, 20, 20, 14, 8);
    CHECK(custom.pixel(1, 3, 4) == PixelPos{6, 0});
    CHECK(custom.pixel(2, 3, 4) == PixelPos{10, 3});
    CHECK(custom.pixel(3, 3, 4) == PixelPos{7, 7});
}

TEST_CASE("Quarter turns about a center that is a corner in one axis and a middle in the other still move whole pixels") {
    // x and y of different parity: the center (10, 10.5) is on a pixel edge in x and in the middle of a pixel
    // in y. A quarter turn then lands on pixel edges and rounds toward the larger index, a half pixel off,
    // but consistently: it stays a one-to-one map of the pixel grid.
    const SymmetryPlan plan = planFor(false, false, 4, 20, 21);
    CHECK(plan.pixel(1, 9, 9) == PixelPos{11, 10});
    for (std::size_t k = 1; k < 4; ++k) {
        CHECK(imagesOf(plan, k, IRect{0, 0, 6, 6}).size() == 36);
        // Neighbours stay neighbours.
        const PixelPos a = plan.pixel(k, 4, 4);
        const PixelPos b = plan.pixel(k, 5, 4);
        CHECK(std::abs(a.x - b.x) + std::abs(a.y - b.y) == 1);
    }
    // The half turn maps (x, y) to (px - x, py - y) rounded: here (20 - 3 - 1 + 0.5 -> 16, 21 - 4 - 1 + 0.5 -> 17).
    const SymmetryPlan half = planFor(false, false, 2, 20, 21);
    CHECK(half.pixel(1, 3, 4) == PixelPos{16, 16});
}

TEST_CASE("Exact copies of a pixel are one-to-one on the canvas for every rotation count") {
    for (const int n : {2, 4, 6, 8}) {
        for (const std::pair<int, int>& size : {std::pair<int, int>{20, 20}, std::pair<int, int>{21, 21}}) {
            const SymmetryPlan plan = planFor(true, false, n, size.first, size.second);
            for (std::size_t k = 0; k < plan.count(); ++k) {
                if (!plan.exact(k))
                    continue;
                CHECK(imagesOf(plan, k, IRect{0, 0, 12, 12}).size() == 144);
            }
        }
    }
}

TEST_CASE("A copy a quarter turn further along is that copy turned a quarter, to the pixel") {
    // A lone 8-fold rotation: turn t + 2 is turn t moved by turn 2; 6-fold: t + 3 by turn 3. This is what makes a
    // whole picture invariant under the quarter turns even though 45 and 60 degree copies are rounded.
    const struct {
        int n;
        std::size_t step;
    } groups[] = {{8, 2}, {6, 3}, {4, 1}, {2, 1}};
    for (const auto& g : groups) {
        const SymmetryPlan plan = planFor(false, false, g.n, 41, 41);
        for (std::size_t t = 0; t + g.step < static_cast<std::size_t>(g.n); ++t)
            for (int y = 0; y < 41; y += 3)
                for (int x = 0; x < 41; x += 3) {
                    const PixelPos a = plan.pixel(t, x, y);
                    CHECK(plan.pixel(t + g.step, x, y) == plan.pixel(g.step, a.x, a.y));
                }
    }
    // The same when a mirror is listed first (turns apply to the mirrored copies too): copies 2j and 2j + 1.
    const SymmetryPlan plan = planFor(true, false, 8, 41, 41);
    for (std::size_t k = 2; k + 4 < plan.count(); ++k)
        for (int y = 0; y < 41; y += 4)
            for (int x = 0; x < 41; x += 4) {
                const PixelPos a = plan.pixel(k, x, y);
                CHECK(plan.pixel(k + 4, x, y) == plan.pixel(4, a.x, a.y)); // 4 is the first quarter turn of the plain copy
            }
}

TEST_CASE("Rectangles swap width and height under quarter turns") {
    const SymmetryPlan plan = planFor(false, false, 4, 20, 20);
    const IRect r{2, 3, 7, 5}; // 5 wide, 2 tall
    CHECK(plan.rect(0, r) == r);
    CHECK(plan.rect(1, r) == IRect{15, 2, 17, 7});
    CHECK(plan.rect(2, r) == IRect{13, 15, 18, 17});
    CHECK(plan.rect(3, r) == IRect{3, 13, 5, 18});
    for (std::size_t k = 0; k < 4; ++k) {
        const IRect m = plan.rect(k, r);
        CHECK(m.x0 < m.x1);
        CHECK(m.y0 < m.y1);
        CHECK(m.width() == (k % 2 == 0 ? 5 : 2));
        CHECK(m.height() == (k % 2 == 0 ? 2 : 5));
    }
}

TEST_CASE("An exact rectangle is exactly the moved pixels, for every parity of the positions") {
    const IRect r{2, 3, 9, 6};
    for (const int n : {2, 4, 8}) {
        for (const std::pair<int, int>& axes : {std::pair<int, int>{20, 20}, std::pair<int, int>{21, 21},
                                               std::pair<int, int>{20, 21}, std::pair<int, int>{13, 8}}) {
            for (const bool mirrorX : {false, true}) {
                const SymmetryPlan plan = planFor(mirrorX, mirrorX, n, 20, 20, axes.first, axes.second);
                for (std::size_t k = 0; k < plan.count(); ++k) {
                    if (!plan.exact(k))
                        continue;
                    CHECK(imagesOf(plan, k, r) == coveredBy(plan.rect(k, r)));
                }
            }
        }
    }
}

TEST_CASE("Rectangles keep their size at other angles and move around the center") {
    const SymmetryPlan three = planFor(false, false, 3, 20, 20);
    const IRect r{12, 8, 16, 12}; // 4x4, its center (14, 10) is 4 right of the canvas center (10, 10)
    for (std::size_t k = 0; k < 3; ++k) {
        const IRect m = three.rect(k, r);
        CHECK(m.width() == 4);
        CHECK(m.height() == 4);
        const double angle = 2.0 * kPi * static_cast<double>(k) / 3.0;
        const double ex = 10.0 + 4.0 * std::cos(angle);
        const double ey = 10.0 + 4.0 * std::sin(angle);
        CHECK(std::abs((m.x0 + m.x1) * 0.5 - ex) <= 0.5);
        CHECK(std::abs((m.y0 + m.y1) * 0.5 - ey) <= 0.5);
    }
    CHECK(three.rect(1, r) == IRect{6, 11, 10, 15});

    // 8 rotations: the 45 degree copy keeps the 6x2 size; the 135 degree copy is that box turned a quarter, so
    // 2x6; the quarter-turn copies swap exactly.
    const SymmetryPlan eight = planFor(false, false, 8, 40, 40);
    const IRect wide{26, 18, 32, 20};
    CHECK(eight.rect(1, wide).width() == 6);
    CHECK(eight.rect(1, wide).height() == 2);
    CHECK(eight.rect(2, wide).width() == 2);
    CHECK(eight.rect(2, wide).height() == 6);
    CHECK(eight.rect(3, wide).width() == 2);
    CHECK(eight.rect(3, wide).height() == 6);
    CHECK(eight.rect(4, wide).width() == 6);
    CHECK(eight.rect(5, wide).width() == 6);
    CHECK(eight.rect(7, wide).width() == 2);
    // A rectangle on a mixed plan stays normalized and keeps its area.
    const SymmetryPlan mixed = planFor(true, true, 6, 40, 40, 33, 41);
    for (std::size_t k = 0; k < mixed.count(); ++k) {
        const IRect m = mixed.rect(k, wide);
        CHECK(m.x0 < m.x1);
        CHECK(m.y0 < m.y1);
        CHECK(m.width() * m.height() == 12);
    }
}

TEST_CASE("Points rotate about the center by j * 360 / n degrees, clockwise on screen") {
    for (const int n : {2, 3, 4, 6, 8}) {
        for (const std::pair<int, int>& axes : {std::pair<int, int>{kSymmetryCenter, kSymmetryCenter},
                                               std::pair<int, int>{13, 9}, std::pair<int, int>{14, 8}}) {
            const SymmetryPlan plan = planFor(false, false, n, 30, 20, axes.first, axes.second);
            const double cx = (axes.first < 0 ? 30 : axes.first) * 0.5;
            const double cy = (axes.second < 0 ? 20 : axes.second) * 0.5;
            const Vec2f v{static_cast<float>(cx) + 7.0f, static_cast<float>(cy) - 2.5f};
            const double radius = std::hypot(v.x - cx, v.y - cy);
            const double base = std::atan2(v.y - cy, v.x - cx);
            for (std::size_t k = 0; k < plan.count(); ++k) {
                const Vec2f p = plan.point(k, v);
                CHECK(std::hypot(p.x - cx, p.y - cy) == doctest::Approx(radius).epsilon(1e-5));
                const double expected = base + 2.0 * kPi * static_cast<double>(k) / n;
                CHECK(p.x == doctest::Approx(cx + radius * std::cos(expected)).epsilon(1e-4).scale(1.0));
                CHECK(p.y == doctest::Approx(cy + radius * std::sin(expected)).epsilon(1e-4).scale(1.0));
            }
        }
    }
}

TEST_CASE("A turn after a mirror turns the mirrored copy") {
    // Mirror X, then Rotate 3, on a 30x20 canvas: the copies after the first block are the turned originals and
    // the turned mirror images, two at a time.
    const SymmetryPlan plan = planFor(true, false, 3, 30, 20);
    const double cx = 15.0;
    const double cy = 10.0;
    const Vec2f v{20.0f, 7.0f};
    const Vec2f mirrored{static_cast<float>(2 * cx) - v.x, v.y}; // across x = 15
    for (std::size_t turn = 0; turn < 3; ++turn) {
        const double angle = 2.0 * kPi * static_cast<double>(turn) / 3.0;
        for (std::size_t m = 0; m < 2; ++m) {
            const Vec2f src = m == 0 ? v : mirrored;
            const double dx = src.x - cx;
            const double dy = src.y - cy;
            const Vec2f p = plan.point(turn * 2 + m, v);
            CHECK(p.x == doctest::Approx(cx + dx * std::cos(angle) - dy * std::sin(angle)).epsilon(1e-4).scale(1.0));
            CHECK(p.y == doctest::Approx(cy + dx * std::sin(angle) + dy * std::cos(angle)).epsilon(1e-4).scale(1.0));
        }
    }
}

TEST_CASE("A turn before a mirror mirrors the turned copy") {
    // Rotate 3, then Mirror X: copy 3 + j is the mirror image of copy j.
    const SymmetryPlan plan = resolveSymmetry(setOf({rotateAt(3), mirrorAt(0)}), 30, 20);
    REQUIRE(plan.count() == 6);
    const Vec2f v{20.0f, 7.0f};
    for (std::size_t j = 0; j < 3; ++j) {
        const Vec2f turned = plan.point(j, v);
        const Vec2f image = plan.point(3 + j, v);
        CHECK(image.x == doctest::Approx(30.0f - turned.x).epsilon(1e-4).scale(1.0));
        CHECK(image.y == doctest::Approx(turned.y).epsilon(1e-4).scale(1.0));
    }
}

TEST_CASE("Points at 3, 6 and 8 rotations stay on a circle around the center and are evenly spaced") {
    for (const int n : {3, 6, 8}) {
        const SymmetryPlan plan = planFor(false, false, n, 41, 41);
        const double c = 20.5;
        const Vec2f v{static_cast<float>(c) + 10.0f, static_cast<float>(c)};
        std::vector<double> angles;
        for (std::size_t k = 0; k < plan.count(); ++k) {
            const Vec2f p = plan.point(k, v);
            CHECK(std::hypot(p.x - c, p.y - c) == doctest::Approx(10.0).epsilon(1e-5));
            angles.push_back(std::atan2(p.y - c, p.x - c));
        }
        for (std::size_t k = 0; k < angles.size(); ++k) {
            double step = angles[(k + 1) % angles.size()] - angles[k];
            if (step <= 0.0)
                step += 2.0 * kPi;
            CHECK(step == doctest::Approx(2.0 * kPi / n).epsilon(1e-4));
        }
    }
}

TEST_CASE("Rotated pixels at 3, 6 and 8 rotations stay within a pixel of the true rotation") {
    // Rounded once at the base angle and then moved by exact quarter turns: the pixel containing the turned
    // center, or its neighbour when a quarter turn is applied to the rounded pixel (at most half a pixel more).
    for (const int n : {3, 6, 8}) {
        const SymmetryPlan plan = planFor(false, false, n, 41, 41);
        for (int j = 5; j < 36; j += 3)
            for (int i = 5; i < 36; i += 3)
                for (std::size_t k = 0; k < plan.count(); ++k) {
                    const PixelPos p = plan.pixel(k, i, j);
                    const Vec2f t = plan.point(k, Vec2f{static_cast<float>(i) + 0.5f, static_cast<float>(j) + 0.5f});
                    CHECK(std::abs(static_cast<double>(p.x) + 0.5 - t.x) <= 0.5 + 1e-3);
                    CHECK(std::abs(static_cast<double>(p.y) + 0.5 - t.y) <= 0.5 + 1e-3);
                }
    }
}

TEST_CASE("Mirror and turn compose: the mirrors already contain the half turn") {
    const SymmetryPlan plan = planFor(true, true, 4, 41, 41);
    // The group has 8 elements (the square's), not 4 x 4: the copies that coincide are dropped.
    CHECK(plan.count() == 8);
    CHECK(imagesOf(plan, 0, IRect{28, 17, 29, 18}).size() == 1);
    std::set<std::pair<int, int>> images;
    for (std::size_t k = 0; k < plan.count(); ++k) {
        const PixelPos p = plan.pixel(k, 28, 17);
        images.insert({p.x, p.y});
    }
    CHECK(images.size() == 8);
}

TEST_CASE("Rotated values stay finite and in range for the largest coordinates tools pass") {
    const int big = 1000000 + 2 * 200;
    for (const int n : kRotationCounts) {
        for (const bool mirrors : {false, true}) {
            const SymmetryPlan plan = planFor(mirrors, mirrors, n, 16384, 16384, 32768, 32767);
            for (std::size_t k = 0; k < plan.count(); ++k) {
                for (const int i : {-big, -1, 0, 1, big}) {
                    for (const int j : {-big, 0, big}) {
                        const PixelPos p = plan.pixel(k, i, j);
                        CHECK(std::abs(static_cast<long long>(p.x)) < 10000000);
                        CHECK(std::abs(static_cast<long long>(p.y)) < 10000000);
                        const Vec2f v = plan.point(k, Vec2f{static_cast<float>(i), static_cast<float>(j)});
                        CHECK(std::isfinite(v.x));
                        CHECK(std::isfinite(v.y));
                    }
                }
                const IRect r = plan.rect(k, IRect{-big, -big, big, big});
                CHECK(r.x0 < r.x1);
                CHECK(r.y0 < r.y1);
                CHECK(std::abs(static_cast<long long>(r.x0)) < 10000000);
                CHECK(std::abs(static_cast<long long>(r.y1)) < 10000000);
            }
        }
    }
}

TEST_CASE("Mapped pixels are clamped, not overflowed, for absurd input") {
    const int extreme[] = {std::numeric_limits<int>::min(), std::numeric_limits<int>::min() / 2, -1,
                           std::numeric_limits<int>::max() / 2, std::numeric_limits<int>::max()};
    const SymmetrySet sets[] = {
        legacySet(true, true, 8, kSymmetryCenter, kSymmetryCenter),
        legacySet(false, false, 3, kSymmetryCenter, kSymmetryCenter),
        setOf({mirrorAt(45), mirrorAt(135, 7, 9), mirrorAt(33), rotateAt(6, 3, 5)}),
    };
    for (const SymmetrySet& set : sets) {
        const SymmetryPlan plan = resolveSymmetry(set, 64, 64);
        for (std::size_t k = 0; k < plan.count(); ++k)
            for (const int i : extreme)
                for (const int j : extreme) {
                    const PixelPos p = plan.pixel(k, i, j);
                    CHECK(std::abs(static_cast<long long>(p.x)) <= 1000000000LL);
                    CHECK(std::abs(static_cast<long long>(p.y)) <= 1000000000LL);
                    const IRect r = plan.rect(k, IRect{i, j, i, j});
                    CHECK(std::abs(static_cast<long long>(r.x0)) <= 1000000000LL);
                    CHECK(std::abs(static_cast<long long>(r.y1)) <= 1000000000LL);
                }
    }
}

TEST_CASE("Hostile item fields still give a valid plan") {
    SymmetrySet set;
    set.count = kMaxSymmetryItems;
    for (int i = 0; i < kMaxSymmetryItems; ++i) {
        SymmetryItem& it = set.items[static_cast<std::size_t>(i)];
        it.kind = i % 2 == 0 ? SymmetryKind::Mirror : SymmetryKind::Rotate;
        it.posX = i % 3 == 0 ? std::numeric_limits<int>::max() : std::numeric_limits<int>::min();
        it.posY = i % 3 == 1 ? std::numeric_limits<int>::max() : -7;
        it.angle = i % 2 == 0 ? std::numeric_limits<int>::min() + i : 12345;
        it.count = 99;
    }
    const SymmetryPlan plan = resolveSymmetry(set, 33, 17);
    CHECK(plan.count() >= 1);
    CHECK(plan.count() <= kMaxSymmetryCopies);
    CHECK(plan.pixel(0, 3, 4) == PixelPos{3, 4});
    for (std::size_t k = 0; k < plan.count(); ++k) {
        const Vec2f v = plan.point(k, Vec2f{5.5f, 6.5f});
        CHECK(std::isfinite(v.x));
        CHECK(std::isfinite(v.y));
        CHECK(std::abs(static_cast<long long>(plan.pixel(k, 5, 6).x)) < 100000);
    }
    set.count = 1000; // a corrupt count is clamped to the array
    CHECK(resolveSymmetry(set, 33, 17).count() >= 1);
    set.count = -1;
    CHECK(resolveSymmetry(set, 33, 17).count() == 1);
}

// --- positions as numbers --------------------------------------------------------------------------

TEST_CASE("resolveAxis gives the middle for a negative position and clamps the rest onto the canvas") {
    CHECK(resolveAxis(kSymmetryCenter, 20) == 20);
    CHECK(resolveAxis(-9, 21) == 21);
    CHECK(resolveAxis(0, 20) == 0);
    CHECK(resolveAxis(13, 20) == 13);
    CHECK(resolveAxis(40, 20) == 40);
    CHECK(resolveAxis(41, 20) == 40);
    CHECK(resolveAxis(1000000, 20) == 40);
    CHECK(resolveAxis(kSymmetryCenter, 0) == 0);
    CHECK(resolveAxis(5, 0) == 0);
    CHECK(resolveAxis(0, 1) == 0);
    CHECK(resolveAxis(3, 1) == 2);
}

TEST_CASE("axisToPixels and axisToPercent describe a stored axis") {
    CHECK(axisToPixels(kSymmetryCenter, 20) == 10.0);
    CHECK(axisToPixels(kSymmetryCenter, 21) == 10.5);
    CHECK(axisToPixels(0, 20) == 0.0);
    CHECK(axisToPixels(7, 20) == 3.5);
    CHECK(axisToPixels(40, 20) == 20.0);
    CHECK(axisToPixels(999, 20) == 20.0); // clamped onto the canvas
    CHECK(axisToPixels(kSymmetryCenter, 0) == 0.0);
    CHECK(axisToPixels(5, 0) == 0.0);

    CHECK(axisToPercent(kSymmetryCenter, 20) == 50.0);
    CHECK(axisToPercent(kSymmetryCenter, 21) == 50.0);
    CHECK(axisToPercent(0, 20) == 0.0);
    CHECK(axisToPercent(40, 20) == 100.0);
    CHECK(axisToPercent(999, 20) == 100.0);
    CHECK(axisToPercent(20, 40) == 25.0);
    CHECK(axisToPercent(10, 20) == 25.0);
    CHECK(axisToPercent(kSymmetryCenter, 0) == 50.0);
    CHECK(axisToPercent(3, 0) == 50.0); // no canvas side: the middle
    CHECK(axisToPercent(3, -4) == 50.0);
}

TEST_CASE("axisFromPixels rounds to the nearest half pixel and clamps onto the canvas") {
    CHECK(axisFromPixels(10.0, 20) == 20);
    CHECK(axisFromPixels(3.5, 20) == 7);
    CHECK(axisFromPixels(0.0, 20) == 0);
    CHECK(axisFromPixels(20.0, 20) == 40);
    CHECK(axisFromPixels(3.74, 20) == 7); // 7.48 half pixels
    CHECK(axisFromPixels(3.76, 20) == 8); // 7.52 half pixels
    CHECK(axisFromPixels(3.25, 20) == 7); // exactly between 3.0 and 3.5: rounds up
    CHECK(axisFromPixels(3.0, 20) == 6);
    CHECK(axisFromPixels(0.2, 20) == 0);
    CHECK(axisFromPixels(0.3, 20) == 1);
    CHECK(axisFromPixels(-4.0, 20) == 0);
    CHECK(axisFromPixels(-1.0e300, 20) == 0);
    CHECK(axisFromPixels(21.0, 20) == 40);
    CHECK(axisFromPixels(1.0e300, 20) == 40);
    CHECK(axisFromPixels(10.0, 1) == 2);
    CHECK(axisFromPixels(0.4, 1) == 1);
}

TEST_CASE("axisFromPixels and axisFromPercent give the center for NaN, infinity and an empty canvas") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const double v : {nan, inf, -inf}) {
        CHECK(axisFromPixels(v, 20) == kSymmetryCenter);
        CHECK(axisFromPercent(v, 20) == kSymmetryCenter);
    }
    CHECK(axisFromPixels(5.0, 0) == kSymmetryCenter);
    CHECK(axisFromPixels(5.0, -3) == kSymmetryCenter);
    CHECK(axisFromPercent(50.0, 0) == kSymmetryCenter);
    CHECK(axisFromPercent(50.0, -3) == kSymmetryCenter);
}

TEST_CASE("axisFromPercent maps 0, 50 and 100 to the edges and the middle") {
    CHECK(axisFromPercent(0.0, 20) == 0);
    CHECK(axisFromPercent(50.0, 20) == 20);
    CHECK(axisFromPercent(100.0, 20) == 40);
    CHECK(axisFromPercent(50.0, 21) == 21); // the middle of pixel 10
    CHECK(axisFromPercent(25.0, 40) == 20);
    CHECK(axisFromPercent(-10.0, 20) == 0);
    CHECK(axisFromPercent(250.0, 20) == 40);
    CHECK(axisFromPercent(1.0e300, 20) == 40);
    CHECK(axisFromPercent(-1.0e300, 20) == 0);
    CHECK(axisFromPercent(50.0, 1) == 1);
    // Rounds to the nearest half pixel: 33% of 20 pixels is 6.6, nearest half pixel 6.5.
    CHECK(axisFromPercent(33.0, 20) == 13);
}

TEST_CASE("Axis positions survive a round trip through pixels and through percent") {
    for (int size = 1; size <= 70; ++size) {
        for (int axis = 0; axis <= 2 * size; ++axis) {
            CHECK(axisFromPixels(axisToPixels(axis, size), size) == axis);
            CHECK(axisFromPercent(axisToPercent(axis, size), size) == axis);
        }
        // The center stays the middle.
        CHECK(axisFromPixels(axisToPixels(kSymmetryCenter, size), size) == size);
        CHECK(axisFromPercent(axisToPercent(kSymmetryCenter, size), size) == size);
    }
}

// --- item names ---------------------------------------------------------------------------------------

namespace {
std::string nameOf(std::string_view text) {
    const SymmetryName n = cleanSymmetryName(text);
    return std::string(symmetryNameView(n));
}
} // namespace

TEST_CASE("Custom names are cleaned: controls and invalid UTF-8 dropped, spaces trimmed") {
    CHECK(nameOf("Left") == "Left");
    CHECK(nameOf("") == "");
    CHECK(nameOf("   ") == "");
    CHECK(nameOf("  Left side  ") == "Left side");
    CHECK(nameOf("a\tb\nc\x01\x7F" "d") == "abcd");
    CHECK(nameOf("\xC2\x85x") == "x");              // a C1 control (U+0085)
    CHECK(nameOf("caf\xC3\xA9") == "caf\xC3\xA9"); // valid UTF-8 is kept
    CHECK(nameOf("a\x80z") == "az");                // stray continuation byte
    CHECK(nameOf("ab\xE2\x82") == "ab");            // truncated sequence at the end
    CHECK(nameOf("a\xE2z") == "az");                // truncated sequence in the middle
    CHECK(nameOf("a\xFFz") == "az");                // invalid lead byte
}

TEST_CASE("Custom names are cut at 32 bytes on a character boundary") {
    const std::string longAscii(80, 'x');
    CHECK(nameOf(longAscii) == std::string(kMaxSymmetryNameBytes, 'x'));

    // 31 ASCII bytes then a 2-byte character: it does not fit, so it is dropped whole.
    CHECK(nameOf(std::string(31, 'a') + "\xC3\xA9" + "tail") == std::string(31, 'a'));
    // 30 bytes then a 2-byte character fits exactly.
    CHECK(nameOf(std::string(30, 'a') + "\xC3\xA9" + "tail") == std::string(30, 'a') + "\xC3\xA9");
    // A row of 3-byte characters keeps whole ones only (10 x 3 = 30 bytes).
    std::string wide;
    for (int i = 0; i < 20; ++i)
        wide += "\xE2\x82\xAC";
    CHECK(nameOf(wide).size() == 30);
    // A cut that leaves a trailing space trims it.
    CHECK(nameOf(std::string(31, 'a') + " b") == std::string(31, 'a'));
}

TEST_CASE("Items stay trivially copyable and compare by name") {
    static_assert(std::is_trivially_copyable_v<SymmetryItem>);
    SymmetryItem a = makeMirrorX();
    const SymmetryItem b = makeMirrorX();
    CHECK(a == b);
    CHECK(symmetryNameView(a.name).empty());
    setSymmetryName(a, "Left");
    CHECK(symmetryNameView(a.name) == "Left");
    CHECK_FALSE(a == b);
    setSymmetryName(a, "");
    CHECK(a == b);
    CHECK(makeMirrorY().name == SymmetryName{});
    CHECK(makeRotate(3).name == SymmetryName{});
}

TEST_CASE("sanitizedItem cleans the name buffer") {
    SymmetryItem it = makeMirrorX();
    it.name.fill('z'); // no NUL at all: must still end up terminated and within the limit
    const SymmetryItem fixed = sanitizedItem(it);
    CHECK(symmetryNameView(fixed.name).size() == kMaxSymmetryNameBytes);
    CHECK(fixed.name.back() == '\0');

    SymmetryItem dirty = makeMirrorX();
    dirty.name = SymmetryName{'\t', 'h', 'i', '\0', 'j', 'u', 'n', 'k'};
    CHECK(symmetryNameView(sanitizedItem(dirty).name) == "hi");
    CHECK(sanitizedItem(dirty).name[4] == '\0'); // the tail after the NUL is cleared

    SymmetrySet set;
    SymmetryItem named = makeMirrorX();
    setSymmetryName(named, "Keep");
    CHECK(addSymmetryItem(set, named));
    CHECK(symmetryNameView(set.items[0].name) == "Keep");
}

TEST_CASE("Names stay with their item when another is removed") {
    SymmetrySet set;
    SymmetryItem a = makeMirrorX();
    setSymmetryName(a, "A");
    SymmetryItem b = makeMirrorY();
    setSymmetryName(b, "B");
    REQUIRE(addSymmetryItem(set, a));
    REQUIRE(addSymmetryItem(set, makeMirrorX()));
    REQUIRE(addSymmetryItem(set, b));
    CHECK(removeSymmetryItem(set, 1));
    CHECK(symmetryNameView(set.items[0].name) == "A");
    CHECK(symmetryNameView(set.items[1].name) == "B");
    CHECK(set.items[2] == SymmetryItem{});
}

TEST_CASE("symmetryOrdinal numbers items per kind and preset, whatever they are called") {
    SymmetrySet set;
    SymmetryItem first = makeMirrorX();
    setSymmetryName(first, "Left"); // a custom name does not change the numbering
    REQUIRE(addSymmetryItem(set, first));
    REQUIRE(addSymmetryItem(set, makeMirrorY()));
    REQUIRE(addSymmetryItem(set, makeMirrorX()));
    REQUIRE(addSymmetryItem(set, makeRotate(2)));
    REQUIRE(addSymmetryItem(set, makeMirrorY()));
    CHECK(symmetryOrdinal(set, 0) == 1);
    CHECK(symmetryOrdinal(set, 1) == 1);
    CHECK(symmetryOrdinal(set, 2) == 2);
    CHECK(symmetryOrdinal(set, 3) == 1);
    CHECK(symmetryOrdinal(set, 4) == 2);
    CHECK(symmetryOrdinal(set, -1) == 0);
    CHECK(symmetryOrdinal(set, 5) == 0);
}
