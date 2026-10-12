#include <doctest/doctest.h>

#include <vector>

#include "core/document.hpp"
#include "core/history.hpp"
#include "tools/toolbox.hpp"

using namespace trace;
using Result = Document::TransformResult;

namespace {

constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kBlack{0, 0, 0, 255};
constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kGreen{0, 255, 0, 255};
constexpr Rgba8 kBlue{0, 0, 255, 255};

std::vector<Rgba8> pixelsOf(const Image& img) {
    return std::vector<Rgba8>(img.pixels().begin(), img.pixels().end());
}

void paint(Document& doc, IRect r, Rgba8 color) {
    for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x)
            doc.image().set(x, y, color);
    doc.markDirty(r);
}

PointerEvent at(float x, float y, float snapRadius = 0.0f) {
    PointerEvent ev;
    ev.pos = Vec2f{x, y};
    ev.snapRadius = snapRadius;
    return ev;
}

} // namespace

// --- History: whole-image steps ---------------------------------------------------------------------

TEST_CASE("commitReplace can be undone and redone, in both directions of size") {
    Image current(8, 6, kRed);
    Image reference = current;
    const Image original = current;
    History history;

    REQUIRE(history.commitReplace(current, reference, Image(3, 2, kBlue)));
    CHECK(current.width() == 3);
    CHECK(reference.width() == 3);
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());

    const IRect undone = history.undo(current, reference);
    CHECK(undone == IRect{0, 0, 8, 6});
    CHECK(pixelsOf(current) == pixelsOf(original));
    CHECK(pixelsOf(reference) == pixelsOf(original));
    CHECK(history.canRedo());

    const IRect redone = history.redo(current, reference);
    CHECK(redone == IRect{0, 0, 3, 2});
    CHECK(current.width() == 3);
    CHECK(current.get(2, 1) == kBlue);
    CHECK(pixelsOf(reference) == pixelsOf(current));
}

TEST_CASE("Pixel steps and replace steps interleave through undo and redo") {
    Image current(10, 10, kWhite);
    Image reference = current;
    History history;

    // 1: paint a pixel   2: shrink   3: paint a pixel on the small image
    current.set(1, 1, kRed);
    REQUIRE(history.commit(current, reference, IRect{0, 0, 10, 10}));
    REQUIRE(history.commitReplace(current, reference, Image(4, 4, kGreen)));
    current.set(2, 2, kBlue);
    REQUIRE(history.commit(current, reference, IRect{0, 0, 4, 4}));
    const Image afterAll = current;

    history.undo(current, reference); // the small-image pixel
    CHECK(current.width() == 4);
    CHECK(current.get(2, 2) == kGreen);
    history.undo(current, reference); // the shrink
    CHECK(current.width() == 10);
    CHECK(current.get(1, 1) == kRed);
    history.undo(current, reference); // the first pixel
    CHECK(current.get(1, 1) == kWhite);
    CHECK_FALSE(history.canUndo());

    // Redo walks forward through all three, alternating pixel and size changes.
    history.redo(current, reference);
    history.redo(current, reference);
    history.redo(current, reference);
    CHECK(pixelsOf(current) == pixelsOf(afterAll));
    CHECK(pixelsOf(reference) == pixelsOf(afterAll));
}

TEST_CASE("A new commit after undoing a replace discards the redo steps") {
    Image current(6, 6, kWhite);
    Image reference = current;
    History history;
    REQUIRE(history.commitReplace(current, reference, Image(2, 2, kRed)));
    history.undo(current, reference);

    current.set(0, 0, kBlack);
    REQUIRE(history.commit(current, reference, IRect{0, 0, 6, 6}));
    CHECK_FALSE(history.canRedo());
    CHECK(history.undoCount() == 1);
}

