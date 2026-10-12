#include <doctest/doctest.h>

#include <vector>

#include "core/document.hpp"
#include "tools/toolbox.hpp"

using namespace trace;

namespace {

constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kGreen{0, 255, 0, 255};
constexpr Rgba8 kBlue{0, 0, 255, 255};
constexpr Rgba8 kClear{};

std::vector<Rgba8> pixelsOf(const Document& d) {
    return std::vector<Rgba8>(d.image().pixels().begin(), d.image().pixels().end());
}

void paintRect(Document& d, IRect r, Rgba8 c) {
    for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x)
            d.image().set(x, y, c);
    d.markDirty(r);
    d.commit();
}

PointerEvent at(float x, float y, bool shift = false) {
    PointerEvent ev;
    ev.pos = Vec2f{x, y};
    ev.shift = shift;
    return ev;
}

void drag(Toolbox& tb, Document& d, Vec2f from, Vec2f to, bool shift = false) {
    tb.press(d, at(from.x, from.y, shift));
    tb.drag(d, at(to.x, to.y, shift));
    tb.release(d, at(to.x, to.y, shift));
}

} // namespace

TEST_CASE("selectRect clips to the image and ignores empty rectangles") {
    Document d(20, 20);
    d.selectRect(IRect{-5, 5, 10, 40});
    CHECK(d.selection().rect() == IRect{0, 5, 10, 20});
    d.selectRect(IRect{3, 3, 3, 9});
    CHECK_FALSE(d.selection().active());
    d.selectAll();
    CHECK(d.selection().rect() == IRect{0, 0, 20, 20});
    d.clearSelection();
    CHECK_FALSE(d.selection().active());
}

TEST_CASE("Lifting and moving a selection moves the pixels and leaves the clear color behind") {
    Document d(20, 20);
    paintRect(d, IRect{2, 2, 5, 5}, kRed);

    d.selectRect(IRect{2, 2, 5, 5});
    REQUIRE(d.liftSelection(kGreen));
    CHECK(d.selection().floating());
    CHECK(d.image().get(3, 3) == kGreen); // the source was cleared
    d.moveSelectionTo(10, 12);
    CHECK(d.image().get(11, 13) == kRed);
    CHECK(d.image().get(3, 3) == kGreen);
    CHECK(d.image().get(9, 12) == kWhite);
    CHECK(d.selection().rect() == IRect{10, 12, 13, 15});
}

TEST_CASE("Moving by one pixel over itself does not smear") {
    Document d(10, 1);
    d.image().set(2, 0, kRed);
    d.image().set(3, 0, kGreen);
    d.image().set(4, 0, kBlue);
    d.markDirty(IRect{0, 0, 10, 1});
    d.commit();

    d.selectRect(IRect{2, 0, 5, 1});
    d.liftSelection(kWhite);
    d.moveSelectionTo(3, 0);
    CHECK(d.image().get(2, 0) == kWhite);
    CHECK(d.image().get(3, 0) == kRed);
    CHECK(d.image().get(4, 0) == kGreen);
    CHECK(d.image().get(5, 0) == kBlue);
}

TEST_CASE("A selection dragged partly off the canvas keeps its pixels") {
    Document d(10, 10);
    paintRect(d, IRect{2, 2, 6, 6}, kRed);
    d.selectRect(IRect{2, 2, 6, 6});
    d.liftSelection(kWhite);

    d.moveSelectionTo(-2, -2);
    CHECK(d.image().get(0, 0) == kRed);
    CHECK(d.image().get(1, 1) == kRed);
    d.moveSelectionTo(20, 20); // completely off the image
    CHECK(d.image().get(9, 9) == kWhite);
    d.moveSelectionTo(4, 4);   // and back: nothing was lost
    CHECK(d.image().get(4, 4) == kRed);
    CHECK(d.image().get(7, 7) == kRed);
}

TEST_CASE("Floating pixels blend over what is underneath") {
    Document d(10, 10);
    paintRect(d, IRect{0, 0, 10, 10}, kBlue);
    d.image().set(1, 1, kRed);
    d.image().set(2, 1, Rgba8{0, 0, 0, 0});
    d.markDirty(IRect{0, 0, 10, 10});
    d.commit();

    d.selectRect(IRect{1, 1, 3, 2});
    d.liftSelection(kWhite);
    d.moveSelectionTo(5, 5);
    CHECK(d.image().get(5, 5) == kRed);
    CHECK(d.image().get(6, 5) == kBlue); // the transparent pixel shows what is underneath
}

