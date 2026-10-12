#include <doctest/doctest.h>

#include <limits>
#include <set>
#include <string>
#include <vector>

#include "tools/toolbox.hpp"

using namespace trace;

namespace {

constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kBlack{0, 0, 0, 255};
constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kGreen{0, 255, 0, 255};
constexpr Rgba8 kBlue{0, 0, 255, 255};

PointerEvent at(float x, float y, bool shift = false, bool secondary = false) {
    PointerEvent ev;
    ev.pos = Vec2f{x, y};
    ev.shift = shift;
    ev.secondary = secondary;
    return ev;
}

int count(const Image& img, Rgba8 color) {
    int n = 0;
    for (const Rgba8& p : img.pixels())
        n += p == color ? 1 : 0;
    return n;
}

std::vector<Rgba8> pixelsOf(const Document& doc) {
    const auto px = doc.image().pixels();
    return std::vector<Rgba8>(px.begin(), px.end());
}

// A toolbox with crisp, predictable settings: hard edges, 1 px strokes, red primary, green secondary.
struct Fixture {
    Document doc{20, 20};
    Toolbox tb;

    Fixture() {
        ToolSettings& s = tb.settings();
        s.primary = kRed;
        s.secondary = kGreen;
        s.antialias = false;
        s.strokeWidth = 1;
        s.brushSize = 1;
        doc.takeDirty();
    }

    void drag(ToolId tool, Vec2f from, Vec2f to, bool shift = false, bool secondary = false) {
        tb.select(tool);
        tb.press(doc, at(from.x, from.y, shift, secondary));
        tb.drag(doc, at(to.x, to.y, shift, secondary));
        tb.release(doc, at(to.x, to.y, shift, secondary));
    }
};

} // namespace

// --- pencil, brush, eraser -----------------------------------------------------------------------

TEST_CASE("Pencil click paints one pixel in the primary color, right click in the secondary") {
    Fixture f;
    f.tb.select(ToolId::Pencil);

    f.tb.press(f.doc, at(3.4f, 4.6f));
    f.tb.release(f.doc, at(3.4f, 4.6f));
    CHECK(f.doc.image().get(3, 4) == kRed);
    CHECK(count(f.doc.image(), kRed) == 1);

    f.tb.press(f.doc, at(8.5f, 8.5f, false, true));
    f.tb.release(f.doc, at(8.5f, 8.5f, false, true));
    CHECK(f.doc.image().get(8, 8) == kGreen);
}

TEST_CASE("Pencil drag draws a continuous line and reports the touched region") {
    Fixture f;
    f.drag(ToolId::Pencil, Vec2f{1.5f, 2.5f}, Vec2f{8.5f, 2.5f});

    for (int x = 1; x <= 8; ++x)
        CHECK(f.doc.image().get(x, 2) == kRed);
    CHECK(count(f.doc.image(), kRed) == 8);
    CHECK(f.doc.takeDirty() == IRect{1, 2, 9, 3});
}

TEST_CASE("Pencil strokes may leave the image without damage") {
    Fixture f;
    f.drag(ToolId::Pencil, Vec2f{-30.0f, 5.5f}, Vec2f{60.0f, 5.5f});
    CHECK(count(f.doc.image(), kRed) == 20);
}

TEST_CASE("Translucent pencil strokes do not darken at segment joints") {
    Fixture f;
    f.tb.settings().primary = Rgba8{0, 0, 0, 128};
    f.tb.select(ToolId::Pencil);
    f.tb.press(f.doc, at(2.5f, 5.5f));
    for (int x = 3; x <= 12; ++x)
        f.tb.drag(f.doc, at(static_cast<float>(x) + 0.5f, 5.5f)); // one segment per pixel
    f.tb.release(f.doc, at(12.5f, 5.5f));

    const Rgba8 first = f.doc.image().get(2, 5);
    for (int x = 2; x <= 12; ++x)
        CHECK(f.doc.image().get(x, 5) == first);
}

TEST_CASE("Hard brush stamps a round dot of the requested size") {
    Fixture f;
    f.tb.settings().brushSize = 5;
    f.tb.select(ToolId::Brush);
    f.tb.press(f.doc, at(10.5f, 10.5f));
    f.tb.release(f.doc, at(10.5f, 10.5f));

    CHECK(count(f.doc.image(), kRed) == 21); // 5x5 without its four corners
    CHECK(f.doc.image().get(10, 10) == kRed);
    CHECK(f.doc.image().get(8, 8) == kWhite);
}