TEST_CASE("History refuses commits that do not match the image size at its position") {
    Image current(6, 6, kWhite);
    Image reference = current;
    History history;
    REQUIRE(history.commitReplace(current, reference, Image(2, 2, kRed)));

    // An image of the old size cannot be committed while the history is at the 2 x 2 state.
    Image wrong(6, 6, kWhite);
    Image wrongReference = wrong;
    wrong.set(0, 0, kBlack);
    CHECK_FALSE(history.commit(wrong, wrongReference, IRect{0, 0, 6, 6}));
    CHECK_FALSE(history.commitReplace(wrong, wrongReference, Image(3, 3, kRed)));
    CHECK(history.undoCount() == 1);

    // undo and redo refuse an image of the wrong size too
    CHECK(history.undo(wrong, wrongReference).empty());
}

TEST_CASE("commitReplace needs matching images and a non-empty result") {
    Image current(4, 4, kWhite);
    Image reference(5, 5, kWhite);
    History history;
    CHECK_FALSE(history.commitReplace(current, reference, Image(2, 2)));

    Image same = current;
    CHECK_FALSE(history.commitReplace(current, same, Image()));
    CHECK(current.width() == 4);
    CHECK_FALSE(history.canUndo());
}

TEST_CASE("Replace steps count their memory and are evicted oldest first") {
    Image current(100, 100, kWhite);
    Image reference = current;
    History history(100 * 100 * sizeof(Rgba8) + 1000); // room for about one 100 x 100 image

    REQUIRE(history.commitReplace(current, reference, Image(100, 100, kRed)));
    const std::size_t one = history.bytes();
    CHECK(one == 100 * 100 * sizeof(Rgba8));
    REQUIRE(history.commitReplace(current, reference, Image(100, 100, kGreen)));
    REQUIRE(history.commitReplace(current, reference, Image(100, 100, kBlue)));

    CHECK(history.undoCount() == 1); // only the newest is kept
    CHECK(history.bytes() == one);
    history.undo(current, reference);
    CHECK(current.get(0, 0) == kGreen);
}

TEST_CASE("Bytes stay correct when a replace step swaps sizes") {
    Image current(100, 100, kWhite);
    Image reference = current;
    History history;
    REQUIRE(history.commitReplace(current, reference, Image(10, 10, kRed)));
    CHECK(history.bytes() == 100 * 100 * sizeof(Rgba8)); // the step holds the old, big image
    history.undo(current, reference);
    CHECK(history.bytes() == 10 * 10 * sizeof(Rgba8)); // now it holds the small one
    history.redo(current, reference);
    CHECK(history.bytes() == 100 * 100 * sizeof(Rgba8));
}

TEST_CASE("The saved state survives a replace step") {
    Image current(6, 6, kWhite);
    Image reference = current;
    History history;
    history.markSaved();
    REQUIRE(history.commitReplace(current, reference, Image(3, 3, kRed)));
    CHECK_FALSE(history.atSavedState());
    history.undo(current, reference);
    CHECK(history.atSavedState());
}

// --- Document -----------------------------------------------------------------------------------------

TEST_CASE("Resizing the image is one undoable step") {
    Document doc(4, 4, kWhite);
    paint(doc, IRect{0, 0, 2, 2}, kRed);
    doc.commit();
    const std::vector<Rgba8> before = pixelsOf(doc.image());
    const auto generation = doc.generation();

    REQUIRE(doc.resizeImage(8, 8, ResizeFilter::Nearest) == Result::Applied);
    CHECK(doc.image().width() == 8);
    CHECK(doc.image().get(3, 3) == kRed);
    CHECK(doc.image().get(4, 4) == kWhite);
    CHECK(doc.generation() != generation);
    CHECK(doc.isModified());
    CHECK(doc.takeDirty() == IRect{0, 0, 8, 8});

    CHECK(doc.undo());
    CHECK(pixelsOf(doc.image()) == before);
    CHECK(doc.takeDirty() == IRect{0, 0, 4, 4});
    CHECK(doc.redo());
    CHECK(doc.image().width() == 8);
}