TEST_CASE("A move and its undo restore the image exactly; redo repeats it") {
    Document d(30, 30);
    paintRect(d, IRect{5, 5, 12, 12}, kRed);
    const std::vector<Rgba8> before = pixelsOf(d);

    d.selectRect(IRect{5, 5, 12, 12});
    d.liftSelection(kWhite);
    d.moveSelectionTo(15, 15);
    d.clearSelection(); // anchors and records the step
    const std::vector<Rgba8> after = pixelsOf(d);
    CHECK(after != before);

    REQUIRE(d.undo());
    CHECK(pixelsOf(d) == before);
    CHECK_FALSE(d.selection().active()); // undo drops the selection
    REQUIRE(d.redo());
    CHECK(pixelsOf(d) == after);
}

TEST_CASE("Delete clears a plain selection and discards a floating one") {
    Document d(20, 20);
    paintRect(d, IRect{2, 2, 8, 8}, kRed);

    d.selectRect(IRect{2, 2, 5, 5});
    d.deleteSelection(kGreen);
    d.commit();
    CHECK(d.image().get(3, 3) == kGreen);
    CHECK(d.image().get(6, 6) == kRed);
    CHECK(d.selection().active()); // a plain selection stays after delete

    d.selectRect(IRect{5, 5, 8, 8});
    d.liftSelection(kWhite);
    d.moveSelectionTo(12, 12);
    d.deleteSelection(kWhite);
    d.commit();
    CHECK_FALSE(d.selection().active());
    CHECK(d.image().get(13, 13) == kWhite);  // the floating pixels are gone
    CHECK(d.image().get(6, 6) == kWhite);    // and the source stays cleared

    REQUIRE(d.undo());
    CHECK(d.image().get(6, 6) == kRed);
}

TEST_CASE("Delete to transparency really clears alpha") {
    Document d(10, 10);
    d.selectRect(IRect{0, 0, 4, 4});
    d.deleteSelection(kClear);
    CHECK(d.image().get(1, 1).a == 0);
}

TEST_CASE("Copy returns the selected pixels, whether plain or floating") {
    Document d(20, 20);
    paintRect(d, IRect{4, 4, 7, 6}, kBlue);
    CHECK(d.copySelection().empty()); // nothing selected

    d.selectRect(IRect{4, 4, 7, 6});
    Image copy = d.copySelection();
    REQUIRE(copy.width() == 3);
    REQUIRE(copy.height() == 2);
    CHECK(copy.get(2, 1) == kBlue);

    d.liftSelection(kWhite);
    d.moveSelectionTo(10, 10);
    Image floating = d.copySelection();
    CHECK(floating.width() == 3);
    CHECK(floating.get(0, 0) == kBlue);
}

TEST_CASE("Paste creates a floating selection that is one undo step") {
    Document d(20, 20);
    const std::vector<Rgba8> before = pixelsOf(d);

    REQUIRE(d.paste(Image(4, 3, kRed), 6, 7));
    CHECK(d.selection().floating());
    CHECK(d.selection().rect() == IRect{6, 7, 10, 10});
    CHECK(d.image().get(6, 7) == kRed);
    d.moveSelectionTo(1, 1);
    CHECK(d.image().get(6, 7) == kWhite); // moving restores what the paste covered
    CHECK(d.image().get(1, 1) == kRed);
    d.clearSelection();

    REQUIRE(d.undo());
    CHECK(pixelsOf(d) == before);
}

TEST_CASE("Pasting an empty image, or onto an empty document, does nothing") {
    Document d(10, 10);
    CHECK_FALSE(d.paste(Image(), 0, 0));
    Document none(0, 0);
    CHECK_FALSE(none.paste(Image(2, 2, kRed), 0, 0));
}

TEST_CASE("Pasting something larger than the canvas is clipped, not rejected") {
    Document d(8, 8);
    REQUIRE(d.paste(Image(20, 20, kRed), 0, 0));
    CHECK(d.image().get(7, 7) == kRed);
    CHECK(d.image().width() == 8);
}

TEST_CASE("Load and reset drop the selection") {
    Document d(10, 10);
    d.selectAll();
    d.reset(5, 5);
    CHECK_FALSE(d.selection().active());

    d.paste(Image(2, 2, kRed), 0, 0);
    d.load(Image(7, 7, kWhite));
    CHECK_FALSE(d.selection().active());
    CHECK(d.image().get(0, 0) == kWhite);
}

// --- the Select tool -----------------------------------------------------------------------------

