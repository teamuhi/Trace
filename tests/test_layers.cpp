#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "core/document.hpp"
#include "core/history.hpp"
#include "tools/toolbox.hpp"

using namespace trace;

namespace {

constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kGreen{0, 255, 0, 255};
constexpr Rgba8 kBlue{0, 0, 255, 255};
constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kClear{};

using Result = Document::OpResult;

// Paints a rectangle into the active cel and reports it, like a tool would; commits when asked.
void paint(Document& doc, IRect r, Rgba8 color, bool commit = true) {
    for (int y = r.y0; y < r.y1; ++y)
        for (int x = r.x0; x < r.x1; ++x)
            doc.image().set(x, y, color);
    doc.markDirty(r);
    if (commit)
        doc.commit();
}

// Everything observable about a document's content: the layout and every cel's pixels.
struct Snapshot {
    Layout layout;
    std::vector<std::vector<Rgba8>> cels;

    bool operator==(const Snapshot&) const = default;
};

Snapshot snap(const Document& doc) {
    Snapshot s;
    s.layout = doc.layout();
    for (int f = 0; f < doc.frameCount(); ++f) {
        for (int l = 0; l < doc.layerCount(); ++l) {
            const auto px = doc.cel(l, f).pixels();
            s.cels.emplace_back(px.begin(), px.end());
        }
    }
    return s;
}

// Transparent W x H document with a single layer and frame.
Document clearDoc(int w = 8, int h = 8) {
    return Document(w, h, kClear);
}

PointerEvent at(float x, float y) {
    PointerEvent ev;
    ev.pos = Vec2f{x, y};
    return ev;
}

LayerInfo propsOf(const Document& doc, int layer) {
    return doc.layer(layer);
}

} // namespace

// --- Paint's single-layer path -----------------------------------------------------------------------

TEST_CASE("A new document is one layer by one frame and displays its own cel") {
    Document doc(16, 8);
    CHECK(doc.layerCount() == 1);
    CHECK(doc.frameCount() == 1);
    CHECK(doc.activeLayer() == 0);
    CHECK(doc.activeFrame() == 0);
    CHECK(doc.activeCel() != kNoCel);
    CHECK(doc.layer(0).visible);
    CHECK(doc.layer(0).opacity == 255);
    CHECK_FALSE(doc.layer(0).locked);
    CHECK(doc.frame(0).durationMs == kDefaultFrameMs);
    CHECK(&doc.displayImage() == &doc.image()); // no copy: the cel itself
    CHECK(&doc.cel(0, 0) == &doc.image());
}

TEST_CASE("Painting in the single-layer case never allocates a composite and keeps the dirty rect exact") {
    Document doc(32, 32);
    doc.takeDirty();
    paint(doc, IRect{3, 4, 9, 7}, kRed);
    CHECK(&doc.displayImage() == &doc.image());
    CHECK(doc.takeDirty() == IRect{3, 4, 9, 7});
    CHECK(doc.displayImage().get(5, 5) == kRed);
}

TEST_CASE("Empty and tiny documents behave") {
    Document empty(0, 0);
    CHECK(empty.image().empty());
    CHECK(empty.displayImage().empty());
    CHECK(empty.addLayer() == Result::Nothing); // nothing to put a cel in
    CHECK(empty.takeDirty().empty());

    Document one(1, 1, kRed);
    CHECK(one.addLayer() == Result::Applied);
    CHECK(one.displayImage().get(0, 0) == kRed);
}

TEST_CASE("load and reset produce a 1x1 document with no history") {
    Document doc(8, 8);
    doc.addLayer();
    doc.addFrame();
    paint(doc, IRect{0, 0, 2, 2}, kRed);
    REQUIRE(doc.canUndo());

    doc.reset(5, 4, kBlue);
    CHECK(doc.layerCount() == 1);
    CHECK(doc.frameCount() == 1);
    CHECK_FALSE(doc.canUndo());
    CHECK_FALSE(doc.isModified());
    CHECK(doc.image().width() == 5);
    CHECK(&doc.displayImage() == &doc.image());
    CHECK(doc.history().bytes() == 0);

    doc.addLayer();
    Image img(3, 3, kGreen);
    doc.load(std::move(img));
    CHECK(doc.layerCount() == 1);
    CHECK(doc.frameCount() == 1);
    CHECK(doc.image().get(1, 1) == kGreen);
    CHECK_FALSE(doc.canUndo());
    CHECK(doc.takeDirty() == IRect{0, 0, 3, 3});
}

// --- Composite -----------------------------------------------------------------------------------------

TEST_CASE("Two opaque layers: the top one wins, and the composite is a separate buffer") {
    Document doc(4, 4, kRed);
    REQUIRE(doc.addLayer() == Result::Applied);
    paint(doc, IRect{1, 1, 3, 3}, kBlue);

    const Image& shown = doc.displayImage();
    CHECK(&shown != &doc.image());
    CHECK(shown.get(0, 0) == kRed);
    CHECK(shown.get(1, 1) == kBlue);
    CHECK(shown.get(2, 2) == kBlue);
    CHECK(shown.get(3, 3) == kRed);
}

TEST_CASE("Layer opacity is the blend coverage") {
    Document doc(2, 1, kRed);
    doc.addLayer();
    paint(doc, IRect{0, 0, 2, 1}, kBlue);
    LayerInfo top = propsOf(doc, 1);
    top.opacity = 128;
    REQUIRE(doc.setLayerProps(1, top) == Result::Applied);

    const Rgba8 expected = blendOver(kRed, kBlue, 128);
    CHECK(expected == Rgba8{127, 0, 128, 255}); // computed by hand
    CHECK(doc.displayImage().get(0, 0) == expected);
    CHECK(doc.displayImage().get(1, 0) == expected);

    // A translucent layer over transparent pixels keeps the color and scales the alpha.
    Document solo = clearDoc(2, 1);
    solo.addLayer();
    paint(solo, IRect{0, 0, 1, 1}, kGreen);
    LayerInfo p = propsOf(solo, 1);
    p.opacity = 100;
    solo.setLayerProps(1, p);
    CHECK(solo.displayImage().get(0, 0) == Rgba8{0, 255, 0, 100});
    CHECK(solo.displayImage().get(1, 0) == kClear);
}

TEST_CASE("A single layer below full opacity or hidden is composited, not shown raw") {
    Document doc(2, 2, kRed);
    LayerInfo p = propsOf(doc, 0);

    p.opacity = 128;
    REQUIRE(doc.setLayerProps(0, p) == Result::Applied);
    CHECK(&doc.displayImage() != &doc.image());
    CHECK(doc.displayImage().get(0, 0) == Rgba8{255, 0, 0, 128});

    p.opacity = 255;
    REQUIRE(doc.setLayerProps(0, p) == Result::Applied);
    CHECK(&doc.displayImage() == &doc.image());

    p.visible = false;
    REQUIRE(doc.setLayerProps(0, p) == Result::Applied);
    CHECK(&doc.displayImage() != &doc.image());
    CHECK(doc.displayImage().get(0, 0) == kClear);
}