TEST_CASE("Antialiased brush strokes keep soft edges however many samples the stroke has") {
    Fixture f;
    f.tb.settings().brushSize = 8;
    f.tb.settings().antialias = true;
    f.tb.settings().primary = kBlack;
    f.tb.select(ToolId::Brush);
    f.tb.press(f.doc, at(2.5f, 10.5f));
    for (int i = 1; i <= 15; ++i)
        f.tb.drag(f.doc, at(2.5f + static_cast<float>(i), 10.5f));
    f.tb.release(f.doc, at(17.5f, 10.5f));

    // Above the stroke: the radius-4 edge row at distance 4.5 from the center line is partly covered.
    const Rgba8 soft = f.doc.image().get(10, 6);
    CHECK(soft.r > 0);
    CHECK(soft.r < 255);
}

TEST_CASE("Brush size is clamped to a sane range") {
    Fixture f;
    f.tb.settings().brushSize = -50;
    f.drag(ToolId::Brush, Vec2f{5.5f, 5.5f}, Vec2f{6.5f, 5.5f});
    CHECK(count(f.doc.image(), kRed) >= 1);

    Fixture g;
    g.tb.settings().brushSize = 1'000'000;
    g.drag(ToolId::Brush, Vec2f{5.5f, 5.5f}, Vec2f{6.5f, 5.5f});
    CHECK(count(g.doc.image(), kRed) == 400); // clamped to 200 px, still covers the whole 20x20 image
}

TEST_CASE("Eraser paints the secondary color, or transparency when asked") {
    Fixture f;
    f.doc.image().clear(kBlue);
    f.tb.settings().brushSize = 3;
    f.drag(ToolId::Eraser, Vec2f{5.5f, 5.5f}, Vec2f{5.5f, 5.5f});
    CHECK(f.doc.image().get(5, 5) == kGreen);

    f.tb.settings().eraseToTransparent = true;
    f.drag(ToolId::Eraser, Vec2f{12.5f, 12.5f}, Vec2f{12.5f, 12.5f});
    CHECK(f.doc.image().get(12, 12).a == 0);
    CHECK(f.doc.image().get(0, 0) == kBlue);
}

TEST_CASE("Brush ring radius reflects the tool and size") {
    Fixture f;
    f.tb.settings().brushSize = 10;
    f.tb.select(ToolId::Brush);
    CHECK(f.tb.cursorRadius() == doctest::Approx(5.0f));
    f.tb.select(ToolId::Eraser);
    CHECK(f.tb.cursorRadius() == doctest::Approx(5.0f));
    f.tb.select(ToolId::Pencil);
    CHECK(f.tb.cursorRadius() == 0.0f);
    f.tb.select(ToolId::Fill);
    CHECK(f.tb.cursorRadius() == 0.0f);
}

// --- shapes --------------------------------------------------------------------------------------

TEST_CASE("Line preview follows the pointer and removes its earlier shape") {
    Fixture f;
    const std::vector<Rgba8> before = pixelsOf(f.doc);

    f.tb.select(ToolId::Line);
    f.tb.press(f.doc, at(1.5f, 1.5f));
    f.tb.drag(f.doc, at(10.5f, 1.5f));
    CHECK(f.doc.image().get(10, 1) == kRed);

    f.tb.drag(f.doc, at(1.5f, 10.5f));
    CHECK(f.doc.image().get(10, 1) == kWhite); // the horizontal preview is gone
    CHECK(f.doc.image().get(1, 10) == kRed);

    f.tb.release(f.doc, at(1.5f, 10.5f));
    CHECK(count(f.doc.image(), kRed) == 10);
    CHECK(pixelsOf(f.doc) != before);
}

