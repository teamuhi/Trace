#include <doctest/doctest.h>

#include <vector>

#include "core/document.hpp"
#include "core/history.hpp"
#include "tools/toolbox.hpp"

using namespace trace;

namespace {

constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kBlack{0, 0, 0, 255};
constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kGreen{0, 255, 0, 255};

std::vector<Rgba8> pixelsOf(const Image& img) {
    return std::vector<Rgba8>(img.pixels().begin(), img.pixels().end());
}

// Paints a rectangle straight into the document and reports it, like a tool would.
void paint(Document& doc, IRect r, Rgba8 color) {
    for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x)
            doc.image().set(x, y, color);
    doc.markDirty(r);
}

constexpr std::size_t kTileBytes = History::kTileSize * History::kTileSize * sizeof(Rgba8);

} // namespace

// --- History on its own --------------------------------------------------------------------------

TEST_CASE("History records only the tiles that changed") {
    Image image(200, 200, kWhite);
    Image reference = image;
    History history;

    image.set(10, 10, kBlack); // tile (0, 0)
    CHECK(history.commit(image, reference, IRect{0, 0, 200, 200}));
    CHECK(history.undoCount() == 1);
    CHECK(history.bytes() == kTileBytes);
}

TEST_CASE("History stores pixels touching a tile border in both tiles") {
    Image image(200, 200, kWhite);
    Image reference = image;
    History history;

    image.set(63, 63, kBlack);
    image.set(64, 64, kBlack);
    history.commit(image, reference, IRect{63, 63, 65, 65});
    CHECK(history.bytes() == 2 * kTileBytes);   // tiles (0,0) and (1,1)

    Image image2(200, 200, kWhite);
    Image reference2 = image2;
    History h2;
    for (const int x : {63, 64})
        image2.set(x, 10, kBlack);
    h2.commit(image2, reference2, IRect{63, 10, 65, 11});
    CHECK(h2.bytes() == 2 * kTileBytes);        // a run across the vertical border: tiles (0,0) and (1,0)
}