TEST_CASE("Hidden layers are skipped by the composite") {
    Document doc(2, 2, kRed);
    doc.addLayer();
    paint(doc, IRect{0, 0, 2, 2}, kBlue);
    LayerInfo p = propsOf(doc, 1);
    p.visible = false;
    REQUIRE(doc.setLayerProps(1, p) == Result::Applied);

    // One visible layer at full opacity: the fast path shows that layer's cel, even though the active
    // layer is the hidden one.
    CHECK(&doc.displayImage() == &doc.cel(0, 0));
    CHECK(doc.displayImage().get(0, 0) == kRed);
    CHECK(doc.compositeOfFrame(0).get(0, 0) == kRed);

    p.visible = true;
    doc.setLayerProps(1, p);
    CHECK(doc.displayImage().get(0, 0) == kBlue);
}

TEST_CASE("The composite is re-blended only inside the dirty rectangle") {
    Document doc(8, 8, kRed);
    doc.addLayer();
    doc.displayImage();
    doc.takeDirty();

    // A pixel written without telling the document stays out of the composite...
    doc.image().set(1, 1, kBlue);
    CHECK(doc.displayImage().get(1, 1) == kRed);
    // ...until it is reported, and then only the reported region is touched.
    doc.image().set(6, 6, kGreen);
    doc.markDirty(IRect{1, 1, 2, 2});
    CHECK(doc.takeDirty() == IRect{1, 1, 2, 2});
    CHECK(doc.displayImage().get(1, 1) == kBlue);
    CHECK(doc.displayImage().get(6, 6) == kRed);
    doc.markDirty(IRect{6, 6, 7, 7});
    doc.takeDirty();
    CHECK(doc.displayImage().get(6, 6) == kGreen);
}

TEST_CASE("takeDirty leaves the composite current even if displayImage was not called first") {
    Document doc(4, 4, kRed);
    doc.addLayer();
    doc.takeDirty();
    paint(doc, IRect{0, 0, 2, 2}, kBlue);
    CHECK(doc.takeDirty() == IRect{0, 0, 2, 2});
    CHECK(doc.displayImage().get(0, 0) == kBlue);
}

TEST_CASE("compositeOfFrame renders any frame and rejects bad indices") {
    Document doc(2, 2, kRed);
    REQUIRE(doc.addFrame(Document::FrameFill::Empty) == Result::Applied);
    paint(doc, IRect{0, 0, 1, 1}, kGreen); // frame 1
    CHECK(doc.compositeOfFrame(0).get(0, 0) == kRed);
    CHECK(doc.compositeOfFrame(1).get(0, 0) == kGreen);
    CHECK(doc.compositeOfFrame(1).get(1, 1) == kClear);
    CHECK(doc.compositeOfFrame(-1).empty());
    CHECK(doc.compositeOfFrame(2).empty());
}

TEST_CASE("Visibility, opacity and activation changes dirty the whole canvas") {
    Document doc(16, 16, kRed);
    doc.addLayer();
    doc.takeDirty();

    LayerInfo p = propsOf(doc, 1);
    p.opacity = 10;
    doc.setLayerProps(1, p);
    CHECK(doc.takeDirty() == IRect{0, 0, 16, 16});

    p.visible = false;
    doc.setLayerProps(1, p);
    CHECK(doc.takeDirty() == IRect{0, 0, 16, 16});

    doc.setActive(0, 0);
    CHECK(doc.takeDirty() == IRect{0, 0, 16, 16});

    // Renaming changes nothing on screen, but it is still a step.
    p = propsOf(doc, 0);
    p.name = "Sky";
    doc.setLayerProps(0, p);
    doc.takeDirty();
    CHECK(doc.layer(0).name == "Sky");
}

TEST_CASE("Undo and redo of pixel steps dirty exactly the changed tiles in composite mode") {
    Document doc(200, 200, kRed);
    doc.addLayer();
    paint(doc, IRect{70, 70, 72, 72}, kBlue);
    doc.takeDirty();

    REQUIRE(doc.undo());
    const IRect r = doc.takeDirty();
    CHECK(r == IRect{64, 64, 128, 128});
    CHECK(doc.displayImage().get(70, 70) == kRed);
    REQUIRE(doc.redo());
    CHECK(doc.takeDirty() == IRect{64, 64, 128, 128});
    CHECK(doc.displayImage().get(70, 70) == kBlue);
}

// --- Activation ---------------------------------------------------------------------------------------

TEST_CASE("Switching the active cel commits pending edits, keeps the pixels and bumps the generation") {
    Document doc = clearDoc();
    doc.addLayer();
    doc.setActive(0, 0);
    const auto gen = doc.generation();
    paint(doc, IRect{1, 1, 3, 3}, kRed, false); // pending, not committed
    const std::size_t undoBefore = doc.history().undoCount();

    CHECK(doc.setActive(1, 0) == Result::Applied);
    CHECK(doc.history().undoCount() == undoBefore + 1); // the edit became a step
    CHECK(doc.generation() != gen);
    CHECK(doc.activeLayer() == 1);
    CHECK(doc.cel(0, 0).get(1, 1) == kRed);
    CHECK(doc.image().get(1, 1) == kClear);

    CHECK(doc.setActive(1, 0) == Result::Nothing); // already there
    CHECK(doc.setActive(5, 0) == Result::Nothing);
    CHECK(doc.setActive(0, -1) == Result::Nothing);
}

TEST_CASE("Switching cels drops a floating selection but its pixels stay") {
    Document doc = clearDoc(10, 10);
    doc.addLayer();
    doc.setActive(0, 0);
    paint(doc, IRect{2, 2, 4, 4}, kRed);
    doc.selectRect(IRect{2, 2, 4, 4});
    REQUIRE(doc.liftSelection(kClear));
    doc.moveSelectionTo(6, 6);
    REQUIRE(doc.selection().floating());

    CHECK(doc.setActive(1, 0) == Result::Applied);
    CHECK_FALSE(doc.selection().floating());
    CHECK(doc.cel(0, 0).get(6, 6) == kRed);
    CHECK(doc.cel(0, 0).get(2, 2) == kClear);

    // The move is part of history: undoing it brings us back to layer 0 and restores the original place.
    REQUIRE(doc.undo());
    CHECK(doc.activeLayer() == 0);
    CHECK(doc.image().get(6, 6) == kClear);
}