TEST_CASE("Cancelling a shape restores the image exactly") {
    Fixture f;
    f.doc.fillTestPattern();
    f.doc.takeDirty();
    const std::vector<Rgba8> before = pixelsOf(f.doc);

    f.tb.select(ToolId::Rectangle);
    f.tb.press(f.doc, at(2.5f, 2.5f));
    f.tb.drag(f.doc, at(15.5f, 12.5f));
    CHECK(pixelsOf(f.doc) != before);

    f.tb.cancel(f.doc);
    CHECK(pixelsOf(f.doc) == before);
    CHECK_FALSE(f.tb.gestureActive());
    CHECK_FALSE(f.doc.takeDirty().empty()); // the restored area is flagged for re-upload
}

TEST_CASE("Line with Shift snaps to 45-degree steps") {
    Fixture f;
    f.drag(ToolId::Line, Vec2f{0.5f, 0.5f}, Vec2f{10.5f, 3.5f}, true);
    CHECK(f.doc.image().get(10, 0) == kRed);
    CHECK(f.doc.image().get(10, 3) == kWhite);

    Fixture g;
    g.drag(ToolId::Line, Vec2f{0.5f, 0.5f}, Vec2f{10.5f, 8.5f}, true);
    CHECK(g.doc.image().get(8, 8) == kRed); // diagonal, keeping the larger extent
    CHECK(g.doc.image().get(10, 10) == kRed);

    Fixture h;
    h.drag(ToolId::Line, Vec2f{0.5f, 0.5f}, Vec2f{2.5f, 12.5f}, true);
    CHECK(h.doc.image().get(0, 12) == kRed); // vertical
}

TEST_CASE("A line click without dragging paints a dot") {
    Fixture f;
    f.tb.select(ToolId::Line);
    f.tb.press(f.doc, at(4.5f, 4.5f));
    f.tb.release(f.doc, at(4.5f, 4.5f));
    CHECK(count(f.doc.image(), kRed) == 1);
}

TEST_CASE("Thick antialiased line is wider than one pixel") {
    Fixture f;
    f.tb.settings().strokeWidth = 5;
    f.tb.settings().antialias = true;
    f.drag(ToolId::Line, Vec2f{2.5f, 10.5f}, Vec2f{17.5f, 10.5f});
    CHECK(f.doc.image().get(10, 8) != kWhite);
    CHECK(f.doc.image().get(10, 12) != kWhite);
    CHECK(f.doc.image().get(10, 5) == kWhite);
}

TEST_CASE("Rectangle outline, fill and both") {
    {
        Fixture f;
        f.drag(ToolId::Rectangle, Vec2f{2.5f, 2.5f}, Vec2f{6.5f, 5.5f});
        CHECK(f.doc.image().get(2, 2) == kRed);
        CHECK(f.doc.image().get(6, 5) == kRed);
        CHECK(f.doc.image().get(4, 2) == kRed);
        CHECK(f.doc.image().get(4, 3) == kWhite); // hollow
        CHECK(count(f.doc.image(), kRed) == 2 * 5 + 2 * 2);
    }
    {
        Fixture f;
        f.tb.settings().shapeMode = ShapeMode::Filled;
        f.drag(ToolId::Rectangle, Vec2f{2.5f, 2.5f}, Vec2f{6.5f, 5.5f});
        CHECK(count(f.doc.image(), kRed) == 5 * 4);
    }
    {
        Fixture f;
        f.tb.settings().shapeMode = ShapeMode::Both;
        f.drag(ToolId::Rectangle, Vec2f{2.5f, 2.5f}, Vec2f{6.5f, 5.5f});
        CHECK(f.doc.image().get(2, 2) == kRed);
        CHECK(f.doc.image().get(4, 3) == kGreen); // interior takes the secondary color
    }
}

TEST_CASE("Right-button shapes swap the colors") {
    Fixture f;
    f.tb.settings().shapeMode = ShapeMode::Both;
    f.drag(ToolId::Rectangle, Vec2f{2.5f, 2.5f}, Vec2f{6.5f, 5.5f}, false, true);
    CHECK(f.doc.image().get(2, 2) == kGreen);
    CHECK(f.doc.image().get(4, 3) == kRed);
}