TEST_CASE("Undo and redo restore the exact pixels") {
    Image image(150, 100, kWhite);
    Image reference = image;
    History history;
    const std::vector<Rgba8> original = pixelsOf(image);

    for (int x = 20; x < 120; ++x)
        image.set(x, 50, kRed);
    history.commit(image, reference, IRect{20, 50, 120, 51});
    const std::vector<Rgba8> edited = pixelsOf(image);

    const IRect undone = history.undo(image, reference);
    CHECK_FALSE(undone.empty());
    CHECK(pixelsOf(image) == original);
    CHECK(pixelsOf(reference) == original);
    CHECK_FALSE(history.canUndo());
    CHECK(history.canRedo());

    history.redo(image, reference);
    CHECK(pixelsOf(image) == edited);
    CHECK(pixelsOf(reference) == edited);
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("Edge tiles smaller than a full tile round-trip") {
    Image image(70, 70, kWhite); // the last tile row and column are 6 pixels wide
    Image reference = image;
    History history;

    image.set(69, 69, kRed);
    image.set(65, 3, kGreen);
    history.commit(image, reference, IRect{0, 0, 70, 70});
    CHECK(history.bytes() == (6 * 6 + 6 * 64) * sizeof(Rgba8));

    history.undo(image, reference);
    CHECK(image.get(69, 69) == kWhite);
    CHECK(image.get(65, 3) == kWhite);
    history.redo(image, reference);
    CHECK(image.get(69, 69) == kRed);
    CHECK(image.get(65, 3) == kGreen);
}

TEST_CASE("A long sequence undoes back to every earlier state and forward again") {
    Image image(130, 130, kWhite);
    Image reference = image;
    History history;

    std::vector<std::vector<Rgba8>> states{pixelsOf(image)};
    for (int i = 0; i < 12; ++i) {
        const int x = i * 10;
        for (int y = 0; y < 130; ++y)
            image.set(x, y, Rgba8{static_cast<std::uint8_t>(i * 20), 0, 0, 255});
        REQUIRE(history.commit(image, reference, IRect{x, 0, x + 1, 130}));
        states.push_back(pixelsOf(image));
    }

    for (int i = 11; i >= 0; --i) {
        history.undo(image, reference);
        CHECK(pixelsOf(image) == states[static_cast<std::size_t>(i)]);
    }
    CHECK_FALSE(history.canUndo());
    for (int i = 1; i <= 12; ++i) {
        history.redo(image, reference);
        CHECK(pixelsOf(image) == states[static_cast<std::size_t>(i)]);
    }
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("Committing nothing records nothing") {
    Image image(100, 100, kWhite);
    Image reference = image;
    History history;

    CHECK_FALSE(history.commit(image, reference, IRect{0, 0, 100, 100}));
    CHECK_FALSE(history.commit(image, reference, IRect{}));
    CHECK_FALSE(history.commit(image, reference, IRect{500, 500, 600, 600}));
    CHECK_FALSE(history.canUndo());
    CHECK(history.bytes() == 0);
}

TEST_CASE("Changes outside the committed region are left for a later commit") {
    Image image(200, 200, kWhite);
    Image reference = image;
    History history;

    image.set(5, 5, kRed);
    image.set(150, 150, kGreen);
    CHECK(history.commit(image, reference, IRect{0, 0, 10, 10}));
    CHECK(reference.get(5, 5) == kRed);
    CHECK(reference.get(150, 150) == kWhite); // not part of the region, still pending
    CHECK(history.commit(image, reference, IRect{140, 140, 160, 160}));
    CHECK(history.undoCount() == 2);
}

TEST_CASE("A new commit after an undo discards the redo steps") {
    Image image(100, 100, kWhite);
    Image reference = image;
    History history;

    image.set(1, 1, kRed);
    history.commit(image, reference, IRect{0, 0, 100, 100});
    image.set(2, 2, kRed);
    history.commit(image, reference, IRect{0, 0, 100, 100});

    history.undo(image, reference);
    CHECK(history.canRedo());

    image.set(3, 3, kGreen);
    history.commit(image, reference, IRect{0, 0, 100, 100});
    CHECK_FALSE(history.canRedo());
    CHECK(history.undoCount() == 2);
    CHECK(history.bytes() == 2 * kTileBytes); // two steps of one tile each; the discarded step is not counted
}

TEST_CASE("After undo the reference matches the image, so the next commit sees only new edits") {
    Image image(100, 100, kWhite);
    Image reference = image;
    History history;

    image.set(5, 5, kRed);
    history.commit(image, reference, IRect{0, 0, 100, 100});
    history.undo(image, reference);
    CHECK_FALSE(history.commit(image, reference, IRect{0, 0, 100, 100}));
}

TEST_CASE("Memory limit evicts the oldest steps but keeps the newest") {
    Image image(100, 100, kWhite);
    Image reference = image;
    History history(2 * kTileBytes);

    for (int i = 0; i < 6; ++i) {
        image.set(5, 5, Rgba8{static_cast<std::uint8_t>(i * 30), 0, 0, 255});
        REQUIRE(history.commit(image, reference, IRect{0, 0, 100, 100}));
    }
    CHECK(history.undoCount() == 2);
    CHECK(history.bytes() <= 2 * kTileBytes);

    history.undo(image, reference);
    history.undo(image, reference);
    CHECK_FALSE(history.canUndo());
    CHECK(image.get(5, 5) == Rgba8{3 * 30, 0, 0, 255}); // the state before the oldest surviving step
}

TEST_CASE("A single step larger than the limit is still kept") {
    Image image(200, 200, kWhite);
    Image reference = image;
    History history(1); // absurdly small

    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 200; ++x)
            image.set(x, y, kRed);
    CHECK(history.commit(image, reference, IRect{0, 0, 200, 200}));
    CHECK(history.canUndo());
    history.undo(image, reference);
    CHECK(image.get(100, 100) == kWhite);
}

TEST_CASE("Lowering the limit evicts immediately") {
    Image image(100, 100, kWhite);
    Image reference = image;
    History history;
    for (int i = 0; i < 5; ++i) {
        image.set(5, 5, Rgba8{static_cast<std::uint8_t>(i + 1), 0, 0, 255});
        history.commit(image, reference, IRect{0, 0, 100, 100});
    }
    history.setMaxBytes(kTileBytes);
    CHECK(history.undoCount() == 1);
}

TEST_CASE("Saved state tracking follows undo, redo, forks and eviction") {
    Image image(100, 100, kWhite);
    Image reference = image;
    History history;
    CHECK(history.atSavedState());

    image.set(1, 1, kRed);
    history.commit(image, reference, IRect{0, 0, 100, 100});
    CHECK_FALSE(history.atSavedState());

    history.markSaved();
    CHECK(history.atSavedState());

    history.undo(image, reference);
    CHECK_FALSE(history.atSavedState());
    history.redo(image, reference);
    CHECK(history.atSavedState());

    // Fork: undo past the saved state, then commit something else. The saved state is gone for good.
    history.undo(image, reference);
    image.set(2, 2, kGreen);
    history.commit(image, reference, IRect{0, 0, 100, 100});
    CHECK_FALSE(history.atSavedState());
    history.undo(image, reference);
    CHECK_FALSE(history.atSavedState());
    history.redo(image, reference);
    CHECK_FALSE(history.atSavedState());
}

TEST_CASE("Saved state is lost when eviction drops the step leading back to it") {
    Image image(100, 100, kWhite);
    Image reference = image;
    History history(2 * kTileBytes);
    history.markSaved(); // saved at the very beginning

    for (int i = 0; i < 4; ++i) {
        image.set(5, 5, Rgba8{static_cast<std::uint8_t>(i + 1), 0, 0, 255});
        history.commit(image, reference, IRect{0, 0, 100, 100});
    }
    while (history.canUndo())
        history.undo(image, reference);
    CHECK_FALSE(history.atSavedState()); // the base state is no longer the saved one
}

TEST_CASE("clear forgets everything and the current state counts as saved") {
    Image image(100, 100, kWhite);
    Image reference = image;
    History history;
    image.set(1, 1, kRed);
    history.commit(image, reference, IRect{0, 0, 100, 100});
    history.clear();
    CHECK_FALSE(history.canUndo());
    CHECK(history.bytes() == 0);
    CHECK(history.atSavedState());
}

TEST_CASE("Mismatched image sizes are refused instead of corrupting memory") {
    Image a(100, 100, kWhite);
    Image b(50, 50, kWhite);
    History history;
    a.set(1, 1, kRed);
    CHECK_FALSE(history.commit(a, b, IRect{0, 0, 100, 100}));

    Image c(100, 100, kWhite);
    Image d = c;
    c.set(1, 1, kRed);
    history.commit(c, d, IRect{0, 0, 100, 100});
    Image small(10, 10, kWhite);
    Image smallRef = small;
    CHECK(history.undo(small, smallRef).empty());
    CHECK(history.undoCount() == 1);
}

// --- Document ------------------------------------------------------------------------------------

TEST_CASE("Document commit, undo and redo") {
    Document doc(100, 100);
    const std::vector<Rgba8> original = pixelsOf(doc.image());

    paint(doc, IRect{10, 10, 30, 30}, kRed);
    CHECK(doc.commit());
    CHECK(doc.canUndo());
    CHECK(doc.isModified());

    doc.takeDirty();
    CHECK(doc.undo());
    CHECK(pixelsOf(doc.image()) == original);
    CHECK(doc.takeDirty() == IRect{0, 0, 64, 64}); // the whole tile is re-uploaded

    CHECK(doc.redo());
    CHECK(doc.image().get(15, 15) == kRed);
    CHECK_FALSE(doc.redo());
    CHECK_FALSE(doc.canRedo());
}

TEST_CASE("Undo and redo do nothing on an untouched document") {
    Document doc(50, 50);
    CHECK_FALSE(doc.undo());
    CHECK_FALSE(doc.redo());
    CHECK_FALSE(doc.isModified());
}

TEST_CASE("Undo flushes edits that were never committed") {
    Document doc(100, 100);
    paint(doc, IRect{10, 10, 20, 20}, kRed); // no commit()
    CHECK(doc.undo());
    CHECK(doc.image().get(15, 15) == kWhite);
    CHECK(doc.redo());
    CHECK(doc.image().get(15, 15) == kRed);
}

TEST_CASE("Rendering aids such as texture re-uploads are not edits") {
    Document doc(100, 100);
    doc.takeDirty();
    doc.markAllDirty();
    CHECK_FALSE(doc.commit());
    CHECK_FALSE(doc.isModified());
}

TEST_CASE("Marking pixels dirty without changing them records no step") {
    Document doc(100, 100);
    doc.markDirty(IRect{0, 0, 100, 100});
    CHECK_FALSE(doc.commit());
    CHECK_FALSE(doc.canUndo());
    CHECK_FALSE(doc.isModified());
}

TEST_CASE("Modified flag follows the saved state") {
    Document doc(100, 100);
    CHECK_FALSE(doc.isModified());

    paint(doc, IRect{0, 0, 5, 5}, kRed);
    doc.commit();
    CHECK(doc.isModified());

    doc.markSaved();
    CHECK_FALSE(doc.isModified());

    paint(doc, IRect{50, 50, 55, 55}, kGreen);
    doc.commit();
    CHECK(doc.isModified());
    doc.undo();
    CHECK_FALSE(doc.isModified()); // back to exactly what was saved
}

TEST_CASE("reset and load clear the history and bump the generation") {
    Document doc(100, 100);
    paint(doc, IRect{0, 0, 5, 5}, kRed);
    doc.commit();
    const std::uint64_t before = doc.generation();

    doc.reset(40, 30);
    CHECK(doc.generation() == before + 1);
    CHECK_FALSE(doc.canUndo());
    CHECK_FALSE(doc.isModified());
    CHECK(doc.image().width() == 40);
    CHECK(doc.takeDirty() == IRect{0, 0, 40, 30});

    Image loaded(10, 20, kGreen);
    doc.load(std::move(loaded));
    CHECK(doc.generation() == before + 2);
    CHECK(doc.image().get(9, 19) == kGreen);
    CHECK_FALSE(doc.isModified());
    CHECK_FALSE(doc.undo());

    paint(doc, IRect{0, 0, 2, 2}, kRed);
    doc.commit();
    doc.undo();
    CHECK(doc.image().get(0, 0) == kGreen); // the loaded image, not the old white one
}

// --- gestures become undo steps ------------------------------------------------------------------

namespace {

PointerEvent at(float x, float y) {
    PointerEvent ev;
    ev.pos = Vec2f{x, y};
    return ev;
}

} // namespace

TEST_CASE("Each tool gesture is exactly one undo step") {
    Document doc(100, 100);
    Toolbox tb;
    tb.settings().antialias = false;
    tb.settings().primary = kRed;
    tb.select(ToolId::Pencil);

    for (int stroke = 0; stroke < 3; ++stroke) {
        const float y = 10.5f + static_cast<float>(stroke) * 10.0f;
        tb.press(doc, at(5.5f, y));
        for (int x = 6; x < 60; ++x)
            tb.drag(doc, at(static_cast<float>(x) + 0.5f, y)); // many samples, still one step
        tb.release(doc, at(60.5f, y));
    }
    CHECK(doc.history().undoCount() == 3);

    doc.undo();
    CHECK(doc.image().get(30, 30) == kWhite);
    CHECK(doc.image().get(30, 20) == kRed);
    doc.undo();
    doc.undo();
    CHECK(doc.image().get(30, 10) == kWhite);
    CHECK_FALSE(doc.canUndo());
}

TEST_CASE("A cancelled shape leaves no undo step; a cut-short stroke does") {
    Document doc(100, 100);
    Toolbox tb;
    tb.settings().primary = kRed;

    tb.select(ToolId::Rectangle);
    tb.press(doc, at(10.5f, 10.5f));
    tb.drag(doc, at(40.5f, 40.5f));
    tb.cancel(doc);
    CHECK_FALSE(doc.canUndo());
    CHECK_FALSE(doc.isModified());

    tb.select(ToolId::Brush);
    tb.press(doc, at(50.5f, 50.5f));
    tb.drag(doc, at(60.5f, 50.5f));
    tb.cancel(doc);
    CHECK(doc.history().undoCount() == 1);
    CHECK(doc.isModified());
    doc.undo();
    CHECK(doc.image().get(55, 50) == kWhite);
}

TEST_CASE("Fill is one undo step and undo restores the region") {
    Document doc(100, 100);
    Toolbox tb;
    tb.settings().primary = kRed;
    tb.select(ToolId::Fill);
    tb.press(doc, at(5.5f, 5.5f));
    tb.release(doc, at(5.5f, 5.5f));
    CHECK(doc.image().get(99, 99) == kRed);

    CHECK(doc.undo());
    CHECK(doc.image().get(99, 99) == kWhite);
    CHECK(doc.image().get(0, 0) == kWhite);
}

TEST_CASE("The picker changes colors but not the document") {
    Document doc(100, 100);
    Toolbox tb;
    tb.select(ToolId::Picker);
    tb.press(doc, at(5.5f, 5.5f));
    tb.release(doc, at(5.5f, 5.5f));
    CHECK_FALSE(doc.canUndo());
    CHECK_FALSE(doc.isModified());
}

TEST_CASE("A gesture interrupted by a document reset records nothing in the new history") {
    Document doc(100, 100);
    Toolbox tb;
    tb.settings().primary = kRed;
    tb.select(ToolId::Brush);
    tb.press(doc, at(5.5f, 5.5f));
    doc.reset(100, 100);
    tb.release(doc, at(9.5f, 5.5f));
    CHECK_FALSE(doc.canUndo());
    CHECK_FALSE(doc.isModified());
}