TEST_CASE("A plain selection survives a cel switch and copies from the new cel") {
    Document doc = clearDoc(10, 10);
    doc.addLayer();
    paint(doc, IRect{0, 0, 4, 4}, kGreen); // layer 1
    doc.setActive(0, 0);
    paint(doc, IRect{0, 0, 4, 4}, kRed);
    doc.selectRect(IRect{1, 1, 3, 3});
    CHECK(doc.copySelection().get(0, 0) == kRed);
    doc.setActive(1, 0);
    CHECK(doc.selection().active());
    CHECK(doc.copySelection().get(0, 0) == kGreen);
}

TEST_CASE("Undo across layers activates the cel the step belongs to") {
    Document doc = clearDoc();
    doc.addLayer();
    doc.setActive(0, 0);
    paint(doc, IRect{0, 0, 2, 2}, kRed);   // layer 0, step A
    doc.setActive(1, 0);
    paint(doc, IRect{4, 4, 6, 6}, kBlue);  // layer 1, step B
    doc.setActive(0, 0);
    const auto gen = doc.generation();

    REQUIRE(doc.undo()); // B
    CHECK(doc.activeLayer() == 1);
    CHECK(doc.image().get(4, 4) == kClear);
    CHECK(doc.cel(0, 0).get(0, 0) == kRed);
    CHECK(doc.generation() != gen);

    REQUIRE(doc.undo()); // A
    CHECK(doc.activeLayer() == 0);
    CHECK(doc.image().get(0, 0) == kClear);

    REQUIRE(doc.redo()); // A again
    CHECK(doc.activeLayer() == 0);
    CHECK(doc.image().get(0, 0) == kRed);
    REQUIRE(doc.redo()); // B again
    CHECK(doc.activeLayer() == 1);
    CHECK(doc.image().get(4, 4) == kBlue);
}

TEST_CASE("Undo across frames activates the right frame") {
    Document doc = clearDoc();
    paint(doc, IRect{0, 0, 2, 2}, kRed);
    REQUIRE(doc.addFrame() == Result::Applied);
    paint(doc, IRect{3, 3, 5, 5}, kGreen);
    doc.setActive(0, 0);

    REQUIRE(doc.undo());
    CHECK(doc.activeFrame() == 1);
    CHECK(doc.image().get(3, 3) == kClear);
    REQUIRE(doc.undo()); // the new frame goes away
    CHECK(doc.frameCount() == 1);
    REQUIRE(doc.undo()); // the red square
    CHECK(doc.image().get(0, 0) == kClear);
    CHECK_FALSE(doc.canUndo());
}

TEST_CASE("Gestures in flight are aborted when the active cel changes") {
    Document doc = clearDoc(20, 20);
    doc.addLayer();
    Toolbox tb;
    tb.settings().antialias = false;
    tb.settings().primary = kRed;
    tb.select(ToolId::Rectangle);
    tb.settings().shapeMode = ShapeMode::Filled;

    tb.press(doc, at(2.5f, 2.5f));
    tb.drag(doc, at(8.5f, 8.5f));
    REQUIRE(doc.setActive(0, 0) == Result::Applied);
    tb.drag(doc, at(12.5f, 12.5f));
    CHECK_FALSE(tb.gestureActive());
    tb.release(doc, at(12.5f, 12.5f));
    for (const Rgba8& p : doc.image().pixels())
        CHECK(p == kClear); // the new cel is untouched
}

// --- Layer operations -----------------------------------------------------------------------------------

TEST_CASE("addLayer inserts above the active layer, activates it and undoes cleanly") {
    Document doc = clearDoc();
    paint(doc, IRect{0, 0, 2, 2}, kRed);
    const Snapshot before = snap(doc);

    REQUIRE(doc.addLayer("Ink") == Result::Applied);
    CHECK(doc.layerCount() == 2);
    CHECK(doc.activeLayer() == 1);
    CHECK(doc.layer(1).name == "Ink");
    CHECK(doc.layer(0).id != doc.layer(1).id);
    CHECK(doc.image().get(0, 0) == kClear);
    CHECK(doc.isModified());
    const Snapshot after = snap(doc);

    REQUIRE(doc.undo());
    CHECK(snap(doc) == before);
    REQUIRE(doc.redo());
    CHECK(snap(doc) == after);

    // Insert above a lower layer when it is the active one.
    doc.setActive(0, 0);
    REQUIRE(doc.addLayer() == Result::Applied);
    CHECK(doc.activeLayer() == 1);
    CHECK(doc.layerCount() == 3);
    CHECK(doc.layer(1).name.rfind("Layer ", 0) == 0);
}

TEST_CASE("deleteLayer refuses the last layer, restores everything on undo and picks a sane active layer") {
    Document doc = clearDoc();
    CHECK(doc.deleteLayer(0) == Result::Nothing);
    doc.addLayer();
    paint(doc, IRect{1, 1, 3, 3}, kBlue);
    doc.addLayer();
    paint(doc, IRect{4, 4, 6, 6}, kGreen);
    const Snapshot before = snap(doc);
    const std::size_t bytesBefore = doc.history().bytes();

    CHECK(doc.deleteLayer(7) == Result::Nothing);
    CHECK(doc.deleteLayer(-1) == Result::Nothing);
    REQUIRE(doc.deleteLayer(2) == Result::Applied); // the active, top layer
    CHECK(doc.layerCount() == 2);
    CHECK(doc.activeLayer() == 1);
    CHECK(doc.history().bytes() >= bytesBefore + 8 * 8 * sizeof(Rgba8)); // the removed cel is kept in the step

    REQUIRE(doc.undo());
    CHECK(snap(doc) == before);
    CHECK(doc.activeLayer() == 2);
    CHECK(doc.image().get(4, 4) == kGreen);
    REQUIRE(doc.redo());
    CHECK(doc.layerCount() == 2);

    // Deleting a layer below the active one shifts the active index.
    REQUIRE(doc.undo());
    doc.setActive(2, 0);
    REQUIRE(doc.deleteLayer(0) == Result::Applied);
    CHECK(doc.activeLayer() == 1);
    CHECK(doc.image().get(4, 4) == kGreen);
}

TEST_CASE("duplicateLayer copies every cel and goes just above the source") {
    Document doc = clearDoc();
    paint(doc, IRect{0, 0, 3, 3}, kRed);
    doc.addFrame(Document::FrameFill::Duplicate);
    paint(doc, IRect{5, 5, 7, 7}, kBlue); // frame 1 differs
    const Snapshot before = snap(doc);

    REQUIRE(doc.duplicateLayer(0, "") == Result::Applied);
    CHECK(doc.layerCount() == 2);
    CHECK(doc.layer(1).name == "Layer 1 copy");
    CHECK(doc.activeLayer() == 1);
    CHECK(doc.cel(1, 0).get(1, 1) == kRed);
    CHECK(doc.cel(1, 1).get(6, 6) == kBlue);
    CHECK(doc.celId(0, 0) != doc.celId(1, 0)); // a real copy

    // Editing the copy leaves the original alone.
    paint(doc, IRect{0, 0, 1, 1}, kGreen);
    CHECK(doc.cel(0, 1).get(0, 0) == kRed);
    REQUIRE(doc.undo());
    REQUIRE(doc.undo());
    CHECK(snap(doc) == before);
    CHECK(doc.duplicateLayer(3) == Result::Nothing);
}