TEST_CASE("Translucent Both-mode shapes do not blend the outline over the fill") {
    Fixture f;
    f.tb.settings().shapeMode = ShapeMode::Both;
    f.tb.settings().primary = Rgba8{255, 0, 0, 128};
    f.tb.settings().secondary = Rgba8{0, 0, 255, 128};
    f.drag(ToolId::Rectangle, Vec2f{2.5f, 2.5f}, Vec2f{9.5f, 9.5f});

    const Rgba8 outline = f.doc.image().get(2, 2);
    CHECK(outline == f.doc.image().get(5, 2));
    CHECK(outline == blendOver(kWhite, Rgba8{255, 0, 0, 128})); // over the white page only, once
    CHECK(f.doc.image().get(5, 5) == blendOver(kWhite, Rgba8{0, 0, 255, 128}));
}

TEST_CASE("Rectangle with Shift is a square") {
    Fixture f;
    f.tb.settings().shapeMode = ShapeMode::Filled;
    f.drag(ToolId::Rectangle, Vec2f{2.5f, 2.5f}, Vec2f{6.5f, 3.5f}, true); // 5 x 2 requested
    CHECK(count(f.doc.image(), kRed) == 25);
    CHECK(f.doc.image().get(6, 6) == kRed);

    Fixture g;
    g.tb.settings().shapeMode = ShapeMode::Filled;
    g.drag(ToolId::Rectangle, Vec2f{10.5f, 10.5f}, Vec2f{7.5f, 14.5f}, true); // dragged up-right/down-left
    CHECK(count(g.doc.image(), kRed) == 25);
}

TEST_CASE("Rectangle dragged backwards covers the same pixels") {
    Fixture f;
    Fixture g;
    f.tb.settings().shapeMode = ShapeMode::Filled;
    g.tb.settings().shapeMode = ShapeMode::Filled;
    f.drag(ToolId::Rectangle, Vec2f{3.5f, 3.5f}, Vec2f{9.5f, 7.5f});
    g.drag(ToolId::Rectangle, Vec2f{9.5f, 7.5f}, Vec2f{3.5f, 3.5f});
    CHECK(pixelsOf(f.doc) == pixelsOf(g.doc));
}

TEST_CASE("Ellipse tool fills and outlines") {
    Fixture f;
    f.tb.settings().shapeMode = ShapeMode::Filled;
    f.drag(ToolId::Ellipse, Vec2f{2.5f, 2.5f}, Vec2f{16.5f, 12.5f});
    CHECK(f.doc.image().get(9, 7) == kRed);
    CHECK(f.doc.image().get(2, 2) == kWhite); // corner of the bounding box is outside the ellipse

    Fixture g;
    g.drag(ToolId::Ellipse, Vec2f{2.5f, 2.5f}, Vec2f{16.5f, 12.5f});
    CHECK(g.doc.image().get(9, 7) == kWhite);
    CHECK(g.doc.image().get(2, 7) == kRed);
}

TEST_CASE("Shapes that start outside the image are clipped") {
    Fixture f;
    f.tb.settings().shapeMode = ShapeMode::Filled;
    f.drag(ToolId::Rectangle, Vec2f{-10.5f, -10.5f}, Vec2f{4.5f, 4.5f});
    CHECK(count(f.doc.image(), kRed) == 25);
}

// --- fill and picker -----------------------------------------------------------------------------

TEST_CASE("Fill tool fills the region under the click and honors tolerance") {
    Fixture f;
    f.doc.image().set(5, 5, kBlack);
    f.tb.select(ToolId::Fill);
    f.tb.press(f.doc, at(0.5f, 0.5f));
    f.tb.release(f.doc, at(0.5f, 0.5f));
    CHECK(count(f.doc.image(), kRed) == 399);
    CHECK(f.doc.image().get(5, 5) == kBlack);

    Fixture g;
    g.doc.image().clear(Rgba8{100, 100, 100, 255});
    g.doc.image().set(3, 3, Rgba8{110, 100, 100, 255});
    g.tb.settings().fillTolerance = 20;
    g.tb.select(ToolId::Fill);
    g.tb.press(g.doc, at(0.5f, 0.5f, false, true));
    g.tb.release(g.doc, at(0.5f, 0.5f, false, true));
    CHECK(count(g.doc.image(), kGreen) == 400); // right button fills with the secondary color
}

TEST_CASE("Fill outside the image does nothing") {
    Fixture f;
    f.tb.select(ToolId::Fill);
    f.tb.press(f.doc, at(-3.0f, 5.0f));
    f.tb.release(f.doc, at(-3.0f, 5.0f));
    CHECK(count(f.doc.image(), kWhite) == 400);
    CHECK(f.doc.takeDirty().empty());
}