TEST_CASE("Edits made after a resize undo before the resize does") {
    Document doc(4, 4, kWhite);
    REQUIRE(doc.resizeImage(2, 2, ResizeFilter::Nearest) == Result::Applied);
    paint(doc, IRect{0, 0, 1, 1}, kRed);
    doc.commit();

    CHECK(doc.undo());
    CHECK(doc.image().width() == 2);
    CHECK(doc.image().get(0, 0) == kWhite);
    CHECK(doc.undo());
    CHECK(doc.image().width() == 4);
    CHECK_FALSE(doc.canUndo());
}

TEST_CASE("Pending edits are part of the history before a resize, not lost") {
    Document doc(4, 4, kWhite);
    paint(doc, IRect{1, 1, 2, 2}, kRed); // not committed yet
    REQUIRE(doc.resizeImage(2, 2, ResizeFilter::Nearest) == Result::Applied);
    CHECK(doc.undo());
    CHECK(doc.image().width() == 4);
    CHECK(doc.image().get(1, 1) == kRed);
    CHECK(doc.undo());
    CHECK(doc.image().get(1, 1) == kWhite);
}

TEST_CASE("Transforms that change nothing report Nothing and record no step") {
    Document doc(5, 5, kWhite);
    CHECK(doc.resizeImage(5, 5, ResizeFilter::Bilinear) == Result::Nothing);
    CHECK(doc.resizeImage(0, 5, ResizeFilter::Bilinear) == Result::Nothing);
    CHECK(doc.resizeImage(5, 99999, ResizeFilter::Bilinear) == Result::Nothing);
    CHECK(doc.resizeCanvas(5, 5, Anchor::Center, kRed) == Result::Nothing);
    CHECK(doc.resizeCanvas(-1, 5, Anchor::Center, kRed) == Result::Nothing);
    CHECK(doc.cropToSelection() == Result::Nothing); // no selection
    CHECK_FALSE(doc.canUndo());
    CHECK_FALSE(doc.isModified());
}

TEST_CASE("Undoing a resize back to the saved state clears the modified flag") {
    Document doc(4, 4, kWhite);
    doc.markSaved();
    REQUIRE(doc.resizeImage(6, 6, ResizeFilter::Nearest) == Result::Applied);
    CHECK(doc.isModified());
    doc.undo();
    CHECK_FALSE(doc.isModified());
}

TEST_CASE("Canvas size places the image by the anchor and fills new area") {
    Document doc(2, 2, kRed);
    REQUIRE(doc.resizeCanvas(4, 4, Anchor::BottomRight, kGreen) == Result::Applied);
    CHECK(doc.image().get(0, 0) == kGreen);
    CHECK(doc.image().get(2, 2) == kRed);
    CHECK(doc.image().get(3, 3) == kRed);

    REQUIRE(doc.resizeCanvas(2, 3, Anchor::TopLeft, kGreen) == Result::Applied);
    CHECK(doc.image().width() == 2);
    CHECK(doc.image().height() == 3);
    CHECK(doc.image().get(1, 2) == kGreen);

    doc.undo();
    CHECK(doc.image().width() == 4);
    doc.undo();
    CHECK(doc.image().width() == 2);
    CHECK(doc.image().get(1, 1) == kRed);
}

TEST_CASE("Canvas size can fill with transparency") {
    Document doc(1, 1, kRed);
    REQUIRE(doc.resizeCanvas(2, 1, Anchor::Left, Rgba8{}) == Result::Applied);
    CHECK(doc.image().get(1, 0) == Rgba8{});
}

TEST_CASE("Cropping to a selection keeps exactly the selected pixels") {
    Document doc(10, 10, kWhite);
    paint(doc, IRect{3, 4, 5, 6}, kRed);
    doc.commit();
    doc.selectRect(IRect{3, 4, 7, 8});

    REQUIRE(doc.cropToSelection() == Result::Applied);
    CHECK(doc.image().width() == 4);
    CHECK(doc.image().height() == 4);
    CHECK(doc.image().get(0, 0) == kRed);
    CHECK(doc.image().get(1, 1) == kRed);
    CHECK(doc.image().get(2, 2) == kWhite);
    CHECK_FALSE(doc.selection().active()); // the old rectangle means nothing on the new image

    doc.undo();
    CHECK(doc.image().width() == 10);
    CHECK(doc.image().get(3, 4) == kRed);
}