TEST_CASE("moveLayer reorders, keeps the active layer and round trips") {
    Document doc = clearDoc();
    doc.addLayer("B");
    paint(doc, IRect{0, 0, 1, 1}, kGreen);
    doc.addLayer("C");
    paint(doc, IRect{1, 0, 2, 1}, kBlue);
    doc.setActive(1, 0); // B is active
    const Snapshot before = snap(doc);
    const auto idB = doc.layer(1).id;

    CHECK(doc.moveLayer(1, 1) == Result::Nothing);
    CHECK(doc.moveLayer(1, 9) == Result::Nothing);
    REQUIRE(doc.moveLayer(1, 2) == Result::Applied);
    CHECK(doc.layer(2).name == "B");
    CHECK(doc.layer(1).name == "C");
    CHECK(doc.activeLayer() == 2);
    CHECK(doc.layer(doc.activeLayer()).id == idB);
    CHECK(doc.image().get(0, 0) == kGreen);

    REQUIRE(doc.moveLayer(2, 0) == Result::Applied);
    CHECK(doc.layer(0).name == "B");
    CHECK(doc.activeLayer() == 0);

    REQUIRE(doc.undo());
    REQUIRE(doc.undo());
    CHECK(snap(doc) == before);
    CHECK(doc.activeLayer() == 1);
}

TEST_CASE("mergeDown blends the layer into the one below in every frame") {
    Document doc(2, 1, kRed);
    doc.addFrame(Document::FrameFill::Duplicate); // frame 1 is red too
    doc.setActive(0, 0);
    doc.addLayer("Top");
    paint(doc, IRect{0, 0, 1, 1}, kBlue);       // frame 0: blue on the left
    doc.setActive(1, 1);
    paint(doc, IRect{1, 0, 2, 1}, kGreen);      // frame 1: green on the right
    LayerInfo top = propsOf(doc, 1);
    top.opacity = 128;
    doc.setLayerProps(1, top);
    const Snapshot before = snap(doc);

    CHECK(doc.mergeDown(0) == Result::Nothing); // nothing below the bottom layer
    REQUIRE(doc.mergeDown(1) == Result::Applied);
    CHECK(doc.layerCount() == 1);
    CHECK(doc.layer(0).name == "Layer 1"); // the lower layer's settings
    CHECK(doc.activeLayer() == 0);
    CHECK(doc.cel(0, 0).get(0, 0) == blendOver(kRed, kBlue, 128));
    CHECK(doc.cel(0, 0).get(1, 0) == kRed);
    CHECK(doc.cel(0, 1).get(1, 0) == blendOver(kRed, kGreen, 128));
    CHECK(doc.cel(0, 1).get(0, 0) == kRed);

    REQUIRE(doc.undo());
    CHECK(snap(doc) == before);
    REQUIRE(doc.redo());
    CHECK(doc.layerCount() == 1);
}

TEST_CASE("mergeDown with a hidden layer adds nothing and keeps the lower cel") {
    Document doc(2, 2, kRed);
    doc.addLayer();
    paint(doc, IRect{0, 0, 2, 2}, kBlue);
    LayerInfo p = propsOf(doc, 1);
    p.visible = false;
    doc.setLayerProps(1, p);
    const CelId lower = doc.celId(0, 0);

    REQUIRE(doc.mergeDown(1) == Result::Applied);
    CHECK(doc.celId(0, 0) == lower);
    CHECK(doc.cel(0, 0).get(0, 0) == kRed);
}

TEST_CASE("flatten renders every frame's composite into one layer") {
    Document doc(3, 1, kRed);
    doc.addLayer();
    paint(doc, IRect{1, 0, 3, 1}, kBlue);
    LayerInfo p = propsOf(doc, 1);
    p.opacity = 128;
    doc.setLayerProps(1, p);
    doc.addLayer("Hidden");
    paint(doc, IRect{0, 0, 3, 1}, kGreen);
    p = propsOf(doc, 2);
    p.visible = false;
    doc.setLayerProps(2, p);
    doc.addFrame(Document::FrameFill::Duplicate);
    const Image f0 = doc.compositeOfFrame(0);
    const Image f1 = doc.compositeOfFrame(1);
    const Snapshot before = snap(doc);

    REQUIRE(doc.flatten() == Result::Applied);
    CHECK(doc.layerCount() == 1);
    CHECK(doc.frameCount() == 2);
    CHECK(doc.layer(0).visible);
    CHECK(doc.layer(0).opacity == 255);
    CHECK(doc.cel(0, 0).pixels()[0] == f0.pixels()[0]);
    for (int f = 0; f < 2; ++f) {
        for (int x = 0; x < 3; ++x)
            CHECK(doc.cel(0, f).get(x, 0) == (f == 0 ? f0 : f1).get(x, 0));
    }
    CHECK(doc.cel(0, 0).get(0, 0) == kRed);
    CHECK(doc.cel(0, 0).get(1, 0) == blendOver(kRed, kBlue, 128));
    CHECK(&doc.displayImage() == &doc.image()); // back on the fast path

    CHECK(doc.flatten() == Result::Nothing);
    REQUIRE(doc.undo());
    CHECK(snap(doc) == before);
}

TEST_CASE("setLayerProps cleans names, ignores the id, and is one step per change") {
    Document doc = clearDoc();
    LayerInfo p = propsOf(doc, 0);
    const auto id = p.id;

    CHECK(doc.setLayerProps(0, p) == Result::Nothing);
    CHECK(doc.setLayerProps(3, p) == Result::Nothing);
    CHECK_FALSE(doc.canUndo());

    p.id = 999;
    p.name = std::string("  Sky\tline\n  ");
    p.locked = true;
    REQUIRE(doc.setLayerProps(0, p) == Result::Applied);
    CHECK(doc.layer(0).id == id);
    CHECK(doc.layer(0).name == "Skyline");
    CHECK(doc.layer(0).locked);

    p.name = "   ";
    p.locked = false;
    REQUIRE(doc.setLayerProps(0, p) == Result::Applied); // empty name keeps the old one
    CHECK(doc.layer(0).name == "Skyline");

    // Long names are cut on a character boundary.
    p.name = std::string(kMaxLayerNameBytes - 1, 'a') + "\xC3\xA9\xC3\xA9";
    REQUIRE(doc.setLayerProps(0, p) == Result::Applied);
    CHECK(doc.layer(0).name.size() == kMaxLayerNameBytes - 1);

    REQUIRE(doc.undo());
    CHECK(doc.layer(0).name == "Skyline");
    REQUIRE(doc.undo());
    CHECK(doc.layer(0).locked);
    REQUIRE(doc.undo());
    CHECK(doc.layer(0).name == "Layer 1");
    CHECK_FALSE(doc.layer(0).locked);
    CHECK_FALSE(doc.canUndo());
}

