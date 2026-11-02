#include <doctest/doctest.h>

#include <vector>

#include "core/board_growth.hpp"
#include "core/document.hpp"
#include "core/raster.hpp"

using namespace trace;

namespace {

constexpr Rgba8 kRed{255, 0, 0, 255};

Document makeBoard(int side = 64) {
    Document doc(side, side, Rgba8{});
    doc.setEndless(true);
    return doc;
}

void dot(Document& doc, int x, int y, Rgba8 color = kRed) {
    doc.image().set(x, y, color);
    doc.markDirty(IRect{x, y, x + 1, y + 1});
    doc.commit();
}

} // namespace

TEST_CASE("growCanvas keeps pixels at their world position and moves the origin") {
    Document doc = makeBoard();
    dot(doc, 10, 20);
    REQUIRE(doc.growCanvas(100, 50, 30, 10) == Document::OpResult::Applied);
    CHECK(doc.image().width() == 64 + 100 + 30);
    CHECK(doc.image().height() == 64 + 50 + 10);
    CHECK(doc.originX() == -100);
    CHECK(doc.originY() == -50);
    CHECK(doc.image().get(110, 70) == kRed);
    CHECK(doc.image().get(10, 20).a == 0);
    CHECK(doc.endless());
}

TEST_CASE("growCanvas is one undo step that restores size, origin and guides") {
    Document doc = makeBoard();
    doc.guides().add(GuideAxis::Vertical, 5);
    REQUIRE(doc.growCanvas(40, 0, 0, 0) == Document::OpResult::Applied);
    CHECK(doc.guides().positions(GuideAxis::Vertical) == std::vector<int>{45}); // follows the content
    REQUIRE(doc.undo());
    CHECK(doc.image().width() == 64);
    CHECK(doc.originX() == 0);
    CHECK(doc.guides().positions(GuideAxis::Vertical) == std::vector<int>{5});
    REQUIRE(doc.redo());
    CHECK(doc.image().width() == 104);
    CHECK(doc.originX() == -40);
    CHECK(doc.guides().positions(GuideAxis::Vertical) == std::vector<int>{45});
}

TEST_CASE("growCanvas moves a plain selection, refuses a floating one and respects the limits") {
    Document doc = makeBoard();
    doc.selectRect(IRect{4, 4, 10, 10});
    REQUIRE(doc.growCanvas(8, 8, 0, 0) == Document::OpResult::Applied);
    CHECK(doc.selection().rect() == IRect{12, 12, 18, 18});

    REQUIRE(doc.paste(Image(4, 4, kRed), 0, 0));
    CHECK(doc.selection().floating());
    CHECK(doc.growCanvas(8, 0, 0, 0) == Document::OpResult::Nothing);
    doc.clearSelection();

    CHECK(doc.growCanvas(0, 0, 0, 0) == Document::OpResult::Nothing);
    CHECK(doc.growCanvas(100000, 0, 0, 0) == Document::OpResult::NoMemory);
    CHECK(doc.image().width() == 72);
}

TEST_CASE("reset ends endless mode") {
    Document doc = makeBoard();
    doc.reset(10, 10);
    CHECK_FALSE(doc.endless());
    CHECK(doc.originX() == 0);
}

TEST_CASE("opaqueBounds finds the drawn area") {
    Image img(20, 10);
    CHECK(opaqueBounds(img).empty());
    img.set(3, 2, kRed);
    CHECK(opaqueBounds(img) == IRect{3, 2, 4, 3});
    img.set(15, 8, kRed);
    img.set(0, 5, Rgba8{1, 2, 3, 1});
    CHECK(opaqueBounds(img) == IRect{0, 2, 16, 9});
    img.set(19, 9, kRed);
    CHECK(opaqueBounds(img) == IRect{0, 2, 20, 10});
}

TEST_CASE("floodTouchesEdge tells a closed area from one that leaks") {
    Image img(10, 10);
    for (int i = 2; i <= 6; ++i) { // a closed box
        img.set(i, 2, kRed);
        img.set(i, 6, kRed);
        img.set(2, i, kRed);
        img.set(6, i, kRed);
    }
    CHECK_FALSE(floodTouchesEdge(img, 4, 4, 0));
    CHECK(floodTouchesEdge(img, 0, 0, 0));
    CHECK(floodTouchesEdge(img, 8, 8, 0));
    img.set(6, 4, Rgba8{}); // open the box
    CHECK(floodTouchesEdge(img, 4, 4, 0));
    CHECK_FALSE(floodTouchesEdge(img, -1, 0, 0));
}

TEST_CASE("planBoardGrowth grows only the sides that need it, in chunks") {
    CHECK_FALSE(planBoardGrowth(1000, 1000, IRect{0, 0, 1000, 1000}, 1, 1).any());
    CHECK_FALSE(planBoardGrowth(1000, 1000, IRect{}, 1, 1).any());

    const BoardGrowth g = planBoardGrowth(1000, 1000, IRect{-10, 0, 1000, 1200}, 1, 1);
    REQUIRE(g.any());
    CHECK_FALSE(g.capped);
    CHECK(g.left >= 10 + kBoardMinGrowth);
    CHECK(g.left % kBoardChunk == 0);
    CHECK(g.bottom >= 200 + kBoardMinGrowth);
    CHECK(g.top == 0);
    CHECK(g.right == 0);
}

TEST_CASE("planBoardGrowth falls back to the exact amount and then stops at the limit") {
    // Already 16384 wide: nothing more fits on that axis.
    const BoardGrowth capped = planBoardGrowth(16384, 100, IRect{0, 0, 16500, 100}, 1, 1);
    CHECK(capped.capped);
    CHECK_FALSE(capped.any());

    // Just under the limit: the roomy growth does not fit, the exact one does.
    const BoardGrowth exact = planBoardGrowth(16000, 100, IRect{0, 0, 16100, 100}, 1, 1);
    CHECK_FALSE(exact.capped);
    CHECK(exact.right == 100);
}