TEST_CASE("Picker copies the pixel color, including transparency, and ignores off-image clicks") {
    Fixture f;
    f.doc.image().set(2, 2, kBlue);
    f.doc.image().set(3, 3, Rgba8{9, 8, 7, 0});

    f.tb.select(ToolId::Picker);
    f.tb.press(f.doc, at(2.5f, 2.5f));
    f.tb.release(f.doc, at(2.5f, 2.5f));
    CHECK(f.tb.settings().primary == kBlue);

    f.tb.press(f.doc, at(3.5f, 3.5f, false, true));
    f.tb.release(f.doc, at(3.5f, 3.5f, false, true));
    CHECK(f.tb.settings().secondary == Rgba8{9, 8, 7, 0});

    f.tb.press(f.doc, at(-5.0f, 3.5f));
    f.tb.release(f.doc, at(-5.0f, 3.5f));
    CHECK(f.tb.settings().primary == kBlue);
}

TEST_CASE("Picker follows the pointer while dragging") {
    Fixture f;
    f.doc.image().set(2, 2, kBlue);
    f.doc.image().set(6, 2, kGreen);
    f.tb.select(ToolId::Picker);
    f.tb.press(f.doc, at(2.5f, 2.5f));
    CHECK(f.tb.settings().primary == kBlue);
    f.tb.drag(f.doc, at(6.5f, 2.5f));
    CHECK(f.tb.settings().primary == kGreen);
    f.tb.release(f.doc, at(6.5f, 2.5f));
}

// --- toolbox -------------------------------------------------------------------------------------

TEST_CASE("The tool cannot change in the middle of a gesture") {
    Fixture f;
    f.tb.select(ToolId::Line);
    f.tb.press(f.doc, at(1.5f, 1.5f));
    CHECK(f.tb.gestureActive());
    CHECK_FALSE(f.tb.select(ToolId::Brush));
    CHECK(f.tb.current() == ToolId::Line);

    f.tb.release(f.doc, at(5.5f, 1.5f));
    CHECK_FALSE(f.tb.gestureActive());
    CHECK(f.tb.select(ToolId::Brush));
}

TEST_CASE("A second press during a gesture is ignored") {
    Fixture f;
    f.tb.select(ToolId::Line);
    f.tb.press(f.doc, at(1.5f, 1.5f));
    f.tb.press(f.doc, at(15.5f, 15.5f)); // would move the line's start if it were honored
    f.tb.release(f.doc, at(5.5f, 1.5f));
    CHECK(f.doc.image().get(1, 1) == kRed);
    CHECK(f.doc.image().get(5, 1) == kRed);
}

TEST_CASE("Drag, release and cancel without a press are harmless") {
    Fixture f;
    f.tb.select(ToolId::Brush);
    f.tb.drag(f.doc, at(3.5f, 3.5f));
    f.tb.release(f.doc, at(3.5f, 3.5f));
    f.tb.cancel(f.doc);
    CHECK(count(f.doc.image(), kWhite) == 400);
}

TEST_CASE("Replacing the document mid-gesture aborts the gesture without touching the new image") {
    Fixture f;
    f.tb.select(ToolId::Rectangle);
    f.tb.settings().shapeMode = ShapeMode::Filled;
    f.tb.press(f.doc, at(2.5f, 2.5f));
    f.tb.drag(f.doc, at(8.5f, 8.5f));

    f.doc.reset(20, 20); // e.g. File > New while the mouse button is held
    f.tb.drag(f.doc, at(12.5f, 12.5f));
    CHECK_FALSE(f.tb.gestureActive());
    f.tb.release(f.doc, at(12.5f, 12.5f));
    CHECK(count(f.doc.image(), kWhite) == 400);

    // The toolbox is usable again afterwards.
    f.drag(ToolId::Pencil, Vec2f{1.5f, 1.5f}, Vec2f{1.5f, 1.5f});
    CHECK(f.doc.image().get(1, 1) == kRed);
}