TEST_CASE("Undoing a property change keeps the layer the user is on") {
    Document doc = clearDoc();
    doc.addLayer();
    doc.setActive(0, 0);
    LayerInfo p = propsOf(doc, 1);
    p.name = "Renamed";
    doc.setLayerProps(1, p); // made while layer 0 is active
    doc.setActive(1, 0);
    doc.setActive(0, 0);
    REQUIRE(doc.undo());
    CHECK(doc.layer(1).name != "Renamed");
    CHECK(doc.activeLayer() == 0);
}

// --- Frame operations -------------------------------------------------------------------------------------

TEST_CASE("addFrame creates an empty or duplicated frame after the active one") {
    Document doc = clearDoc();
    doc.addLayer();
    paint(doc, IRect{0, 0, 2, 2}, kRed);
    doc.setFrameDuration(0, 250);
    const Snapshot before = snap(doc);

    REQUIRE(doc.addFrame(Document::FrameFill::Empty) == Result::Applied);
    CHECK(doc.frameCount() == 2);
    CHECK(doc.activeFrame() == 1);
    CHECK(doc.activeLayer() == 1);
    CHECK(doc.frame(1).durationMs == 250);
    CHECK(doc.image().get(0, 0) == kClear);
    CHECK(doc.cel(0, 1).get(0, 0) == kClear);

    doc.setActive(1, 0);
    REQUIRE(doc.addFrame(Document::FrameFill::Duplicate) == Result::Applied);
    CHECK(doc.frameCount() == 3);
    CHECK(doc.activeFrame() == 1); // after frame 0
    CHECK(doc.image().get(0, 0) == kRed);
    CHECK(doc.celId(1, 0) != doc.celId(1, 1));

    REQUIRE(doc.undo());
    REQUIRE(doc.undo());
    CHECK(snap(doc) == before);
}

TEST_CASE("deleteFrame refuses the last frame and restores on undo") {
    Document doc = clearDoc();
    CHECK(doc.deleteFrame(0) == Result::Nothing);
    paint(doc, IRect{0, 0, 2, 2}, kRed);
    doc.addFrame();
    paint(doc, IRect{3, 3, 4, 4}, kBlue);
    doc.addFrame();
    const Snapshot before = snap(doc);

    CHECK(doc.deleteFrame(5) == Result::Nothing);
    REQUIRE(doc.deleteFrame(1) == Result::Applied);
    CHECK(doc.frameCount() == 2);
    CHECK(doc.activeFrame() == 1); // the active frame (2) shifted down
    REQUIRE(doc.undo());
    CHECK(snap(doc) == before);
    CHECK(doc.cel(0, 1).get(3, 3) == kBlue);
    REQUIRE(doc.redo());
    CHECK(doc.frameCount() == 2);
}

TEST_CASE("moveFrame and setFrameDuration") {
    Document doc = clearDoc();
    paint(doc, IRect{0, 0, 1, 1}, kRed);
    doc.addFrame();
    paint(doc, IRect{0, 0, 1, 1}, kGreen);
    doc.addFrame();
    paint(doc, IRect{0, 0, 1, 1}, kBlue);
    doc.setActive(0, 1);
    const Snapshot before = snap(doc);

    CHECK(doc.moveFrame(1, 1) == Result::Nothing);
    CHECK(doc.moveFrame(0, 3) == Result::Nothing);
    REQUIRE(doc.moveFrame(1, 0) == Result::Applied);
    CHECK(doc.cel(0, 0).get(0, 0) == kGreen);
    CHECK(doc.cel(0, 1).get(0, 0) == kRed);
    CHECK(doc.activeFrame() == 0); // still the green frame
    REQUIRE(doc.undo());
    CHECK(snap(doc) == before);

    CHECK(doc.setFrameDuration(0, 100) == Result::Nothing);
    REQUIRE(doc.setFrameDuration(0, 5) == Result::Applied);
    CHECK(doc.frame(0).durationMs == 5);
    REQUIRE(doc.setFrameDuration(0, -20) == Result::Applied);
    CHECK(doc.frame(0).durationMs == kMinFrameMs);
    REQUIRE(doc.setFrameDuration(0, 1'000'000) == Result::Applied);
    CHECK(doc.frame(0).durationMs == kMaxFrameMs);
    CHECK(doc.setFrameDuration(9, 50) == Result::Nothing);
    REQUIRE(doc.undo());
    CHECK(doc.frame(0).durationMs == kMinFrameMs);
}

TEST_CASE("Limits: 256 layers and 999 frames") {
    Document layers(1, 1);
    for (int i = 1; i < kMaxLayers; ++i)
        REQUIRE(layers.addLayer() == Result::Applied);
    CHECK(layers.layerCount() == kMaxLayers);
    CHECK(layers.addLayer() == Result::Nothing);
    CHECK(layers.duplicateLayer(0) == Result::Nothing);
    CHECK(layers.layerCount() == kMaxLayers);
    REQUIRE(layers.deleteLayer(5) == Result::Applied);
    CHECK(layers.addLayer() == Result::Applied);

    Document frames(1, 1);
    for (int i = 1; i < kMaxFrames; ++i)
        REQUIRE(frames.addFrame() == Result::Applied);
    CHECK(frames.frameCount() == kMaxFrames);
    CHECK(frames.addFrame() == Result::Nothing);
    CHECK(frames.addFrame(Document::FrameFill::Duplicate) == Result::Nothing);
}

TEST_CASE("Layer and cel ids stay unique across undo and new edits") {
    Document doc = clearDoc();
    doc.addLayer();
    const auto firstId = doc.layer(1).id;
    doc.undo();
    doc.addLayer();
    CHECK(doc.layer(1).id != firstId); // ids are never handed out twice
    doc.undo();
    doc.redo();
    CHECK(doc.layerCount() == 2);
}

// --- History: structure steps ------------------------------------------------------------------------------