TEST_CASE("Cropping a floating selection includes the moved pixels") {
    Document doc(10, 10, kWhite);
    paint(doc, IRect{0, 0, 2, 2}, kRed);
    doc.commit();
    doc.selectRect(IRect{0, 0, 2, 2});
    REQUIRE(doc.liftSelection(kWhite));
    doc.moveSelectionTo(6, 6);

    REQUIRE(doc.cropToSelection() == Result::Applied);
    CHECK(doc.image().width() == 2);
    CHECK(doc.image().get(0, 0) == kRed);
    CHECK(doc.image().get(1, 1) == kRed);
}

TEST_CASE("A selection partly off the image crops to the part that is on it") {
    Document doc(10, 10, kWhite);
    doc.selectRect(IRect{8, 8, 20, 20}); // selectRect clips already
    REQUIRE(doc.cropToSelection() == Result::Applied);
    CHECK(doc.image().width() == 2);
    CHECK(doc.image().height() == 2);
}

TEST_CASE("Transforms end the selection and keep floating pixels") {
    Document doc(6, 6, kWhite);
    paint(doc, IRect{0, 0, 2, 2}, kRed);
    doc.commit();
    doc.selectRect(IRect{0, 0, 2, 2});
    REQUIRE(doc.liftSelection(kWhite));
    doc.moveSelectionTo(3, 3);

    REQUIRE(doc.resizeImage(12, 12, ResizeFilter::Nearest) == Result::Applied);
    CHECK_FALSE(doc.selection().active());
    CHECK(doc.image().get(6, 6) == kRed); // the lifted pixels were dropped in place first, then scaled
    CHECK(doc.image().get(0, 0) == kWhite);
}

TEST_CASE("A gesture that began before a transform is aborted by the generation change") {
    Document doc(10, 10, kWhite);
    Toolbox tools;
    tools.settings().primary = kRed;
    tools.settings().antialias = false;
    tools.select(ToolId::Pencil);
    tools.press(doc, at(1, 1));
    REQUIRE(doc.resizeCanvas(4, 4, Anchor::TopLeft, kWhite) == Result::Applied);
    tools.drag(doc, at(3, 3));
    tools.release(doc, at(3, 3));
    CHECK_FALSE(tools.gestureActive());
    CHECK(doc.image().width() == 4);
    CHECK(doc.image().get(3, 3) == kWhite);
}

TEST_CASE("Guides follow the content through transforms") {
    Document doc(100, 50, kWhite);
    doc.guides().add(GuideAxis::Vertical, 50);
    doc.guides().add(GuideAxis::Horizontal, 10);

    REQUIRE(doc.resizeImage(200, 100, ResizeFilter::Nearest) == Result::Applied);
    CHECK(doc.guides().positions(GuideAxis::Vertical) == std::vector<int>{100});
    CHECK(doc.guides().positions(GuideAxis::Horizontal) == std::vector<int>{20});

    REQUIRE(doc.resizeCanvas(250, 100, Anchor::Right, kWhite) == Result::Applied);
    CHECK(doc.guides().positions(GuideAxis::Vertical) == std::vector<int>{150}); // shifted 50 right

    doc.selectRect(IRect{100, 0, 250, 100});
    REQUIRE(doc.cropToSelection() == Result::Applied);
    CHECK(doc.guides().positions(GuideAxis::Vertical) == std::vector<int>{50});
}

TEST_CASE("Guides that fall off the canvas are dropped, and loading a document clears them") {
    Document doc(100, 100, kWhite);
    doc.guides().add(GuideAxis::Vertical, 90);
    doc.guides().add(GuideAxis::Vertical, 10);
    REQUIRE(doc.resizeCanvas(50, 100, Anchor::TopLeft, kWhite) == Result::Applied);
    CHECK(doc.guides().positions(GuideAxis::Vertical) == std::vector<int>{10});

    doc.reset(10, 10);
    CHECK(doc.guides().empty());
}