TEST_CASE("Replacing the document with a different size mid-stroke is safe") {
    Fixture f;
    f.tb.select(ToolId::Brush);
    f.tb.settings().antialias = true;
    f.tb.press(f.doc, at(5.5f, 5.5f));
    f.doc.reset(7, 3);
    f.tb.drag(f.doc, at(6.5f, 5.5f));
    f.tb.release(f.doc, at(6.5f, 5.5f));
    CHECK(count(f.doc.image(), kWhite) == 21);
}

TEST_CASE("Non-finite pointer positions never crash or corrupt memory") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    for (int i = 0; i < kToolCount; ++i) {
        Fixture f;
        f.tb.select(static_cast<ToolId>(i));
        f.tb.press(f.doc, at(nan, nan));
        f.tb.drag(f.doc, at(inf, -inf));
        f.tb.drag(f.doc, at(1e30f, -1e30f));
        f.tb.release(f.doc, at(nan, 5.0f));
        CHECK_FALSE(f.tb.gestureActive());
    }
}

TEST_CASE("Every tool survives an empty document") {
    for (int i = 0; i < kToolCount; ++i) {
        Toolbox tb;
        Document doc(0, 0);
        tb.select(static_cast<ToolId>(i));
        tb.press(doc, at(1.0f, 1.0f));
        tb.drag(doc, at(5.0f, 5.0f));
        tb.release(doc, at(5.0f, 5.0f));
        CHECK(doc.image().empty());
    }
}

TEST_CASE("swapColors exchanges primary and secondary") {
    Toolbox tb;
    tb.settings().primary = kRed;
    tb.settings().secondary = kBlue;
    tb.swapColors();
    CHECK(tb.settings().primary == kBlue);
    CHECK(tb.settings().secondary == kRed);
}

TEST_CASE("Every tool has a distinct name") {
    std::set<std::string> seen;
    for (int i = 0; i < kToolCount; ++i) {
        const char* name = Toolbox::name(static_cast<ToolId>(i));
        CHECK(std::string(name).size() > 0);
        CHECK(seen.insert(name).second);
    }
}

// --- pixel art options ---------------------------------------------------------------------------
// Pixel-perfect pencil, square brush and stamp, and global fill. Every one is off by default, so Paint's tools
// are unchanged; these tests also pin that.

namespace {

// Moves the pencil through pixel centers one after the other, as separate drag events.
void strokeThrough(Fixture& f, std::initializer_list<std::pair<int, int>> pixels) {
    f.tb.select(ToolId::Pencil);
    bool first = true;
    std::pair<int, int> last{0, 0};
    for (const auto& p : pixels) {
        const PointerEvent ev = at(static_cast<float>(p.first) + 0.5f, static_cast<float>(p.second) + 0.5f);
        if (first)
            f.tb.press(f.doc, ev);
        else
            f.tb.drag(f.doc, ev);
        first = false;
        last = p;
    }
    f.tb.release(f.doc, at(static_cast<float>(last.first) + 0.5f, static_cast<float>(last.second) + 0.5f));
}

} // namespace

TEST_CASE("Pixel art options are off by default") {
    const ToolSettings s;
    CHECK_FALSE(s.pixelPerfect);
    CHECK_FALSE(s.squareBrush);
    CHECK_FALSE(s.fillGlobal);
}

TEST_CASE("Without pixel-perfect the pencil keeps the corner of an L") {
    Fixture f;
    strokeThrough(f, {{2, 2}, {3, 2}, {3, 3}});
    CHECK(count(f.doc.image(), kRed) == 3);
    CHECK(f.doc.image().get(3, 2) == kRed);
}

TEST_CASE("Pixel-perfect pencil removes the corner pixel of an L") {
    Fixture f;
    f.tb.settings().pixelPerfect = true;
    strokeThrough(f, {{2, 2}, {3, 2}, {3, 3}});
    CHECK(count(f.doc.image(), kRed) == 2);
    CHECK(f.doc.image().get(2, 2) == kRed);
    CHECK(f.doc.image().get(3, 3) == kRed);
    CHECK(f.doc.image().get(3, 2) == kWhite);
}