TEST_CASE("Pending edits are committed before a structure change, as their own step") {
    Document doc = clearDoc();
    paint(doc, IRect{0, 0, 2, 2}, kRed, false);
    REQUIRE(doc.addLayer() == Result::Applied);
    CHECK(doc.history().undoCount() == 2);

    REQUIRE(doc.undo());
    CHECK(doc.layerCount() == 1);
    CHECK(doc.image().get(0, 0) == kRed);
    REQUIRE(doc.undo());
    CHECK(doc.image().get(0, 0) == kClear);
}

TEST_CASE("Every user-level operation is exactly one undo step") {
    Document doc(4, 4, kRed);
    const auto steps = [&] { return doc.history().undoCount(); };
    std::size_t n = 0;
    const auto expectOne = [&](Result r) {
        CHECK(r == Result::Applied);
        CHECK(steps() == ++n);
    };
    expectOne(doc.addLayer());
    expectOne(doc.duplicateLayer(0));
    expectOne(doc.moveLayer(0, 2));
    expectOne(doc.mergeDown(2));
    expectOne(doc.setLayerProps(0, LayerInfo{0, "x", true, 200, true}));
    expectOne(doc.addFrame());
    expectOne(doc.addFrame(Document::FrameFill::Duplicate));
    expectOne(doc.moveFrame(0, 2));
    expectOne(doc.setFrameDuration(1, 300));
    expectOne(doc.deleteFrame(1));
    expectOne(doc.flatten());
    expectOne(doc.addLayer());
    expectOne(doc.deleteLayer(0));
    expectOne(doc.resizeImage(8, 8, ResizeFilter::Nearest));

    // And all of it unwinds, one step at a time, to the start.
    while (doc.canUndo())
        REQUIRE(doc.undo());
    CHECK(doc.layerCount() == 1);
    CHECK(doc.frameCount() == 1);
    CHECK(doc.image().width() == 4);
    CHECK(doc.image().get(0, 0) == kRed);
    CHECK_FALSE(doc.isModified());
    while (doc.canRedo())
        REQUIRE(doc.redo());
    CHECK(doc.image().width() == 8);
}

TEST_CASE("Saved state follows structure steps") {
    Document doc = clearDoc();
    CHECK_FALSE(doc.isModified());
    doc.addLayer();
    CHECK(doc.isModified());
    doc.markSaved();
    CHECK_FALSE(doc.isModified());
    doc.undo();
    CHECK(doc.isModified());
    doc.redo();
    CHECK_FALSE(doc.isModified());

    doc.undo();
    doc.markSaved(); // saved with one layer
    doc.addFrame();  // a new step discards nothing here, but moves away from the saved state
    CHECK(doc.isModified());
    doc.undo();
    CHECK_FALSE(doc.isModified());

    // A new step on top of an undone structure step discards the saved state that lived in the future.
    doc.redo();   // the frame step again
    doc.markSaved();
    doc.undo();
    doc.addLayer(); // forks the timeline
    doc.undo();
    CHECK(doc.isModified()); // the saved state is gone for good
}

TEST_CASE("Structure steps count toward the memory limit and are evicted oldest first") {
    constexpr int side = 64;
    Document doc(side, side, kRed);
    doc.setHistoryLimit(3 * side * side * sizeof(Rgba8));

    // Each deleteLayer keeps one 16 KB cel in its step (plus a little layout data).
    for (int i = 0; i < 8; ++i) {
        REQUIRE(doc.addLayer() == Result::Applied);
        REQUIRE(doc.deleteLayer(0) == Result::Applied);
    }
    CHECK(doc.history().bytes() <= 3 * side * side * sizeof(Rgba8) + 4096);
    CHECK(doc.history().undoCount() < 16);
    CHECK(doc.history().undoCount() >= 1);

    // What is left still undoes correctly.
    const std::size_t steps = doc.history().undoCount();
    for (std::size_t i = 0; i < steps; ++i)
        REQUIRE(doc.undo());
    CHECK_FALSE(doc.canUndo());
    CHECK(doc.layerCount() >= 1);
    CHECK(doc.isModified()); // the saved state was evicted

    // The newest step is kept even when it alone exceeds the limit.
    doc.setHistoryLimit(1);
    REQUIRE(doc.addLayer() == Result::Applied);
    REQUIRE(doc.deleteLayer(0) == Result::Applied);
    CHECK(doc.history().undoCount() == 1);
    REQUIRE(doc.undo());
}

TEST_CASE("Structure step bytes follow what the step holds when it is undone and redone") {
    Document doc(32, 32, kRed);
    doc.addLayer();
    const std::size_t afterAdd = doc.history().bytes();
    doc.deleteLayer(1); // holds the added layer's cel
    const std::size_t afterDelete = doc.history().bytes();
    CHECK(afterDelete >= afterAdd + 32 * 32 * sizeof(Rgba8));
    doc.undo();
    const std::size_t afterUndo = doc.history().bytes();
    CHECK(afterUndo + 32 * 32 * sizeof(Rgba8) <= afterDelete); // the cel is back in the document, not in the step
    CHECK(afterUndo >= afterAdd);
    doc.redo();
    CHECK(doc.history().bytes() == afterDelete);
    doc.undo();
    doc.undo();
    CHECK(doc.history().bytes() >= 32 * 32 * sizeof(Rgba8)); // the add step now holds that cel
}

TEST_CASE("A new edit after undoing a structure step discards the redo steps and frees their cels") {
    Document doc(32, 32, kRed);
    doc.addLayer();
    doc.undo();
    REQUIRE(doc.history().bytes() > 0);
    paint(doc, IRect{0, 0, 1, 1}, kBlue);
    CHECK_FALSE(doc.canRedo());
    CHECK(doc.history().bytes() == 32 * 32 * sizeof(Rgba8)); // just the one tile of the paint step
}

