#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "core/guides.hpp"

using namespace trace;

namespace {

constexpr GuideAxis V = GuideAxis::Vertical;
constexpr GuideAxis H = GuideAxis::Horizontal;

Guides with(std::vector<int> vertical, std::vector<int> horizontal = {}) {
    Guides g;
    for (const int p : vertical)
        g.add(V, p);
    for (const int p : horizontal)
        g.add(H, p);
    return g;
}

} // namespace

TEST_CASE("Guides stay sorted and unique per axis") {
    Guides g;
    CHECK(g.empty());
    CHECK(g.add(V, 30));
    CHECK(g.add(V, 10));
    CHECK(g.add(V, 20));
    CHECK_FALSE(g.add(V, 20));
    CHECK(g.add(H, 20)); // the axes are independent
    CHECK(g.positions(V) == std::vector<int>{10, 20, 30});
    CHECK(g.positions(H) == std::vector<int>{20});
    CHECK(g.count() == 4);
}

TEST_CASE("Guides can sit on the image edges and outside it") {
    Guides g;
    CHECK(g.add(V, 0));
    CHECK(g.add(V, -3)); // range checks are the caller's business
    CHECK(g.positions(V) == std::vector<int>{-3, 0});
}

TEST_CASE("Guides are capped per axis") {
    Guides g;
    for (std::size_t i = 0; i < Guides::kMaxPerAxis; ++i)
        CHECK(g.add(V, static_cast<int>(i)));
    CHECK_FALSE(g.add(V, 100000));
    CHECK(g.add(H, 1)); // the other axis has its own budget
    CHECK(g.positions(V).size() == Guides::kMaxPerAxis);
}

TEST_CASE("Removing and clearing guides") {
    Guides g = with({5, 9}, {7});
    CHECK(g.remove(V, 5));
    CHECK_FALSE(g.remove(V, 5));
    CHECK_FALSE(g.remove(H, 9)); // wrong axis
    CHECK(g.positions(V) == std::vector<int>{9});
    g.clear();
    CHECK(g.empty());
}

TEST_CASE("Moving a guide onto another merges them") {
    Guides g = with({5, 9});
    CHECK(g.move(V, 5, 7));
    CHECK(g.positions(V) == std::vector<int>{7, 9});
    CHECK(g.move(V, 7, 9));
    CHECK(g.positions(V) == std::vector<int>{9});
    CHECK_FALSE(g.move(V, 1, 2));
    CHECK(g.positions(V) == std::vector<int>{9});
}

TEST_CASE("nearest finds the closest guide within the radius") {
    const Guides g = with({10, 20});
    CHECK(g.nearest(V, 11.0f, 2.0f) == 10);
    CHECK(g.nearest(V, 17.0f, 5.0f) == 20);
    CHECK_FALSE(g.nearest(V, 15.0f, 4.0f).has_value());
    CHECK(g.nearest(V, 10.0f, 0.0f) == 10); // exactly on it
    CHECK_FALSE(g.nearest(H, 10.0f, 5.0f).has_value());
    CHECK_FALSE(g.nearest(V, std::numeric_limits<float>::quiet_NaN(), 5.0f).has_value());
    CHECK_FALSE(g.nearest(V, 10.0f, -1.0f).has_value());
}

TEST_CASE("snapStart pulls the pointer onto a guide and leaves it alone otherwise") {
    const Guides g = with({10});
    CHECK(g.snapStart(V, 10.7f, 1.0f) == 10.0f);
    CHECK(g.snapStart(V, 9.2f, 1.0f) == 10.0f);
    CHECK(g.snapStart(V, 12.0f, 1.0f) == 12.0f);
    CHECK(g.snapStart(H, 10.2f, 1.0f) == 10.2f);
}

TEST_CASE("snapEnd puts the far edge of an inclusive box on the guide") {
    const Guides g = with({10});
    // Dragging right of an anchor at 4: the box's last pixel is 9, so its right edge is the guide.
    CHECK(g.snapEnd(V, 10.4f, 4.0f, 1.0f) == 9.5f);
    CHECK(g.snapEnd(V, 9.6f, 4.0f, 1.0f) == 9.5f);
    // Dragging left of an anchor at 20: the box's first pixel is 10, so its left edge is the guide.
    CHECK(g.snapEnd(V, 10.4f, 20.0f, 1.0f) == 10.5f);
    // No guide in reach.
    CHECK(g.snapEnd(V, 13.0f, 4.0f, 1.0f) == 13.0f);
}

TEST_CASE("snapEnd never lets the box invert past its anchor pixel") {
    const Guides g = with({10});
    // The anchor already snapped to the same guide (pixel 10); dragging right must not go to pixel 9.
    CHECK(g.snapEnd(V, 10.2f, 10.0f, 1.0f) == 10.5f);
}

TEST_CASE("snapEnd ignores non-finite anchors") {
    const Guides g = with({10});
    CHECK(g.snapEnd(V, 10.2f, std::numeric_limits<float>::quiet_NaN(), 1.0f) == 10.2f);
}

TEST_CASE("snapSpan lines up whichever edge is closer to a guide") {
    const Guides g = with({50});
    // A 20 px wide span: left edge at 49 snaps to 50.
    CHECK(g.snapSpan(V, 49, 20, 2.0f) == 50);
    // Right edge at 51 (start 31) snaps to 50, so the start becomes 30.
    CHECK(g.snapSpan(V, 31, 20, 2.0f) == 30);
    // Out of reach.
    CHECK(g.snapSpan(V, 10, 20, 2.0f) == 10);
    // A guide on the other axis does not matter.
    CHECK(g.snapSpan(H, 49, 20, 2.0f) == 49);
}

TEST_CASE("snapSpan picks the nearest of several guides") {
    const Guides g = with({50, 53});
    CHECK(g.snapSpan(V, 52, 10, 3.0f) == 53);
}

TEST_CASE("shift moves guides with the content and drops those that leave the canvas") {
    Guides g = with({0, 10, 50, 100}, {5, 60});
    g.shift(-20, 10, 80, 70);
    CHECK(g.positions(V) == std::vector<int>{30, 80}); // 10 -> -10 and 0 -> -20 are gone, 100 -> 80 stays
    CHECK(g.positions(H) == std::vector<int>{15, 70});
}

TEST_CASE("shift merges guides that land on each other and survives huge offsets") {
    Guides g = with({1, 2});
    g.shift(std::numeric_limits<int>::max(), 0, 10, 10);
    CHECK(g.empty());
    Guides h = with({5});
    h.shift(std::numeric_limits<int>::min(), 0, 10, 10);
    CHECK(h.empty());
}

TEST_CASE("scale follows a proportional resize") {
    Guides g = with({10, 50, 100}, {20});
    g.scale(100, 40, 200, 20);
    CHECK(g.positions(V) == std::vector<int>{20, 100, 200});
    CHECK(g.positions(H) == std::vector<int>{10});
}

TEST_CASE("scale merges guides that round to the same pixel and handles a zero source size") {
    Guides g = with({10, 11});
    g.scale(100, 100, 10, 10);
    CHECK(g.positions(V) == std::vector<int>{1});

    Guides z = with({5});
    z.scale(0, 0, 10, 10); // nothing to scale from: positions stay, then get range-checked
    CHECK(z.positions(V) == std::vector<int>{5});
}