TEST_CASE("Pixel-perfect pencil turns a staircase into a clean diagonal") {
    Fixture f;
    f.tb.settings().pixelPerfect = true;
    strokeThrough(f, {{0, 0}, {1, 0}, {1, 1}, {2, 1}, {2, 2}, {3, 2}, {3, 3}});
    CHECK(count(f.doc.image(), kRed) == 4);
    for (int i = 0; i < 4; ++i)
        CHECK(f.doc.image().get(i, i) == kRed);
}

TEST_CASE("Pixel-perfect pencil gives back what was under a removed corner") {
    Fixture f;
    f.doc.image().set(3, 2, kBlue);
    f.doc.markDirty(IRect{3, 2, 4, 3});
    f.doc.commit();
    f.tb.settings().pixelPerfect = true;
    strokeThrough(f, {{2, 2}, {3, 2}, {3, 3}});
    CHECK(f.doc.image().get(3, 2) == kBlue); // restored, not erased to white
    CHECK(count(f.doc.image(), kRed) == 2);
}

TEST_CASE("Pixel-perfect pencil leaves straight and diagonal lines alone") {
    Fixture f;
    f.tb.settings().pixelPerfect = true;
    f.drag(ToolId::Pencil, Vec2f{1.5f, 1.5f}, Vec2f{9.5f, 1.5f}); // horizontal
    CHECK(count(f.doc.image(), kRed) == 9);
    f.drag(ToolId::Pencil, Vec2f{1.5f, 5.5f}, Vec2f{8.5f, 12.5f}); // diagonal
    CHECK(count(f.doc.image(), kRed) == 9 + 8);
}

TEST_CASE("Pixel-perfect pencil: one click is one pixel, and one stroke is one undo step") {
    Fixture f;
    f.tb.settings().pixelPerfect = true;
    f.tb.select(ToolId::Pencil);
    f.tb.press(f.doc, at(4.5f, 4.5f));
    f.tb.release(f.doc, at(4.5f, 4.5f));
    CHECK(count(f.doc.image(), kRed) == 1);

    strokeThrough(f, {{8, 8}, {9, 8}, {9, 9}, {10, 9}});
    f.doc.undo();
    CHECK(count(f.doc.image(), kRed) == 1);
    f.doc.undo();
    CHECK(count(f.doc.image(), kRed) == 0);
}

TEST_CASE("Pixel-perfect pencil keeps going when the stroke leaves the image") {
    Fixture f;
    f.tb.settings().pixelPerfect = true;
    strokeThrough(f, {{-3, 5}, {-2, 5}, {-1, 5}, {0, 5}, {1, 5}, {1, 6}, {1, 60}});
    CHECK(f.doc.image().get(0, 5) == kRed);
    CHECK(f.doc.image().get(1, 5) == kWhite); // the L corner went
    CHECK(count(f.doc.image(), kRed) >= 2);
}

TEST_CASE("A square brush stamps an exact block, centered for odd sizes") {
    Fixture f;
    f.tb.settings().squareBrush = true;
    f.tb.settings().brushSize = 3;
    f.tb.select(ToolId::Brush);
    f.tb.press(f.doc, at(10.5f, 10.5f));
    f.tb.release(f.doc, at(10.5f, 10.5f));
    CHECK(count(f.doc.image(), kRed) == 9);
    for (int y = 9; y <= 11; ++y)
        for (int x = 9; x <= 11; ++x)
            CHECK(f.doc.image().get(x, y) == kRed);
    CHECK(f.tb.cursorSquareSize() == 3);
    CHECK(f.tb.cursorRadius() == 0.0f); // the canvas draws the square instead of a ring
}

TEST_CASE("A square brush of even size covers size x size pixels") {
    Fixture f;
    f.tb.settings().squareBrush = true;
    f.tb.settings().brushSize = 4;
    f.tb.select(ToolId::Brush);
    f.tb.press(f.doc, at(10.5f, 10.5f));
    f.tb.release(f.doc, at(10.5f, 10.5f));
    CHECK(count(f.doc.image(), kRed) == 16);
    CHECK(f.doc.image().get(8, 8) == kRed);
    CHECK(f.doc.image().get(11, 11) == kRed);
    CHECK(f.doc.image().get(12, 12) == kWhite);
}