TEST_CASE("History rejects structure commits that do not add up and changes nothing") {
    CelStore store;
    store.layout.layers.push_back(LayerInfo{1, "a", true, 255, false});
    store.layout.frames.push_back(FrameInfo{});
    store.layout.grid.push_back(1);
    store.pool.emplace(1, Image(4, 4, kRed));
    Image reference = store.pool.at(1);
    History history;

    Layout next = store.layout;
    next.layers.push_back(LayerInfo{2, "b", true, 255, false});
    next.grid.push_back(2);

    CHECK_FALSE(history.commitStructure(store, reference, next, {}));                      // cel 2 missing
    CelPool wrong;
    wrong.emplace(3, Image(4, 4));
    CHECK_FALSE(history.commitStructure(store, reference, next, std::move(wrong)));        // wrong id
    CelPool empty;
    empty.emplace(2, Image());
    CHECK_FALSE(history.commitStructure(store, reference, next, std::move(empty)));        // empty image
    Layout badActive = next;
    badActive.activeLayer = 5;
    CelPool ok;
    ok.emplace(2, Image(4, 4));
    CHECK_FALSE(history.commitStructure(store, reference, badActive, std::move(ok)));      // invalid position
    Layout dup = next;
    dup.grid = {1, 1};
    CHECK_FALSE(history.commitStructure(store, reference, dup, {}));                       // duplicate ids

    CHECK(store.layout.layers.size() == 1);
    CHECK(store.pool.size() == 1);
    CHECK_FALSE(history.canUndo());

    CelPool good;
    good.emplace(2, Image(4, 4));
    CHECK(history.commitStructure(store, reference, next, std::move(good)));
    CHECK(store.pool.size() == 2);
    CHECK(history.canUndo());

    // The single-image API cannot apply a structure step; the store API can.
    Image current = store.pool.at(1);
    CHECK(history.undo(current, reference).empty());
    CHECK(history.canUndo());
    CHECK_FALSE(history.undo(store, reference).empty());
    CHECK(store.pool.size() == 1);
    CHECK(store.layout.layers.size() == 1);
    CHECK(history.peekRedo().has_value());
    CHECK(history.peekRedo()->kind == History::StepKind::Structure);
}

TEST_CASE("Pixel steps record their cel and refuse to apply on another one") {
    CelStore store;
    store.layout.layers = {LayerInfo{1, "a", true, 255, false}, LayerInfo{2, "b", true, 255, false}};
    store.layout.frames.push_back(FrameInfo{});
    store.layout.grid = {1, 2};
    store.pool.emplace(1, Image(4, 4, kRed));
    store.pool.emplace(2, Image(4, 4, kBlue));
    Image reference = store.pool.at(1);
    History history;

    store.pool.at(1).set(0, 0, kGreen);
    REQUIRE(history.commit(store.pool.at(1), reference, IRect{0, 0, 4, 4}, 1));
    CHECK(history.peekUndo()->cel == 1);
    CHECK(history.peekUndo()->kind == History::StepKind::Pixels);

    store.layout.activeLayer = 1;
    Image otherReference = store.pool.at(2);
    CHECK(history.undo(store, otherReference).empty()); // active cel is 2
    CHECK(store.pool.at(1).get(0, 0) == kGreen);

    store.layout.activeLayer = 0;
    CHECK_FALSE(history.undo(store, reference).empty());
    CHECK(store.pool.at(1).get(0, 0) == kRed);
}

// --- Transforms ---------------------------------------------------------------------------------------------

TEST_CASE("resizeImage, resizeCanvas and crop apply to every cel as one step and undo restores all") {
    Document doc(8, 8, kRed);
    doc.addLayer();
    paint(doc, IRect{2, 2, 6, 6}, kBlue);
    doc.addFrame(Document::FrameFill::Duplicate);
    paint(doc, IRect{0, 0, 2, 2}, kGreen);
    const Snapshot before = snap(doc);
    const std::size_t steps = doc.history().undoCount();

    REQUIRE(doc.resizeImage(16, 16, ResizeFilter::Nearest) == Result::Applied);
    CHECK(doc.history().undoCount() == steps + 1);
    for (int f = 0; f < 2; ++f) {
        for (int l = 0; l < 2; ++l) {
            CHECK(doc.cel(l, f).width() == 16);
            CHECK(doc.cel(l, f).height() == 16);
        }
    }
    CHECK(doc.cel(0, 0).get(15, 15) == kRed);
    CHECK(doc.cel(1, 0).get(8, 8) == kBlue);
    CHECK(doc.cel(1, 1).get(1, 1) == kGreen);
    CHECK(doc.displayImage().width() == 16);

    REQUIRE(doc.resizeCanvas(20, 24, Anchor::TopLeft, kWhite) == Result::Applied);
    CHECK(doc.cel(0, 1).width() == 20);
    CHECK(doc.cel(0, 1).height() == 24);
    CHECK(doc.cel(0, 1).get(19, 23) == kWhite);
    CHECK(doc.cel(0, 1).get(15, 15) == kRed);

    doc.selectRect(IRect{0, 0, 4, 4});
    REQUIRE(doc.cropToSelection() == Result::Applied);
    for (int f = 0; f < 2; ++f)
        for (int l = 0; l < 2; ++l)
            CHECK(doc.cel(l, f).width() == 4);

    while (doc.history().undoCount() > steps)
        REQUIRE(doc.undo());
    CHECK(snap(doc) == before);
    CHECK(doc.displayImage().width() == 8);
    while (doc.canRedo())
        REQUIRE(doc.redo());
    CHECK(doc.cel(1, 1).width() == 4);
}

TEST_CASE("A transform bumps the generation, rescales guides and refuses a no-op") {
    Document doc(10, 10, kRed);
    doc.addLayer();
    const auto gen = doc.generation();
    CHECK(doc.resizeImage(10, 10, ResizeFilter::Nearest) == Result::Nothing);
    CHECK(doc.resizeImage(0, 10, ResizeFilter::Nearest) == Result::Nothing);
    CHECK(doc.resizeImage(kMaxImageSide + 1, 10, ResizeFilter::Nearest) == Result::Nothing);
    CHECK(doc.generation() == gen);
    CHECK(doc.layerCount() == 2);
    CHECK(doc.image().width() == 10);

    REQUIRE(doc.resizeImage(20, 20, ResizeFilter::Nearest) == Result::Applied);
    CHECK(doc.generation() != gen);
    REQUIRE(doc.undo());
    CHECK(doc.image().width() == 10);
}

TEST_CASE("Structure operations and transforms flatten a floating selection first") {
    Document doc = clearDoc(10, 10);
    paint(doc, IRect{1, 1, 3, 3}, kRed);
    doc.selectRect(IRect{1, 1, 3, 3});
    REQUIRE(doc.liftSelection(kClear));
    doc.moveSelectionTo(5, 5);
    REQUIRE(doc.addLayer() == Result::Applied);
    CHECK_FALSE(doc.selection().floating());
    CHECK(doc.cel(0, 0).get(5, 5) == kRed);
    REQUIRE(doc.undo()); // the layer
    REQUIRE(doc.undo()); // the move
    CHECK(doc.image().get(1, 1) == kRed);
}

// --- Toolbox ------------------------------------------------------------------------------------------------------