TEST_CASE("Dragging on empty canvas draws a selection; a click deselects") {
    Document d(40, 40);
    Toolbox tb;
    tb.select(ToolId::Select);

    drag(tb, d, Vec2f{5.5f, 6.5f}, Vec2f{14.5f, 12.5f});
    CHECK(d.selection().rect() == IRect{5, 6, 15, 13});
    CHECK_FALSE(d.selection().floating());
    CHECK_FALSE(d.canUndo()); // selecting is not an edit

    tb.press(d, at(30.5f, 30.5f));
    tb.release(d, at(30.5f, 30.5f));
    CHECK_FALSE(d.selection().active());
}

TEST_CASE("A marquee can start anywhere and is clipped; Shift makes it square") {
    Document d(40, 40);
    Toolbox tb;
    tb.select(ToolId::Select);

    drag(tb, d, Vec2f{-10.5f, -10.5f}, Vec2f{8.5f, 8.5f});
    CHECK(d.selection().rect() == IRect{0, 0, 9, 9});

    drag(tb, d, Vec2f{10.5f, 10.5f}, Vec2f{20.5f, 13.5f}, true);
    CHECK(d.selection().rect() == IRect{10, 10, 21, 21});
}

TEST_CASE("Dragging inside the selection moves its pixels as one undo step per drag") {
    Document d(40, 40);
    paintRect(d, IRect{5, 5, 10, 10}, kRed);
    Toolbox tb;
    tb.settings().secondary = kWhite;
    tb.select(ToolId::Select);

    drag(tb, d, Vec2f{3.5f, 3.5f}, Vec2f{11.5f, 11.5f}); // select around the red square
    CHECK(d.selection().rect() == IRect{3, 3, 12, 12});

    drag(tb, d, Vec2f{6.5f, 6.5f}, Vec2f{26.5f, 16.5f}); // move by (+20, +10)
    CHECK(d.image().get(25, 15) == kRed);
    CHECK(d.image().get(6, 6) == kWhite);
    CHECK(d.selection().rect() == IRect{23, 13, 32, 22});
    CHECK(d.history().undoCount() == 2); // the paint, then the move

    drag(tb, d, Vec2f{25.5f, 15.5f}, Vec2f{26.5f, 15.5f}); // nudge one pixel right
    CHECK(d.history().undoCount() == 3);
    CHECK(d.image().get(26, 15) == kRed);

    d.undo();
    CHECK(d.image().get(25, 15) == kRed);
    d.undo();
    CHECK(d.image().get(6, 6) == kRed);
}

TEST_CASE("Escape during a marquee restores the previous selection; during a move it restores the position") {
    Document d(40, 40);
    paintRect(d, IRect{5, 5, 10, 10}, kRed);
    Toolbox tb;
    tb.select(ToolId::Select);
    drag(tb, d, Vec2f{4.5f, 4.5f}, Vec2f{11.5f, 11.5f});
    const IRect first = d.selection().rect();

    tb.press(d, at(30.5f, 30.5f));
    tb.drag(d, at(35.5f, 35.5f));
    CHECK(d.selection().rect() != first);
    tb.cancel(d);
    CHECK(d.selection().rect() == first);

    tb.press(d, at(7.5f, 7.5f));
    tb.drag(d, at(20.5f, 20.5f));
    tb.cancel(d);
    CHECK(d.selection().rect() == first);
    CHECK(d.image().get(6, 6) == kRed); // lifted, then put back where it was
    CHECK(d.image().get(30, 30) == kWhite);
}

TEST_CASE("Using another tool anchors a floating selection first") {
    Document d(40, 40);
    paintRect(d, IRect{5, 5, 10, 10}, kRed);
    Toolbox tb;
    tb.settings().antialias = false;
    tb.settings().primary = kBlue;
    tb.settings().brushSize = 1;
    tb.select(ToolId::Select);
    drag(tb, d, Vec2f{4.5f, 4.5f}, Vec2f{11.5f, 11.5f});
    drag(tb, d, Vec2f{6.5f, 6.5f}, Vec2f{26.5f, 26.5f}); // float it away

    tb.select(ToolId::Pencil);
    tb.press(d, at(1.5f, 1.5f));
    tb.release(d, at(1.5f, 1.5f));
    CHECK_FALSE(d.selection().active());
    CHECK(d.image().get(1, 1) == kBlue);
    CHECK(d.image().get(25, 25) == kRed); // the moved pixels stayed where they were dropped
}

TEST_CASE("Replacing the document during a selection gesture is safe") {
    Document d(40, 40);
    Toolbox tb;
    tb.select(ToolId::Select);
    tb.press(d, at(5.5f, 5.5f));
    tb.drag(d, at(15.5f, 15.5f));
    d.reset(40, 40);
    tb.drag(d, at(20.5f, 20.5f));
    tb.release(d, at(20.5f, 20.5f));
    CHECK_FALSE(d.selection().active());
}