TEST_CASE("A square brush stroke paints a translucent color once per pixel") {
    Fixture f;
    f.tb.settings().squareBrush = true;
    f.tb.settings().brushSize = 3;
    f.tb.settings().primary = Rgba8{0, 0, 0, 128};
    f.tb.select(ToolId::Brush);
    f.tb.press(f.doc, at(4.5f, 10.5f));
    for (int x = 5; x <= 14; ++x)
        f.tb.drag(f.doc, at(static_cast<float>(x) + 0.5f, 10.5f));
    f.tb.release(f.doc, at(14.5f, 10.5f));

    const Rgba8 expected = blendOver(kWhite, Rgba8{0, 0, 0, 128});
    for (int x = 3; x <= 15; ++x) {
        CHECK(f.doc.image().get(x, 9) == expected);
        CHECK(f.doc.image().get(x, 11) == expected);
    }
}

TEST_CASE("The square eraser clears to transparency and stays inside the image") {
    Fixture f;
    f.tb.settings().squareBrush = true;
    f.tb.settings().eraseToTransparent = true;
    f.tb.settings().brushSize = 5;
    f.tb.select(ToolId::Eraser);
    f.tb.press(f.doc, at(0.5f, 0.5f)); // a corner: part of the stamp is off the image
    f.tb.release(f.doc, at(0.5f, 0.5f));
    CHECK(f.doc.image().get(0, 0).a == 0);
    CHECK(f.doc.image().get(2, 2).a == 0);
    CHECK(f.doc.image().get(3, 3) == kWhite);
}

TEST_CASE("The round brush stays the default") {
    Fixture f;
    f.tb.settings().brushSize = 5;
    f.tb.select(ToolId::Brush);
    f.tb.press(f.doc, at(10.5f, 10.5f));
    f.tb.release(f.doc, at(10.5f, 10.5f));
    CHECK(count(f.doc.image(), kRed) == 21);
    CHECK(f.tb.cursorSquareSize() == 0);
}

TEST_CASE("Fill is contiguous by default and global on request") {
    const auto setup = [](Fixture& f) {
        // A black wall splits the white image into a left and a right region.
        for (int y = 0; y < 20; ++y)
            f.doc.image().set(10, y, kBlack);
        f.doc.markDirty(IRect{10, 0, 11, 20});
        f.doc.commit();
        f.tb.settings().primary = kBlue;
        f.tb.select(ToolId::Fill);
    };

    Fixture contiguous;
    setup(contiguous);
    contiguous.tb.press(contiguous.doc, at(2.5f, 2.5f));
    contiguous.tb.release(contiguous.doc, at(2.5f, 2.5f));
    CHECK(count(contiguous.doc.image(), kBlue) == 10 * 20);
    CHECK(contiguous.doc.image().get(15, 5) == kWhite);

    Fixture global;
    setup(global);
    global.tb.settings().fillGlobal = true;
    global.tb.press(global.doc, at(2.5f, 2.5f));
    global.tb.release(global.doc, at(2.5f, 2.5f));
    CHECK(count(global.doc.image(), kBlue) == 19 * 20);
    CHECK(global.doc.image().get(15, 5) == kBlue);
    CHECK(global.doc.image().get(10, 5) == kBlack);
}

TEST_CASE("Global fill honours the tolerance and reports its bounds") {
    Fixture f;
    f.doc.image().set(3, 4, Rgba8{250, 250, 250, 255});
    f.doc.image().set(8, 8, kBlack);
    f.doc.takeDirty();
    f.tb.settings().fillGlobal = true;
    f.tb.settings().fillTolerance = 10;
    f.tb.settings().primary = kGreen;
    f.tb.select(ToolId::Fill);
    f.tb.press(f.doc, at(0.5f, 0.5f));
    f.tb.release(f.doc, at(0.5f, 0.5f));

    CHECK(f.doc.image().get(3, 4) == kGreen); // within the tolerance of white
    CHECK(f.doc.image().get(8, 8) == kBlack);
    CHECK(count(f.doc.image(), kGreen) == 399);
    CHECK(f.doc.takeDirty() == IRect{0, 0, 20, 20});
}

TEST_CASE("ToolSettings compare equal field by field") {
    ToolSettings a;
    ToolSettings b;
    CHECK(a == b);
    b.pixelPerfect = true;
    CHECK_FALSE(a == b);
    b = a;
    b.primary = kBlue;
    CHECK_FALSE(a == b);
}