// --- Tools: snapping to guides ---------------------------------------------------------------------

TEST_CASE("A rectangle's corners snap so its edges land on the guides") {
    Document doc(40, 40, kWhite);
    doc.guides().add(GuideAxis::Vertical, 10);
    doc.guides().add(GuideAxis::Vertical, 30);
    doc.guides().add(GuideAxis::Horizontal, 5);
    doc.guides().add(GuideAxis::Horizontal, 25);

    Toolbox tools;
    tools.settings().primary = kRed;
    tools.settings().strokeWidth = 1;
    tools.settings().shapeMode = ShapeMode::Filled;
    tools.select(ToolId::Rectangle);
    tools.press(doc, at(10.6f, 4.7f, 1.0f));
    tools.drag(doc, at(29.7f, 25.4f, 1.0f));
    tools.release(doc, at(29.7f, 25.4f, 1.0f));

    // Filled box [10, 30) x [5, 25)
    CHECK(doc.image().get(10, 5) == kRed);
    CHECK(doc.image().get(29, 24) == kRed);
    CHECK(doc.image().get(9, 5) == kWhite);
    CHECK(doc.image().get(30, 24) == kWhite);
    CHECK(doc.image().get(10, 4) == kWhite);
    CHECK(doc.image().get(29, 25) == kWhite);
}

TEST_CASE("With snapRadius 0 shapes ignore the guides") {
    Document doc(40, 40, kWhite);
    doc.guides().add(GuideAxis::Vertical, 10);

    Toolbox tools;
    tools.settings().primary = kRed;
    tools.settings().shapeMode = ShapeMode::Filled;
    tools.select(ToolId::Rectangle);
    tools.press(doc, at(9.6f, 2.0f)); // would snap to the guide at 10 if snapping were on
    tools.drag(doc, at(20.0f, 6.0f));
    tools.release(doc, at(20.0f, 6.0f));
    CHECK(doc.image().get(9, 2) == kRed);
}

TEST_CASE("A marquee snaps to guides and a click still deselects") {
    Document doc(40, 40, kWhite);
    doc.guides().add(GuideAxis::Vertical, 8);
    doc.guides().add(GuideAxis::Vertical, 20);
    doc.guides().add(GuideAxis::Horizontal, 8);
    doc.guides().add(GuideAxis::Horizontal, 20);

    Toolbox tools;
    tools.select(ToolId::Select);
    tools.press(doc, at(8.4f, 7.8f, 1.0f));
    tools.drag(doc, at(19.6f, 20.3f, 1.0f));
    tools.release(doc, at(19.6f, 20.3f, 1.0f));
    REQUIRE(doc.selection().active());
    CHECK(doc.selection().rect() == IRect{8, 8, 20, 20});

    // A plain click next to a guide clears the selection instead of selecting a sliver.
    tools.press(doc, at(30.2f, 30.2f, 1.0f));
    tools.release(doc, at(30.2f, 30.2f, 1.0f));
    CHECK_FALSE(doc.selection().active());
    tools.press(doc, at(8.4f, 8.4f, 1.0f));
    tools.release(doc, at(8.4f, 8.4f, 1.0f));
    CHECK_FALSE(doc.selection().active());
}

TEST_CASE("A moved selection snaps its nearest edge to a guide") {
    Document doc(60, 60, kWhite);
    doc.guides().add(GuideAxis::Vertical, 40);
    doc.guides().add(GuideAxis::Horizontal, 30);
    doc.selectRect(IRect{0, 0, 10, 10});

    Toolbox tools;
    tools.select(ToolId::Select);
    tools.press(doc, at(5.0f, 5.0f, 2.0f));
    tools.drag(doc, at(35.0f, 24.0f, 2.0f)); // proposed top-left (30, 19): right edge 40 is on the guide
    tools.release(doc, at(35.0f, 24.0f, 2.0f));

    // x: right edge 40 is exactly on the vertical guide, so the span stays; y: bottom edge 29 is 1 from 30.
    CHECK(doc.selection().rect() == IRect{30, 20, 40, 30});
}