TEST_CASE("Toolbox refuses to draw on a hidden or locked layer and says why") {
    Document doc(10, 10, kWhite);
    Toolbox tb;
    tb.settings().primary = kRed;
    tb.settings().antialias = false;
    tb.select(ToolId::Pencil);
    CHECK(tb.lastRefusal() == Toolbox::Refusal::None);

    LayerInfo p = propsOf(doc, 0);
    p.locked = true;
    doc.setLayerProps(0, p);
    tb.press(doc, at(2.5f, 2.5f));
    CHECK_FALSE(tb.gestureActive());
    CHECK(tb.lastRefusal() == Toolbox::Refusal::LayerLocked);
    tb.drag(doc, at(3.5f, 3.5f));
    tb.release(doc, at(3.5f, 3.5f));
    CHECK(doc.image().get(2, 2) == kWhite);
    CHECK(doc.image().get(3, 3) == kWhite);
    CHECK(doc.history().undoCount() == 1); // only the lock change

    p.locked = false;
    p.visible = false;
    doc.setLayerProps(0, p);
    tb.press(doc, at(2.5f, 2.5f));
    CHECK_FALSE(tb.gestureActive());
    CHECK(tb.lastRefusal() == Toolbox::Refusal::LayerHidden);

    p.visible = true;
    doc.setLayerProps(0, p);
    tb.press(doc, at(2.5f, 2.5f));
    CHECK(tb.gestureActive());
    CHECK(tb.lastRefusal() == Toolbox::Refusal::None); // reset by the next press
    tb.release(doc, at(2.5f, 2.5f));
    CHECK(doc.image().get(2, 2) == kRed);
}

TEST_CASE("Every drawing tool is refused on a locked layer; the picker and a marquee are not") {
    for (int i = 0; i < kToolCount; ++i) {
        Document doc(10, 10, kWhite);
        LayerInfo p = propsOf(doc, 0);
        p.locked = true;
        doc.setLayerProps(0, p);
        Toolbox tb;
        tb.select(static_cast<ToolId>(i));
        tb.press(doc, at(2.5f, 2.5f));
        const bool allowed = static_cast<ToolId>(i) == ToolId::Picker || static_cast<ToolId>(i) == ToolId::Select;
        CHECK(tb.gestureActive() == allowed);
        tb.release(doc, at(4.5f, 4.5f));
        for (const Rgba8& px : doc.image().pixels())
            CHECK(px == kWhite);
    }
}

TEST_CASE("Moving selected pixels on a locked layer is refused and keeps the selection") {
    Document doc(10, 10, kWhite);
    doc.selectRect(IRect{2, 2, 5, 5});
    LayerInfo p = propsOf(doc, 0);
    p.locked = true;
    doc.setLayerProps(0, p);
    Toolbox tb;
    tb.select(ToolId::Select);

    tb.press(doc, at(3.5f, 3.5f)); // inside the selection: would lift and move it
    CHECK_FALSE(tb.gestureActive());
    CHECK(tb.lastRefusal() == Toolbox::Refusal::LayerLocked);
    CHECK(doc.selection().active());
    CHECK_FALSE(doc.selection().floating());

    tb.press(doc, at(7.5f, 7.5f)); // outside: a new marquee, which edits nothing
    CHECK(tb.gestureActive());
    tb.release(doc, at(8.5f, 8.5f));
}

TEST_CASE("Drawing goes to the active cel of the active layer and frame") {
    Document doc = clearDoc(10, 10);
    doc.addLayer();
    doc.addFrame();
    Toolbox tb;
    tb.settings().primary = kRed;
    tb.settings().antialias = false;
    tb.select(ToolId::Pencil);
    tb.press(doc, at(1.5f, 1.5f));
    tb.release(doc, at(1.5f, 1.5f));
    CHECK(doc.cel(1, 1).get(1, 1) == kRed);
    CHECK(doc.cel(0, 1).get(1, 1) == kClear);
    CHECK(doc.cel(1, 0).get(1, 1) == kClear);
    CHECK(doc.displayImage().get(1, 1) == kRed);
    REQUIRE(doc.undo());
    CHECK(doc.cel(1, 1).get(1, 1) == kClear);
}

// --- Display after structural undo ------------------------------------------------------------------------------------

TEST_CASE("Undoing and redoing visibility changes switches between the fast path and the composite") {
    Document doc(4, 4, kRed);
    doc.addLayer();
    paint(doc, IRect{0, 0, 4, 4}, kBlue);
    CHECK(doc.displayImage().get(0, 0) == kBlue);
    LayerInfo p = propsOf(doc, 1);
    p.visible = false;
    doc.setLayerProps(1, p);
    CHECK(doc.displayImage().get(0, 0) == kRed);
    CHECK(&doc.displayImage() == &doc.cel(0, 0));

    doc.takeDirty();
    REQUIRE(doc.undo());
    CHECK(doc.takeDirty() == IRect{0, 0, 4, 4});
    CHECK(doc.displayImage().get(0, 0) == kBlue);
    REQUIRE(doc.redo());
    CHECK(doc.displayImage().get(0, 0) == kRed);
}

TEST_CASE("The composite follows a resize and its undo") {
    Document doc(4, 4, kRed);
    doc.addLayer();
    paint(doc, IRect{0, 0, 2, 2}, kBlue);
    CHECK(doc.displayImage().width() == 4);

    REQUIRE(doc.resizeImage(8, 8, ResizeFilter::Nearest) == Result::Applied);
    CHECK(doc.displayImage().width() == 8);
    CHECK(doc.displayImage().get(3, 3) == kBlue);
    CHECK(doc.displayImage().get(4, 4) == kRed);
    CHECK(doc.takeDirty() == IRect{0, 0, 8, 8});

    REQUIRE(doc.undo());
    CHECK(doc.displayImage().width() == 4);
    CHECK(doc.displayImage().get(1, 1) == kBlue);
    CHECK(doc.displayImage().get(2, 2) == kRed);
}

TEST_CASE("Undoing a structure step that changes the active cel bumps the generation") {
    Document doc = clearDoc();
    doc.addLayer();
    const auto gen = doc.generation();
    REQUIRE(doc.undo());
    CHECK(doc.generation() != gen);
    const auto gen2 = doc.generation();
    LayerInfo p = propsOf(doc, 0);
    p.name = "x";
    doc.setLayerProps(0, p);
    CHECK(doc.generation() == gen2); // the cel object did not change
}

TEST_CASE("activeEditable reflects visibility and lock of the active layer") {
    Document doc = clearDoc();
    CHECK(doc.activeEditable());
    LayerInfo p = propsOf(doc, 0);
    p.locked = true;
    doc.setLayerProps(0, p);
    CHECK_FALSE(doc.activeEditable());
    p.locked = false;
    p.visible = false;
    doc.setLayerProps(0, p);
    CHECK_FALSE(doc.activeEditable());
}

TEST_CASE("Out-of-range accessors are harmless") {
    Document doc = clearDoc();
    CHECK(doc.layer(-1).name.empty());
    CHECK(doc.layer(5).id == 0);
    CHECK(doc.frame(7).durationMs == kDefaultFrameMs);
    CHECK(doc.cel(3, 3).empty());
    CHECK(doc.celId(3, 3) == kNoCel);
}
