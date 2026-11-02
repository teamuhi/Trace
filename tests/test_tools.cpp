#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <utility>
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

TEST_CASE("Above the bottom layer the eraser leaves transparency, whatever the setting") {
    Fixture f;
    f.doc.image().clear(kBlue);
    REQUIRE(f.doc.addLayer() == Document::OpResult::Applied); // the new layer is active and sits above
    REQUIRE(f.doc.activeLayer() == 1);
    f.doc.image().clear(kBlue);
    f.tb.settings().eraseToTransparent = false;
    CHECK(clearsToTransparency(f.tb.settings(), f.doc));

    f.tb.settings().brushSize = 3;
    f.drag(ToolId::Eraser, Vec2f{5.5f, 5.5f}, Vec2f{5.5f, 5.5f});
    CHECK(f.doc.image().get(5, 5).a == 0);

    // The bottom layer still follows the setting.
    REQUIRE(f.doc.setActive(0, 0) == Document::OpResult::Applied);
    CHECK_FALSE(clearsToTransparency(f.tb.settings(), f.doc));
    f.tb.settings().eraseToTransparent = true;
    CHECK(clearsToTransparency(f.tb.settings(), f.doc));
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

// --- mirror drawing ------------------------------------------------------------------------------

namespace {

// One symmetry setup: which mirrors are on, where the axes are (half pixels, kSymmetryCenter = canvas middle) and
// how many rotated copies there are.
struct Mirror {
    bool x = false;
    bool y = false;
    int axisX = kSymmetryCenter;
    int axisY = kSymmetryCenter;
    int rotations = 1; // 1 = off, else 2, 3, 4, 6 or 8 copies around where the axes cross
};

// The symmetry list for a setup: Mirror X and Mirror Y at the axes (in that order), then a Rotate about where the
// axes cross. Off axes and a rotation count of 1 (or an invalid one) add nothing.
SymmetrySet toSet(const Mirror& m) {
    SymmetrySet set;
    const auto place = [&](SymmetryItem item) {
        item.posX = m.axisX;
        item.posY = m.axisY;
        addSymmetryItem(set, item);
    };
    if (m.x)
        place(makeMirrorX());
    if (m.y)
        place(makeMirrorY());
    if (sanitizeRotations(m.rotations) > 1)
        place(makeRotate(m.rotations));
    return set;
}

// A canvas of any size with the same crisp settings as Fixture, drawing with the given symmetry setup.
struct MirrorCanvas {
    Document doc;
    Toolbox tb;

    MirrorCanvas(int w, int h, const Mirror& m) : MirrorCanvas(w, h, toSet(m)) {}

    MirrorCanvas(int w, int h, const SymmetrySet& set) : doc(w, h) {
        ToolSettings& s = tb.settings();
        s.primary = kRed;
        s.secondary = kGreen;
        s.antialias = false;
        s.strokeWidth = 1;
        s.brushSize = 1;
        s.symmetry = set;
        doc.takeDirty();
    }

    void drag(ToolId tool, Vec2f from, Vec2f to, bool secondary = false) {
        tb.select(tool);
        tb.press(doc, at(from.x, from.y, false, secondary));
        tb.drag(doc, at(to.x, to.y, false, secondary));
        tb.release(doc, at(to.x, to.y, false, secondary));
    }

    // Moves the pencil through pixel centers one after the other, as separate drag events.
    void strokeThrough(const std::vector<std::pair<int, int>>& pixels) {
        tb.select(ToolId::Pencil);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            const PointerEvent ev = at(static_cast<float>(pixels[i].first) + 0.5f, static_cast<float>(pixels[i].second) + 0.5f);
            if (i == 0)
                tb.press(doc, ev);
            else
                tb.drag(doc, ev);
        }
        const auto& last = pixels.back();
        tb.release(doc, at(static_cast<float>(last.first) + 0.5f, static_cast<float>(last.second) + 0.5f));
    }

    // Makes edits made straight on the image part of the baseline the next gesture starts from.
    void settle() {
        doc.markDirty(IRect{0, 0, doc.image().width(), doc.image().height()});
        doc.commit();
    }
};

// The picture a symmetric tool must produce, built by hand: the unmirrored drawing plus its mirror images.
// Every non-background pixel of `plain` is copied to its mirrors (mirrors off the canvas are dropped).
std::vector<Rgba8> handMirrored(const Document& plain, const SymmetrySet& set) {
    const Image& img = plain.image();
    const SymmetryPlan plan = resolveSymmetry(set, img.width(), img.height());
    std::vector<Rgba8> out = pixelsOf(plain);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const Rgba8 p = img.get(x, y);
            if (p == kWhite)
                continue;
            for (std::size_t k = 1; k < plan.count(); ++k) {
                const PixelPos dst = plan.pixel(k, x, y);
                if (img.contains(dst.x, dst.y))
                    out[static_cast<std::size_t>(dst.y) * static_cast<std::size_t>(img.width()) + static_cast<std::size_t>(dst.x)] = p;
            }
        }
    return out;
}

std::vector<Rgba8> handMirrored(const Document& plain, const Mirror& m) {
    return handMirrored(plain, toSet(m));
}

// The mirror setups every tool is checked against: X, Y and both, with a center axis, an axis on a pixel edge
// and one through the middle of a pixel (the custom ones also make a stroke overlap its own mirror image).
std::vector<Mirror> mirrorSetups() {
    return {Mirror{true, false, kSymmetryCenter, kSymmetryCenter}, Mirror{false, true, kSymmetryCenter, kSymmetryCenter},
            Mirror{true, true, kSymmetryCenter, kSymmetryCenter},  Mirror{true, true, 14, 8},
            Mirror{true, true, 13, 9},                             Mirror{true, false, 13, kSymmetryCenter}};
}

// Draws with `draw` on a plain canvas and on a symmetric one of the same size, for every setup and for an even
// and an odd canvas: the mirrored result must be the hand-mirrored plain one. `draw` stays inside the canvas.
template <class Draw>
void checkSets(const std::vector<SymmetrySet>& sets, Draw&& draw) {
    for (const std::pair<int, int>& size : {std::pair<int, int>{20, 20}, std::pair<int, int>{21, 15}}) {
        for (const SymmetrySet& set : sets) {
            MirrorCanvas plain(size.first, size.second, SymmetrySet{});
            draw(plain);
            MirrorCanvas mirrored(size.first, size.second, set);
            draw(mirrored);
            CHECK(pixelsOf(mirrored.doc) == handMirrored(plain.doc, set));
        }
    }
}

template <class Draw>
void checkSetups(const std::vector<Mirror>& setups, Draw&& draw) {
    std::vector<SymmetrySet> sets;
    for (const Mirror& m : setups)
        sets.push_back(toSet(m));
    checkSets(sets, draw);
}

template <class Draw>
void checkMirrored(Draw&& draw) {
    checkSetups(mirrorSetups(), draw);
}

} // namespace

TEST_CASE("Mirrored pencil equals the hand-mirrored stroke") {
    checkMirrored([](MirrorCanvas& c) { c.drag(ToolId::Pencil, Vec2f{2.5f, 3.5f}, Vec2f{10.5f, 6.5f}); });
    checkMirrored([](MirrorCanvas& c) {
        c.tb.select(ToolId::Pencil);
        c.tb.press(c.doc, at(1.5f, 1.5f));
        c.tb.drag(c.doc, at(5.5f, 2.5f));
        c.tb.drag(c.doc, at(4.5f, 9.5f));
        c.tb.release(c.doc, at(8.5f, 11.5f));
    });
}

TEST_CASE("Mirrored pencil draws a click as every mirrored pixel") {
    MirrorCanvas c(20, 20, Mirror{true, true});
    c.tb.select(ToolId::Pencil);
    c.tb.press(c.doc, at(3.5f, 4.5f));
    c.tb.release(c.doc, at(3.5f, 4.5f));
    CHECK(count(c.doc.image(), kRed) == 4);
    CHECK(c.doc.image().get(3, 4) == kRed);
    CHECK(c.doc.image().get(16, 4) == kRed);
    CHECK(c.doc.image().get(3, 15) == kRed);
    CHECK(c.doc.image().get(16, 15) == kRed);
}

TEST_CASE("A pixel on an odd axis is painted once") {
    MirrorCanvas c(21, 21, Mirror{true, true});
    c.tb.select(ToolId::Pencil);
    c.tb.press(c.doc, at(10.5f, 10.5f)); // the middle pixel is its own mirror on both axes
    c.tb.release(c.doc, at(10.5f, 10.5f));
    CHECK(count(c.doc.image(), kRed) == 1);

    MirrorCanvas t(21, 21, Mirror{true, true});
    t.tb.settings().primary = Rgba8{0, 0, 0, 128};
    t.tb.select(ToolId::Pencil);
    t.tb.press(t.doc, at(10.5f, 10.5f));
    t.tb.release(t.doc, at(10.5f, 10.5f));
    CHECK(t.doc.image().get(10, 10) == blendOver(kWhite, Rgba8{0, 0, 0, 128})); // blended once, not four times
}

TEST_CASE("Mirrored round and square brushes equal the hand-mirrored stroke") {
    for (const bool square : {false, true}) {
        for (const int size : {1, 2, 3, 4, 5}) {
            checkMirrored([&](MirrorCanvas& c) {
                c.tb.settings().squareBrush = square;
                c.tb.settings().brushSize = size;
                c.drag(ToolId::Brush, Vec2f{4.5f, 5.5f}, Vec2f{9.5f, 8.5f});
            });
        }
    }
}

TEST_CASE("A mirrored even square stamp covers exactly the mirror image of the original") {
    // Size 4 is anchored two pixels left of and above the pixel, so the mirrored stamp must not be a plain
    // stamp on the mirrored pixel (which would sit one pixel off).
    MirrorCanvas c(20, 20, Mirror{true, false});
    c.tb.settings().squareBrush = true;
    c.tb.settings().brushSize = 4;
    c.tb.select(ToolId::Brush);
    c.tb.press(c.doc, at(5.5f, 5.5f)); // covers x 3..6, y 3..6
    c.tb.release(c.doc, at(5.5f, 5.5f));
    CHECK(count(c.doc.image(), kRed) == 32);
    for (int y = 3; y <= 6; ++y)
        for (int x = 3; x <= 6; ++x) {
            CHECK(c.doc.image().get(x, y) == kRed);
            CHECK(c.doc.image().get(19 - x, y) == kRed); // x 13..16
        }
    CHECK(c.doc.image().get(17, 4) == kWhite);
    CHECK(c.doc.image().get(12, 4) == kWhite);
}

TEST_CASE("Mirrored round brush with antialiasing is symmetric within rounding") {
    MirrorCanvas c(20, 20, Mirror{true, true});
    c.tb.settings().antialias = true;
    c.tb.settings().brushSize = 4;
    c.drag(ToolId::Brush, Vec2f{3.2f, 4.7f}, Vec2f{7.9f, 6.1f});
    const Image& img = c.doc.image();
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) {
            const Rgba8 a = img.get(x, y);
            CHECK(std::abs(static_cast<int>(a.g) - img.get(19 - x, y).g) <= 2);
            CHECK(std::abs(static_cast<int>(a.g) - img.get(x, 19 - y).g) <= 2);
        }
    CHECK(img.get(5, 5) != kWhite);
}

TEST_CASE("Mirrored eraser equals the hand-mirrored stroke") {
    for (const bool square : {false, true}) {
        checkMirrored([&](MirrorCanvas& c) {
            c.tb.settings().squareBrush = square;
            c.tb.settings().brushSize = 3;
            c.tb.settings().eraseToTransparent = true;
            c.drag(ToolId::Eraser, Vec2f{3.5f, 4.5f}, Vec2f{8.5f, 4.5f});
        });
    }
}

TEST_CASE("A translucent stroke across the axis blends once per pixel") {
    const Rgba8 ink{0, 0, 0, 128};
    const Rgba8 expected = blendOver(kWhite, ink);
    for (const bool square : {false, true}) {
        MirrorCanvas c(20, 20, Mirror{true, true});
        c.tb.settings().primary = ink;
        c.tb.settings().brushSize = 3;
        c.tb.settings().squareBrush = square;
        c.tb.select(ToolId::Brush);
        c.tb.press(c.doc, at(4.5f, 8.5f));
        for (int x = 5; x <= 15; ++x)
            c.tb.drag(c.doc, at(static_cast<float>(x) + 0.5f, 8.5f)); // crosses the vertical axis
        c.tb.release(c.doc, at(15.5f, 8.5f));
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 20; ++x) {
                const Rgba8 p = c.doc.image().get(x, y);
                CHECK((p == kWhite || p == expected));
            }
        CHECK(c.doc.image().get(10, 8) == expected);
        CHECK(c.doc.image().get(9, 8) == expected);
    }

    MirrorCanvas pencil(20, 20, Mirror{true, true});
    pencil.tb.settings().primary = ink;
    pencil.drag(ToolId::Pencil, Vec2f{2.5f, 8.5f}, Vec2f{17.5f, 8.5f}); // every pixel is also its mirror's pixel
    for (int x = 2; x <= 17; ++x) {
        CHECK(pencil.doc.image().get(x, 8) == expected);
        CHECK(pencil.doc.image().get(x, 11) == expected);
    }
    CHECK(count(pencil.doc.image(), expected) == 32);
}

TEST_CASE("A translucent pixel-perfect stroke across the axis blends once per pixel") {
    const Rgba8 ink{0, 0, 0, 128};
    const Rgba8 expected = blendOver(kWhite, ink);
    MirrorCanvas c(20, 20, Mirror{true, false});
    c.tb.settings().primary = ink;
    c.tb.settings().pixelPerfect = true;
    c.drag(ToolId::Pencil, Vec2f{2.5f, 5.5f}, Vec2f{17.5f, 5.5f});
    for (int x = 2; x <= 17; ++x)
        CHECK(c.doc.image().get(x, 5) == expected);
    CHECK(count(c.doc.image(), expected) == 16);
}

TEST_CASE("Mirrored line equals the hand-mirrored line") {
    checkMirrored([](MirrorCanvas& c) { c.drag(ToolId::Line, Vec2f{2.5f, 3.5f}, Vec2f{10.5f, 6.5f}); });
    checkMirrored([](MirrorCanvas& c) { c.drag(ToolId::Line, Vec2f{1.5f, 1.5f}, Vec2f{9.5f, 12.5f}); });
    checkMirrored([](MirrorCanvas& c) {
        c.tb.settings().strokeWidth = 3;
        c.drag(ToolId::Line, Vec2f{2.5f, 3.5f}, Vec2f{10.5f, 5.5f});
    });
}

TEST_CASE("Mirrored rectangle equals the hand-mirrored rectangle in every shape mode") {
    for (const ShapeMode mode : {ShapeMode::Outline, ShapeMode::Filled, ShapeMode::Both}) {
        for (const int width : {1, 2}) {
            checkMirrored([&](MirrorCanvas& c) {
                c.tb.settings().shapeMode = mode;
                c.tb.settings().strokeWidth = width;
                c.tb.settings().secondary = kRed; // one color, so copies that overlap still compare exactly
                c.drag(ToolId::Rectangle, Vec2f{2.5f, 3.5f}, Vec2f{7.5f, 9.5f});
            });
        }
    }
}

TEST_CASE("Mirrored ellipse equals the hand-mirrored ellipse in every shape mode") {
    for (const ShapeMode mode : {ShapeMode::Outline, ShapeMode::Filled, ShapeMode::Both}) {
        for (const int width : {1, 2}) {
            checkMirrored([&](MirrorCanvas& c) {
                c.tb.settings().shapeMode = mode;
                c.tb.settings().strokeWidth = width;
                c.tb.settings().secondary = kRed;
                c.drag(ToolId::Ellipse, Vec2f{1.5f, 2.5f}, Vec2f{8.5f, 10.5f});
            });
        }
    }
}

TEST_CASE("Both-mode shapes keep their own outline and fill colors in every copy") {
    for (const ToolId tool : {ToolId::Rectangle, ToolId::Ellipse}) {
        MirrorCanvas plain(20, 20, Mirror{});
        plain.tb.settings().shapeMode = ShapeMode::Both;
        plain.tb.settings().strokeWidth = 2;
        plain.drag(tool, Vec2f{2.5f, 3.5f}, Vec2f{7.5f, 8.5f});
        CHECK(count(plain.doc.image(), kGreen) > 0);

        const Mirror m{true, true};
        MirrorCanvas mirrored(20, 20, m);
        mirrored.tb.settings().shapeMode = ShapeMode::Both;
        mirrored.tb.settings().strokeWidth = 2;
        mirrored.drag(tool, Vec2f{2.5f, 3.5f}, Vec2f{7.5f, 8.5f});
        CHECK(pixelsOf(mirrored.doc) == handMirrored(plain.doc, m));
        CHECK(count(mirrored.doc.image(), kGreen) == 4 * count(plain.doc.image(), kGreen));
    }
}

TEST_CASE("A Both-mode shape keeps every outline over the fills of the other copies") {
    // The copies overlap across the axis; one copy's fill must not cover the other's outline.
    MirrorCanvas c(20, 20, Mirror{true, false});
    c.tb.settings().shapeMode = ShapeMode::Both;
    c.tb.settings().strokeWidth = 1;
    c.drag(ToolId::Rectangle, Vec2f{4.5f, 4.5f}, Vec2f{12.5f, 10.5f}); // x 4..12; its mirror is x 7..15
    CHECK(c.doc.image().get(4, 7) == kRed);
    CHECK(c.doc.image().get(12, 7) == kRed); // the original's right edge, inside the mirror's fill
    CHECK(c.doc.image().get(7, 7) == kRed);  // the mirror's left edge, inside the original's fill
    CHECK(c.doc.image().get(15, 7) == kRed);
    CHECK(c.doc.image().get(9, 7) == kGreen);
}

TEST_CASE("Cancelling a mirrored shape restores the image exactly") {
    for (const ToolId tool : {ToolId::Line, ToolId::Rectangle, ToolId::Ellipse}) {
        MirrorCanvas c(20, 20, Mirror{true, true, 13, 9});
        c.doc.fillTestPattern();
        c.doc.takeDirty();
        const std::vector<Rgba8> before = pixelsOf(c.doc);

        c.tb.settings().shapeMode = ShapeMode::Both;
        c.tb.select(tool);
        c.tb.press(c.doc, at(2.5f, 2.5f));
        c.tb.drag(c.doc, at(6.5f, 5.5f));
        c.tb.drag(c.doc, at(11.5f, 8.5f)); // a second preview over the first
        CHECK(pixelsOf(c.doc) != before);
        c.tb.cancel(c.doc);
        CHECK(pixelsOf(c.doc) == before);
        CHECK_FALSE(c.tb.gestureActive());
    }
}

TEST_CASE("A shrinking mirrored shape preview leaves nothing behind") {
    MirrorCanvas c(20, 20, Mirror{true, true});
    c.tb.select(ToolId::Rectangle);
    c.tb.settings().shapeMode = ShapeMode::Filled;
    c.tb.press(c.doc, at(2.5f, 2.5f));
    c.tb.drag(c.doc, at(8.5f, 8.5f));
    c.tb.drag(c.doc, at(3.5f, 3.5f));
    c.tb.release(c.doc, at(3.5f, 3.5f));
    CHECK(count(c.doc.image(), kRed) == 4 * 4); // 2x2 plus its three mirrors
}

TEST_CASE("Pixel-perfect pencil with symmetry mirrors the corner removal") {
    MirrorCanvas c(20, 20, Mirror{true, false});
    c.tb.settings().pixelPerfect = true;
    c.strokeThrough({{2, 2}, {3, 2}, {3, 3}});
    CHECK(count(c.doc.image(), kRed) == 4);
    CHECK(c.doc.image().get(3, 2) == kWhite);
    CHECK(c.doc.image().get(16, 2) == kWhite); // the mirrored corner went too
    CHECK(c.doc.image().get(2, 2) == kRed);
    CHECK(c.doc.image().get(3, 3) == kRed);
    CHECK(c.doc.image().get(17, 2) == kRed);
    CHECK(c.doc.image().get(16, 3) == kRed);
}

TEST_CASE("Pixel-perfect pencil never undoes a pixel another copy painted") {
    // The axis at x = 5 puts the original's corner (5, 2) where the mirrored copy starts, and the copy's corner
    // (4, 2) where the original starts. Both are legitimately painted by the other side.
    MirrorCanvas c(20, 20, Mirror{true, false, 10, kSymmetryCenter});
    c.tb.settings().pixelPerfect = true;
    c.strokeThrough({{4, 2}, {5, 2}, {5, 3}});
    CHECK(count(c.doc.image(), kRed) == 4);
    CHECK(c.doc.image().get(4, 2) == kRed);
    CHECK(c.doc.image().get(5, 2) == kRed);
    CHECK(c.doc.image().get(4, 3) == kRed);
    CHECK(c.doc.image().get(5, 3) == kRed);
}

TEST_CASE("Pixel-perfect pencil with symmetry gives back what was under a removed corner") {
    MirrorCanvas c(20, 20, Mirror{true, true});
    c.tb.settings().pixelPerfect = true;
    c.doc.image().set(3, 2, kBlue);
    c.doc.image().set(16, 17, kBlue);
    c.settle();
    c.strokeThrough({{2, 2}, {3, 2}, {3, 3}});
    CHECK(c.doc.image().get(3, 2) == kBlue); // restored, not erased to white
    CHECK(c.doc.image().get(16, 17) == kBlue);
    CHECK(c.doc.image().get(16, 2) == kWhite);
    CHECK(count(c.doc.image(), kRed) == 8);
}

TEST_CASE("Pixel-perfect pencil with symmetry matches the union of the copies on a long wiggle") {
    const std::vector<std::pair<int, int>> path{{1, 1}, {2, 1}, {2, 2}, {3, 2}, {3, 3}, {4, 3}, {5, 3}, {5, 4},
                                                {6, 4}, {6, 5}, {7, 5}, {8, 5}, {8, 6}, {9, 6}, {9, 7}};
    for (const Mirror& m : mirrorSetups()) {
        MirrorCanvas plain(20, 20, Mirror{});
        plain.tb.settings().pixelPerfect = true;
        plain.strokeThrough(path);
        MirrorCanvas mirrored(20, 20, m);
        mirrored.tb.settings().pixelPerfect = true;
        mirrored.strokeThrough(path);
        CHECK(pixelsOf(mirrored.doc) == handMirrored(plain.doc, m));
    }
}

TEST_CASE("Fill floods every mirrored seed") {
    MirrorCanvas c(20, 20, Mirror{true, false});
    // A closed 5x5 black box on each side (the right one is the left one mirrored by hand).
    for (int y = 2; y <= 6; ++y)
        for (int x = 2; x <= 6; ++x)
            if (x == 2 || x == 6 || y == 2 || y == 6) {
                c.doc.image().set(x, y, kBlack);
                c.doc.image().set(19 - x, y, kBlack);
            }
    c.settle();
    c.tb.settings().primary = kBlue;
    c.tb.select(ToolId::Fill);
    c.tb.press(c.doc, at(4.5f, 4.5f));
    c.tb.release(c.doc, at(4.5f, 4.5f));
    CHECK(count(c.doc.image(), kBlue) == 2 * 3 * 3);
    CHECK(c.doc.image().get(15, 4) == kBlue);
    CHECK(c.doc.image().get(10, 10) == kWhite); // the outside is untouched
}

TEST_CASE("Fill with symmetry on a single region fills it once") {
    MirrorCanvas c(20, 20, Mirror{true, true});
    c.tb.settings().primary = kBlue;
    c.tb.select(ToolId::Fill);
    c.tb.press(c.doc, at(3.5f, 3.5f));
    c.tb.release(c.doc, at(3.5f, 3.5f));
    CHECK(count(c.doc.image(), kBlue) == 400);
    CHECK(c.doc.takeDirty() == IRect{0, 0, 20, 20});
}

TEST_CASE("Fill skips a seed that falls off the image and uses the mirrored one that does not") {
    // The axis at x = 5: a click at pixel x = -2 mirrors to pixel 11 (10 - (-2) - 1).
    MirrorCanvas c(20, 20, Mirror{true, false, 10, kSymmetryCenter});
    for (int y = 0; y < 20; ++y)
        c.doc.image().set(7, y, kBlack); // a wall: the left part is not connected to x = 11
    c.settle();
    c.tb.settings().primary = kBlue;
    c.tb.select(ToolId::Fill);
    c.tb.press(c.doc, at(-1.5f, 4.5f));
    c.tb.release(c.doc, at(-1.5f, 4.5f));
    CHECK(c.doc.image().get(11, 4) == kBlue);
    CHECK(c.doc.image().get(15, 9) == kBlue);
    CHECK(c.doc.image().get(2, 4) == kWhite);

    MirrorCanvas off(20, 20, Mirror{true, true});
    off.tb.settings().primary = kBlue;
    off.tb.select(ToolId::Fill);
    off.tb.press(off.doc, at(-50.5f, 4.5f)); // every mirror of the seed is also off the image
    off.tb.release(off.doc, at(-50.5f, 4.5f));
    CHECK(count(off.doc.image(), kBlue) == 0);
}

TEST_CASE("Global fill with symmetry is a single pass") {
    MirrorCanvas c(20, 20, Mirror{true, true});
    c.doc.image().set(3, 4, Rgba8{250, 250, 250, 255});
    c.doc.image().set(16, 4, kBlack);
    c.settle();
    c.tb.settings().fillGlobal = true;
    c.tb.settings().fillTolerance = 10;
    c.tb.settings().primary = kGreen;
    c.tb.select(ToolId::Fill);
    c.tb.press(c.doc, at(0.5f, 0.5f));
    c.tb.release(c.doc, at(0.5f, 0.5f));
    CHECK(c.doc.image().get(3, 4) == kGreen);
    CHECK(c.doc.image().get(16, 4) == kBlack);
    CHECK(count(c.doc.image(), kGreen) == 399);
}

TEST_CASE("A tolerance fill does not re-flood from a seed an earlier copy already filled") {
    // The axis at x = 5 mirrors the click at pixel 2 to pixel 7, in the same region. The wall at x = 9 is
    // close to the yellow fill color but far from the white region, so a second flood that compared against
    // the new yellow would swallow it.
    const Rgba8 wall{250, 250, 5, 255};
    const Rgba8 yellow{255, 255, 0, 255};
    MirrorCanvas c(20, 20, Mirror{true, false, 10, kSymmetryCenter});
    for (int y = 0; y < 20; ++y)
        c.doc.image().set(9, y, wall);
    c.settle();
    c.tb.settings().fillTolerance = 10;
    c.tb.settings().primary = yellow;
    c.tb.select(ToolId::Fill);
    c.tb.press(c.doc, at(2.5f, 2.5f));
    c.tb.release(c.doc, at(2.5f, 2.5f));
    CHECK(c.doc.image().get(9, 5) == wall);
    CHECK(c.doc.image().get(0, 0) == yellow);
    CHECK(c.doc.image().get(8, 19) == yellow);
    CHECK(c.doc.image().get(15, 5) == kWhite); // the right region was never a seed
}

TEST_CASE("The picker ignores symmetry") {
    MirrorCanvas c(20, 20, Mirror{true, true});
    c.doc.image().set(3, 3, kBlue);
    c.settle();
    c.tb.select(ToolId::Picker);
    c.tb.press(c.doc, at(3.5f, 3.5f));
    c.tb.release(c.doc, at(3.5f, 3.5f));
    CHECK(c.tb.settings().primary == kBlue);
    CHECK(count(c.doc.image(), kBlue) == 1);
}

TEST_CASE("With the axes set but symmetry off every tool draws the same as before") {
    for (const ToolId tool : {ToolId::Pencil, ToolId::Brush, ToolId::Eraser, ToolId::Line, ToolId::Rectangle,
                              ToolId::Ellipse}) {
        MirrorCanvas plain(20, 20, Mirror{});
        MirrorCanvas off(20, 20, Mirror{false, false, 7, 9});
        plain.drag(tool, Vec2f{2.5f, 3.5f}, Vec2f{9.5f, 7.5f});
        off.drag(tool, Vec2f{2.5f, 3.5f}, Vec2f{9.5f, 7.5f});
        CHECK(pixelsOf(off.doc) == pixelsOf(plain.doc));
    }
}

TEST_CASE("Toggling symmetry in the middle of a gesture does not split it") {
    // A stroke that started mirrored stays mirrored; one that started plain stays plain.
    MirrorCanvas on(20, 20, Mirror{true, false});
    on.tb.select(ToolId::Pencil);
    on.tb.press(on.doc, at(2.5f, 2.5f));
    on.tb.settings().symmetry = SymmetrySet{};
    on.tb.drag(on.doc, at(6.5f, 2.5f));
    on.tb.release(on.doc, at(6.5f, 2.5f));
    CHECK(count(on.doc.image(), kRed) == 10);

    MirrorCanvas off(20, 20, Mirror{});
    off.tb.select(ToolId::Rectangle);
    off.tb.press(off.doc, at(2.5f, 2.5f));
    off.tb.drag(off.doc, at(5.5f, 5.5f));
    off.tb.settings().symmetry = toSet(Mirror{true, false});
    off.tb.drag(off.doc, at(6.5f, 6.5f));
    off.tb.release(off.doc, at(6.5f, 6.5f));
    CHECK(off.doc.image().get(17, 2) == kWhite);
    CHECK(off.doc.image().get(6, 6) == kRed);
}

TEST_CASE("A mirrored stroke is one undo step") {
    MirrorCanvas c(20, 20, Mirror{true, true});
    c.drag(ToolId::Brush, Vec2f{3.5f, 3.5f}, Vec2f{8.5f, 6.5f});
    CHECK(count(c.doc.image(), kRed) > 0);
    c.doc.undo();
    CHECK(count(c.doc.image(), kRed) == 0);
}

TEST_CASE("Mirrored tools survive hostile coordinates") {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const ToolId tool : {ToolId::Pencil, ToolId::Brush, ToolId::Eraser, ToolId::Line, ToolId::Rectangle,
                              ToolId::Ellipse, ToolId::Fill}) {
        for (const int variant : {0, 1}) {
            const bool brushLike = tool == ToolId::Pencil || tool == ToolId::Brush || tool == ToolId::Eraser;
            if (variant == 1 && !brushLike)
                continue;
            MirrorCanvas c(20, 20, Mirror{true, true, 13, 9});
            c.tb.settings().pixelPerfect = variant == 1; // the pencil's corner trails
            c.tb.settings().squareBrush = variant == 1;  // the brushes' rectangle stamps
            c.tb.settings().brushSize = 16;
            c.tb.settings().strokeWidth = 16;
            c.drag(tool, Vec2f{1.0e30f, -1.0e30f}, Vec2f{nan, inf});
            c.drag(tool, Vec2f{-inf, 5.5f}, Vec2f{2.5f, 2.0e9f});
            c.drag(tool, Vec2f{3.0e6f, 3.0e6f}, Vec2f{-3.0e6f, 4.5f});
        }
    }
    CHECK(true); // reaching here without a crash or sanitizer report is the check
}

// --- rotational drawing --------------------------------------------------------------------------

namespace {

// Setups whose turns are all quarter turns, so the result is exactly the hand-rotated picture. Every axis pair has
// one parity (even/even or odd/odd), as does the center of both canvas sizes checkSetups uses (20x20 and 21x15).
std::vector<Mirror> rotationSetups() {
    return {Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 2}, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 4},
            Mirror{false, false, 14, 8, 4},                            Mirror{false, false, 13, 9, 4},
            Mirror{false, false, 13, 9, 2},                            Mirror{true, false, kSymmetryCenter, kSymmetryCenter, 2},
            Mirror{true, true, kSymmetryCenter, kSymmetryCenter, 2},   Mirror{true, false, 14, 8, 4},
            Mirror{false, true, 13, 9, 4},                             Mirror{true, true, 13, 9, 4}};
}

// Number of 8-connected groups of pixels that differ from the white background.
int componentCount(const Image& img) {
    const int w = img.width();
    const int h = img.height();
    std::vector<char> seen(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    int groups = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (img.get(x, y) == kWhite || seen[static_cast<std::size_t>(y) * w + x])
                continue;
            ++groups;
            std::vector<std::pair<int, int>> stack{{x, y}};
            seen[static_cast<std::size_t>(y) * w + x] = 1;
            while (!stack.empty()) {
                const auto [cx, cy] = stack.back();
                stack.pop_back();
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = cx + dx;
                        const int ny = cy + dy;
                        if (!img.contains(nx, ny) || img.get(nx, ny) == kWhite || seen[static_cast<std::size_t>(ny) * w + nx])
                            continue;
                        seen[static_cast<std::size_t>(ny) * w + nx] = 1;
                        stack.push_back({nx, ny});
                    }
            }
        }
    return groups;
}

// True when the picture is unchanged by copy `k` of the plan (every pixel equals the one it maps to).
bool invariantUnder(const Document& doc, const SymmetryPlan& plan, std::size_t k) {
    const Image& img = doc.image();
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const PixelPos p = plan.pixel(k, x, y);
            if (!img.contains(p.x, p.y) || img.get(p.x, p.y) != img.get(x, y))
                return false;
        }
    return true;
}

// A gentle stroke on a 41x41 canvas (center pixel 20): four points 9 to 14 pixels from the center, turning 9
// degrees clockwise from `startDeg`. Copies of it stay apart for the rotation counts and mirrors the tests use,
// as long as `startDeg` keeps it clear of the lines the mirrors would reflect it across.
void orbitStroke(MirrorCanvas& c, ToolId tool, double startDeg = -20.0) {
    std::vector<Vec2f> path;
    for (int i = 0; i < 4; ++i) {
        const double angle = (startDeg + 3.0 * i) * 3.141592653589793 / 180.0;
        const double radius = 9.0 + 1.7 * i;
        path.push_back(Vec2f{static_cast<float>(20.5 + radius * std::cos(angle)), static_cast<float>(20.5 + radius * std::sin(angle))});
    }
    c.tb.select(tool);
    c.tb.press(c.doc, at(path[0].x, path[0].y));
    for (std::size_t i = 1; i < path.size(); ++i)
        c.tb.drag(c.doc, at(path[i].x, path[i].y));
    c.tb.release(c.doc, at(path.back().x, path.back().y));
}

// Every tool setup the orbit tests run: pencil (plain and pixel-perfect), round and square brushes, line.
struct OrbitTool {
    ToolId tool;
    bool pixelPerfect = false;
    bool square = false;
    int size = 1;
};
std::vector<OrbitTool> orbitTools() {
    return {{ToolId::Pencil},        {ToolId::Pencil, true}, {ToolId::Brush, false, false, 1}, {ToolId::Brush, false, false, 3},
            {ToolId::Brush, false, true, 3}, {ToolId::Brush, false, true, 2}, {ToolId::Line, false, false, 1},
            {ToolId::Line, false, false, 3}};
}
void configure(MirrorCanvas& c, const OrbitTool& t) {
    c.tb.settings().pixelPerfect = t.pixelPerfect;
    c.tb.settings().squareBrush = t.square;
    c.tb.settings().brushSize = t.size;
    c.tb.settings().strokeWidth = t.size;
}

} // namespace

TEST_CASE("Rotation turns the drawing about the point where the axes cross, with the mirrors off") {
    // Axes at (14, 8) half pixels: the center is (7, 4). The pixel (3, 4) has its center at (3.5, 4.5).
    MirrorCanvas c(20, 20, Mirror{false, false, 14, 8, 4});
    c.tb.select(ToolId::Pencil);
    c.tb.press(c.doc, at(3.5f, 4.5f));
    c.tb.release(c.doc, at(3.5f, 4.5f));
    CHECK(count(c.doc.image(), kRed) == 4);
    CHECK(c.doc.image().get(3, 4) == kRed);
    CHECK(c.doc.image().get(6, 0) == kRed); // a quarter turn clockwise: left of the center goes to above it
    CHECK(c.doc.image().get(10, 3) == kRed);
    CHECK(c.doc.image().get(7, 7) == kRed);
}

TEST_CASE("Rotated pencil equals the hand-rotated stroke") {
    for (const bool perfect : {false, true}) {
        checkSetups(rotationSetups(), [&](MirrorCanvas& c) {
            c.tb.settings().pixelPerfect = perfect;
            c.drag(ToolId::Pencil, Vec2f{2.5f, 3.5f}, Vec2f{10.5f, 6.5f});
        });
        checkSetups(rotationSetups(), [&](MirrorCanvas& c) {
            c.tb.settings().pixelPerfect = perfect;
            c.tb.select(ToolId::Pencil);
            c.tb.press(c.doc, at(1.5f, 1.5f));
            c.tb.drag(c.doc, at(5.5f, 2.5f));
            c.tb.drag(c.doc, at(4.5f, 9.5f));
            c.tb.release(c.doc, at(8.5f, 11.5f));
        });
    }
}

TEST_CASE("Rotated round and square brushes and eraser equal the hand-rotated stroke") {
    for (const bool square : {false, true}) {
        for (const int size : {1, 2, 3, 4, 5}) {
            checkSetups(rotationSetups(), [&](MirrorCanvas& c) {
                c.tb.settings().squareBrush = square;
                c.tb.settings().brushSize = size;
                c.drag(ToolId::Brush, Vec2f{4.5f, 5.5f}, Vec2f{9.5f, 8.5f});
            });
        }
        checkSetups(rotationSetups(), [&](MirrorCanvas& c) {
            c.tb.settings().squareBrush = square;
            c.tb.settings().brushSize = 3;
            c.tb.settings().eraseToTransparent = true;
            c.drag(ToolId::Eraser, Vec2f{3.5f, 4.5f}, Vec2f{8.5f, 4.5f});
        });
    }
}

TEST_CASE("Rotated line, rectangle and ellipse equal the hand-rotated shape") {
    checkSetups(rotationSetups(), [](MirrorCanvas& c) { c.drag(ToolId::Line, Vec2f{2.5f, 3.5f}, Vec2f{10.5f, 6.5f}); });
    checkSetups(rotationSetups(), [](MirrorCanvas& c) {
        c.tb.settings().strokeWidth = 3;
        c.drag(ToolId::Line, Vec2f{2.5f, 3.5f}, Vec2f{10.5f, 5.5f});
    });
    for (const ShapeMode mode : {ShapeMode::Outline, ShapeMode::Filled, ShapeMode::Both}) {
        for (const int width : {1, 2}) {
            for (const ToolId tool : {ToolId::Rectangle, ToolId::Ellipse}) {
                checkSetups(rotationSetups(), [&](MirrorCanvas& c) {
                    c.tb.settings().shapeMode = mode;
                    c.tb.settings().strokeWidth = width;
                    c.tb.settings().secondary = kRed;
                    c.drag(tool, Vec2f{2.5f, 3.5f}, Vec2f{8.5f, 6.5f}); // wider than tall, so a quarter turn swaps
                });
            }
        }
    }
}

TEST_CASE("Rotated antialiased brush is symmetric within rounding") {
    MirrorCanvas c(20, 20, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 2});
    c.tb.settings().antialias = true;
    c.tb.settings().brushSize = 4;
    c.drag(ToolId::Brush, Vec2f{3.2f, 4.7f}, Vec2f{7.9f, 6.1f});
    const Image& img = c.doc.image();
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x)
            CHECK(std::abs(static_cast<int>(img.get(x, y).g) - img.get(19 - x, 19 - y).g) <= 2);
    CHECK(img.get(5, 5) != kWhite);
}

TEST_CASE("A rotated hard brush stamp is a whole number of pixels per copy") {
    // A 3x3 square stamp at pixel (5, 6) on a 20x20 canvas: the four quarter copies are 3x3 blocks.
    MirrorCanvas c(20, 20, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 4});
    c.tb.settings().squareBrush = true;
    c.tb.settings().brushSize = 3;
    c.tb.select(ToolId::Brush);
    c.tb.press(c.doc, at(5.5f, 6.5f));
    c.tb.release(c.doc, at(5.5f, 6.5f));
    CHECK(count(c.doc.image(), kRed) == 36);
    CHECK(componentCount(c.doc.image()) == 4);
}

TEST_CASE("Rotated fill floods every rotated seed") {
    struct Case {
        int w;
        int h;
        Mirror m;
        IRect box; // the outline of a closed box (pixels) drawn in every copy
    };
    const std::vector<Case> cases{
        {20, 20, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 4}, IRect{3, 3, 8, 8}},
        {20, 20, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 2}, IRect{3, 3, 8, 8}},
        {21, 15, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 4}, IRect{3, 3, 8, 8}},
        {21, 15, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 2}, IRect{3, 3, 8, 8}},
        {20, 20, Mirror{false, false, 14, 16, 4}, IRect{9, 6, 14, 11}},
        {20, 20, Mirror{false, false, 13, 17, 4}, IRect{9, 7, 13, 11}},
        {20, 20, Mirror{true, false, kSymmetryCenter, kSymmetryCenter, 2}, IRect{3, 3, 8, 8}},
        {20, 20, Mirror{true, true, kSymmetryCenter, kSymmetryCenter, 2}, IRect{3, 3, 8, 8}},
    };
    for (const Case& cs : cases) {
        MirrorCanvas c(cs.w, cs.h, cs.m);
        const SymmetryPlan plan = resolveSymmetry(toSet(cs.m), cs.w, cs.h);
        std::set<std::pair<int, int>> walls;
        std::set<std::pair<int, int>> inside;
        for (std::size_t k = 0; k < plan.count(); ++k)
            for (int y = cs.box.y0; y < cs.box.y1; ++y)
                for (int x = cs.box.x0; x < cs.box.x1; ++x) {
                    const PixelPos p = plan.pixel(k, x, y);
                    REQUIRE(c.doc.image().contains(p.x, p.y));
                    const bool edge = x == cs.box.x0 || x == cs.box.x1 - 1 || y == cs.box.y0 || y == cs.box.y1 - 1;
                    (edge ? walls : inside).insert({p.x, p.y});
                }
        for (const auto& w : walls)
            REQUIRE(inside.count(w) == 0); // the copies do not overlap
        for (const auto& w : walls)
            c.doc.image().set(w.first, w.second, kBlack);
        c.settle();
        c.tb.settings().primary = kBlue;
        c.tb.select(ToolId::Fill);
        const float sx = static_cast<float>(cs.box.x0 + 1) + 0.5f;
        const float sy = static_cast<float>(cs.box.y0 + 1) + 0.5f;
        c.tb.press(c.doc, at(sx, sy));
        c.tb.release(c.doc, at(sx, sy));
        CHECK(count(c.doc.image(), kBlue) == static_cast<int>(inside.size()));
        CHECK(count(c.doc.image(), kBlack) == static_cast<int>(walls.size()));
        for (const auto& p : inside)
            CHECK(c.doc.image().get(p.first, p.second) == kBlue);
    }
}

TEST_CASE("Fill with 3, 6 and 8 copies floods the room each copy of the click lands in") {
    for (const int n : {3, 6, 8}) {
        for (const int size : {41, 40}) {
            MirrorCanvas c(size, size, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, n});
            const SymmetryPlan plan = resolveSymmetry(toSet(Mirror{false, false, kSymmetryCenter, kSymmetryCenter, n}), size, size);
            // A black canvas with a 3x3 white room around every copy of the pixel (28, 21).
            Image& img = c.doc.image();
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    img.set(x, y, kBlack);
            std::set<std::pair<int, int>> rooms;
            for (std::size_t k = 0; k < plan.count(); ++k) {
                const PixelPos p = plan.pixel(k, 28, 21);
                REQUIRE(img.contains(p.x, p.y));
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (img.contains(p.x + dx, p.y + dy)) {
                            img.set(p.x + dx, p.y + dy, kWhite);
                            rooms.insert({p.x + dx, p.y + dy});
                        }
            }
            c.settle();
            c.tb.settings().primary = kBlue;
            c.tb.select(ToolId::Fill);
            c.tb.press(c.doc, at(28.5f, 21.5f));
            c.tb.release(c.doc, at(28.5f, 21.5f));
            CHECK(count(img, kBlue) == static_cast<int>(rooms.size()));
            CHECK(count(img, kWhite) == 0);
            CHECK(count(img, kBlack) == size * size - static_cast<int>(rooms.size()));
        }
    }
}

TEST_CASE("Fill with rotation on a single region fills it once") {
    for (const int n : {2, 3, 4, 6, 8}) {
        MirrorCanvas c(20, 20, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, n});
        c.tb.settings().primary = kBlue;
        c.tb.select(ToolId::Fill);
        c.tb.press(c.doc, at(3.5f, 3.5f));
        c.tb.release(c.doc, at(3.5f, 3.5f));
        CHECK(count(c.doc.image(), kBlue) == 400);
        CHECK(c.doc.takeDirty() == IRect{0, 0, 20, 20});
    }
}

TEST_CASE("Fill with rotation floods each room a copy of the click lands in, and skips seeds off the image") {
    // Axes at (14, 8): the center is (7, 4). Walls on column 9 and row 6 split the canvas in four rooms.
    MirrorCanvas c(20, 20, Mirror{false, false, 14, 8, 4});
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x)
            if (x == 9 || y == 6)
                c.doc.image().set(x, y, kBlack);
    c.settle();
    c.tb.settings().primary = kBlue;
    c.tb.select(ToolId::Fill);
    c.tb.press(c.doc, at(3.5f, 4.5f)); // top left room; its copies are (6, 0), (10, 3) and (7, 7)
    c.tb.release(c.doc, at(3.5f, 4.5f));
    CHECK(c.doc.image().get(3, 4) == kBlue);
    CHECK(c.doc.image().get(6, 0) == kBlue);
    CHECK(c.doc.image().get(10, 3) == kBlue);
    CHECK(c.doc.image().get(7, 7) == kBlue);
    CHECK(count(c.doc.image(), kBlue) == 9 * 6 + 10 * 6 + 9 * 13); // top left, top right and bottom left
    CHECK(c.doc.image().get(15, 15) == kWhite);                      // the bottom right room has no seed
    CHECK(count(c.doc.image(), kBlack) == 39);

    MirrorCanvas off(20, 20, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 3});
    off.tb.settings().primary = kBlue;
    off.tb.select(ToolId::Fill);
    off.tb.press(off.doc, at(-50.5f, 4.5f)); // far off the image, and so are its copies
    off.tb.release(off.doc, at(-50.5f, 4.5f));
    CHECK(count(off.doc.image(), kBlue) == 0);
}

TEST_CASE("Rotation and mirrors multiply the copies") {
    // Strokes stay clear of the axes, so every copy is its own group of pixels. The 41x41 canvas has its center
    // on the pixel (20, 20).
    const struct {
        Mirror m;
        int copies;
        double startDeg;
    } cases[] = {
        {Mirror{true, false, kSymmetryCenter, kSymmetryCenter, 2}, 4, -20.0},
        {Mirror{true, false, kSymmetryCenter, kSymmetryCenter, 4}, 8, -20.0},
        {Mirror{false, true, kSymmetryCenter, kSymmetryCenter, 4}, 8, -20.0},
        {Mirror{true, false, kSymmetryCenter, kSymmetryCenter, 3}, 6, -4.0}, // near 0 degrees: far from the 3-fold mirror lines
        {Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 6}, 6, -20.0},
        {Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 8}, 8, -20.0},
    };
    for (const auto& cs : cases) {
        for (const OrbitTool& t : orbitTools()) {
            MirrorCanvas c(41, 41, cs.m);
            configure(c, t);
            orbitStroke(c, t.tool, cs.startDeg);
            INFO("rotations " << cs.m.rotations << " mirror " << cs.m.x << " tool " << static_cast<int>(t.tool) << " size "
                              << t.size);
            CHECK(componentCount(c.doc.image()) == cs.copies);
        }
    }
}

TEST_CASE("Every copy at 3, 6 and 8 rotations is one connected stroke") {
    for (const int n : {3, 6, 8}) {
        for (const OrbitTool& t : orbitTools()) {
            MirrorCanvas c(41, 41, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, n});
            configure(c, t);
            orbitStroke(c, t.tool);
            INFO("rotations " << n << " tool " << static_cast<int>(t.tool) << " size " << t.size << " square " << t.square
                              << " perfect " << t.pixelPerfect);
            CHECK(componentCount(c.doc.image()) == n);
            CHECK(count(c.doc.image(), kRed) > 0);
        }
    }
}

TEST_CASE("A thin straight line has no gaps in any copy, whatever its direction") {
    // A one pixel line is the hard case: rounding each rotated pixel of it would break it into pieces.
    for (const int n : {3, 6, 8}) {
        for (const double deg : {0.0, 7.0, 20.0, 33.0, 45.0, 63.0, 90.0, 118.0, 151.0}) {
            for (const OrbitTool& t : {OrbitTool{ToolId::Pencil}, OrbitTool{ToolId::Pencil, true}, OrbitTool{ToolId::Line}}) {
                MirrorCanvas c(41, 41, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, n});
                configure(c, t);
                const double a = deg * 3.141592653589793 / 180.0;
                const Vec2f from{static_cast<float>(20.5 + 7.0 * std::cos(a)), static_cast<float>(20.5 + 7.0 * std::sin(a))};
                const Vec2f to{static_cast<float>(20.5 + 18.0 * std::cos(a)), static_cast<float>(20.5 + 18.0 * std::sin(a))};
                c.drag(t.tool, from, to);
                INFO("rotations " << n << " angle " << deg << " tool " << static_cast<int>(t.tool));
                CHECK(componentCount(c.doc.image()) == n);
            }
        }
    }
}

TEST_CASE("A stroke dragged in many small steps is as connected as one long drag") {
    for (const int n : {3, 8}) {
        for (const ToolId tool : {ToolId::Pencil, ToolId::Brush}) {
            MirrorCanvas c(41, 41, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, n});
            c.tb.settings().brushSize = 2;
            c.tb.select(tool);
            c.tb.press(c.doc, at(28.5f, 16.5f));
            for (int i = 1; i <= 24; ++i)
                c.tb.drag(c.doc, at(28.5f + static_cast<float>(i) * 0.25f, 16.5f - static_cast<float>(i % 5) * 0.4f));
            c.tb.release(c.doc, at(34.5f, 16.5f));
            CHECK(componentCount(c.doc.image()) == n);
        }
    }
}

TEST_CASE("The picture is unchanged by the quarter turns of its copies") {
    const struct {
        int n;
        std::size_t turn; // the copy that is a quarter (or half) turn, at mirror 0
    } cases[] = {{8, 2}, {8, 4}, {8, 6}, {6, 3}, {4, 1}, {2, 1}};
    for (const auto& cs : cases) {
        for (const OrbitTool& t : orbitTools()) {
            MirrorCanvas c(41, 41, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, cs.n});
            configure(c, t);
            orbitStroke(c, t.tool);
            const SymmetryPlan plan = resolveSymmetry(toSet(Mirror{false, false, kSymmetryCenter, kSymmetryCenter, cs.n}), 41, 41);
            INFO("rotations " << cs.n << " turn " << cs.turn << " tool " << static_cast<int>(t.tool) << " size " << t.size
                              << " square " << t.square << " perfect " << t.pixelPerfect);
            CHECK(invariantUnder(c.doc, plan, cs.turn));
        }
    }
    // The same with the eraser over a filled canvas, and with a mirror.
    MirrorCanvas e(41, 41, Mirror{true, false, kSymmetryCenter, kSymmetryCenter, 8});
    e.tb.settings().eraseToTransparent = true;
    e.tb.settings().brushSize = 3;
    for (int y = 0; y < 41; ++y)
        for (int x = 0; x < 41; ++x)
            e.doc.image().set(x, y, kBlue);
    e.settle();
    orbitStroke(e, ToolId::Eraser);
    const SymmetryPlan mirrored = resolveSymmetry(toSet(Mirror{true, false, kSymmetryCenter, kSymmetryCenter, 8}), 41, 41);
    CHECK(count(e.doc.image(), kBlue) < 41 * 41);
    CHECK(invariantUnder(e.doc, mirrored, 4)); // turn 2 of the 2 mirror variants: a quarter turn
}

TEST_CASE("The copies of an eight-fold stroke are turned images of each other") {
    // Copy 3 (135 degrees) is copy 1 (45 degrees) turned a quarter, to the pixel.
    MirrorCanvas c(41, 41, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 8});
    c.tb.settings().brushSize = 3;
    orbitStroke(c, ToolId::Brush);
    const SymmetryPlan plan = resolveSymmetry(toSet(Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 8}), 41, 41);
    int checked = 0;
    for (int y = 0; y < 41; ++y)
        for (int x = 0; x < 41; ++x) {
            const PixelPos a = plan.pixel(2, x, y);
            CHECK(c.doc.image().get(x, y) == c.doc.image().get(a.x, a.y));
            ++checked;
        }
    CHECK(checked == 41 * 41);
}

TEST_CASE("A translucent stroke through the center blends once per pixel in every rotation") {
    const Rgba8 ink{0, 0, 0, 128};
    const Rgba8 expected = blendOver(kWhite, ink);
    for (const int n : {2, 3, 4, 6, 8}) {
        for (const OrbitTool& t : {OrbitTool{ToolId::Pencil}, OrbitTool{ToolId::Pencil, true}, OrbitTool{ToolId::Brush, false, false, 3},
                                   OrbitTool{ToolId::Brush, false, true, 3}}) {
            MirrorCanvas c(21, 21, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, n});
            c.tb.settings().primary = ink;
            configure(c, t);
            c.tb.select(t.tool);
            c.tb.press(c.doc, at(3.5f, 10.5f));
            for (int x = 4; x <= 17; ++x)
                c.tb.drag(c.doc, at(static_cast<float>(x) + 0.5f, 10.5f)); // through the center
            c.tb.release(c.doc, at(17.5f, 10.5f));
            int painted = 0;
            for (int y = 0; y < 21; ++y)
                for (int x = 0; x < 21; ++x) {
                    const Rgba8 p = c.doc.image().get(x, y);
                    CHECK((p == kWhite || p == expected));
                    painted += p == expected ? 1 : 0;
                }
            CHECK(c.doc.image().get(10, 10) == expected);
            CHECK(painted > 14);
        }
    }
}

TEST_CASE("A rotated square stamp and its copies composite once even when they overlap") {
    const Rgba8 ink{0, 0, 0, 128};
    MirrorCanvas c(21, 21, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 4});
    c.tb.settings().primary = ink;
    c.tb.settings().squareBrush = true;
    c.tb.settings().brushSize = 7;
    c.tb.select(ToolId::Brush);
    c.tb.press(c.doc, at(12.5f, 10.5f)); // 7x7 around (12, 10): the four copies overlap near the center
    c.tb.release(c.doc, at(12.5f, 10.5f));
    for (int y = 0; y < 21; ++y)
        for (int x = 0; x < 21; ++x) {
            const Rgba8 p = c.doc.image().get(x, y);
            CHECK((p == kWhite || p == blendOver(kWhite, ink)));
        }
}

TEST_CASE("Cancelling a rotated shape restores the image exactly") {
    for (const int n : {2, 3, 4, 6, 8}) {
        for (const ToolId tool : {ToolId::Line, ToolId::Rectangle, ToolId::Ellipse}) {
            for (const bool mirror : {false, true}) {
                MirrorCanvas c(20, 20, Mirror{mirror, mirror, 13, 9, n});
                c.doc.fillTestPattern();
                c.doc.takeDirty();
                const std::vector<Rgba8> before = pixelsOf(c.doc);

                c.tb.settings().shapeMode = ShapeMode::Both;
                c.tb.settings().strokeWidth = 2;
                c.tb.select(tool);
                c.tb.press(c.doc, at(2.5f, 2.5f));
                c.tb.drag(c.doc, at(6.5f, 5.5f));
                c.tb.drag(c.doc, at(11.5f, 8.5f)); // a second preview over the first
                CHECK(pixelsOf(c.doc) != before);
                c.tb.cancel(c.doc);
                CHECK(pixelsOf(c.doc) == before);
                CHECK_FALSE(c.tb.gestureActive());
            }
        }
    }
}

TEST_CASE("A shrinking rotated shape preview leaves nothing behind") {
    for (const int n : {3, 4, 6, 8}) {
        MirrorCanvas c(41, 41, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, n});
        c.tb.select(ToolId::Rectangle);
        c.tb.settings().shapeMode = ShapeMode::Filled;
        c.tb.press(c.doc, at(28.5f, 8.5f));
        c.tb.drag(c.doc, at(36.5f, 16.5f));
        c.tb.drag(c.doc, at(29.5f, 9.5f));
        c.tb.release(c.doc, at(29.5f, 9.5f));
        // Only the final 2x2 box (and its copies) remain; each copy is 2x2 or 4 pixels in a row of copies.
        CHECK(count(c.doc.image(), kRed) == 4 * n);
        CHECK(componentCount(c.doc.image()) == n);
    }
}

TEST_CASE("Rotated shapes keep their size in a quarter-turn copy swapped and in other copies unchanged") {
    // A 6x2 filled rectangle: copies at 90 and 270 degrees are 2x6, and the other angles keep the box size
    // (in the copy at 135 degrees it is swapped on top of the 45 degree copy, as that copy is a quarter turn of it).
    MirrorCanvas c(41, 41, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 8});
    c.tb.settings().shapeMode = ShapeMode::Filled;
    c.drag(ToolId::Rectangle, Vec2f{28.5f, 8.5f}, Vec2f{33.5f, 9.5f}); // x 28..33, y 8..9: 6 wide, 2 tall
    CHECK(count(c.doc.image(), kRed) == 8 * 12);
    CHECK(componentCount(c.doc.image()) == 8);
}

TEST_CASE("Rotations of 1 and invalid counts draw the same as no symmetry") {
    for (const int rotations : {1, 0, -3, 5, 7, 9, 99}) {
        for (const ToolId tool : {ToolId::Pencil, ToolId::Brush, ToolId::Eraser, ToolId::Line, ToolId::Rectangle,
                                  ToolId::Ellipse}) {
            MirrorCanvas plain(20, 20, Mirror{});
            MirrorCanvas rotated(20, 20, Mirror{false, false, 7, 9, rotations});
            plain.drag(tool, Vec2f{2.5f, 3.5f}, Vec2f{9.5f, 7.5f});
            rotated.drag(tool, Vec2f{2.5f, 3.5f}, Vec2f{9.5f, 7.5f});
            CHECK(pixelsOf(rotated.doc) == pixelsOf(plain.doc));
        }
        MirrorCanvas plain(20, 20, Mirror{});
        MirrorCanvas rotated(20, 20, Mirror{false, false, 7, 9, rotations});
        plain.tb.settings().primary = kBlue;
        rotated.tb.settings().primary = kBlue;
        plain.drag(ToolId::Fill, Vec2f{3.5f, 3.5f}, Vec2f{3.5f, 3.5f});
        rotated.drag(ToolId::Fill, Vec2f{3.5f, 3.5f}, Vec2f{3.5f, 3.5f});
        CHECK(pixelsOf(rotated.doc) == pixelsOf(plain.doc));
    }
}

TEST_CASE("Toggling the rotation count in the middle of a gesture does not split it") {
    MirrorCanvas c(41, 41, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 4});
    c.tb.select(ToolId::Pencil);
    c.tb.press(c.doc, at(28.5f, 10.5f));
    c.tb.settings().symmetry = SymmetrySet{};
    c.tb.drag(c.doc, at(33.5f, 10.5f));
    c.tb.release(c.doc, at(33.5f, 10.5f));
    CHECK(count(c.doc.image(), kRed) == 4 * 6); // still four copies of the 6 pixel stroke

    MirrorCanvas off(41, 41, Mirror{});
    off.tb.select(ToolId::Pencil);
    off.tb.press(off.doc, at(28.5f, 10.5f));
    off.tb.settings().symmetry = toSet(Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 4});
    off.tb.drag(off.doc, at(33.5f, 10.5f));
    off.tb.release(off.doc, at(33.5f, 10.5f));
    CHECK(count(off.doc.image(), kRed) == 6);
}

TEST_CASE("A rotated stroke is one undo step") {
    MirrorCanvas c(41, 41, Mirror{false, false, kSymmetryCenter, kSymmetryCenter, 6});
    orbitStroke(c, ToolId::Brush);
    CHECK(count(c.doc.image(), kRed) > 0);
    c.doc.undo();
    CHECK(count(c.doc.image(), kRed) == 0);
}

TEST_CASE("The picker and Select ignore rotation") {
    MirrorCanvas c(20, 20, Mirror{true, true, kSymmetryCenter, kSymmetryCenter, 4});
    c.doc.image().set(3, 3, kBlue);
    c.settle();
    c.tb.select(ToolId::Picker);
    c.tb.press(c.doc, at(3.5f, 3.5f));
    c.tb.release(c.doc, at(3.5f, 3.5f));
    CHECK(c.tb.settings().primary == kBlue);
    CHECK(count(c.doc.image(), kBlue) == 1);
}

TEST_CASE("Rotated tools survive hostile coordinates") {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const Mirror& m : {Mirror{false, false, 13, 9, 3}, Mirror{false, true, 13, 9, 6}}) {
        for (const ToolId tool : {ToolId::Pencil, ToolId::Brush, ToolId::Eraser, ToolId::Line, ToolId::Rectangle,
                                  ToolId::Ellipse, ToolId::Fill}) {
            for (const int variant : {0, 1}) {
                const bool brushLike = tool == ToolId::Pencil || tool == ToolId::Brush || tool == ToolId::Eraser;
                if (variant == 1 && !brushLike)
                    continue;
                MirrorCanvas c(20, 20, m);
                c.tb.settings().pixelPerfect = variant == 1; // the pencil's corner trails
                c.tb.settings().squareBrush = variant == 1;  // the brushes' rectangle stamps
                c.tb.settings().brushSize = 16;
                c.tb.settings().strokeWidth = 16;
                // Tools that step through every pixel of a segment (the pencil and square stamps) would walk a
                // two million pixel line per copy; give them hostile values that stay put and a long but
                // bounded segment, and the tools that clip in constant time the full range.
                const bool walks = tool == ToolId::Pencil || (brushLike && variant == 1);
                if (walks) {
                    c.drag(tool, Vec2f{1.0e30f, -1.0e30f}, Vec2f{1.0e30f, -1.0e30f});
                    c.drag(tool, Vec2f{nan, inf}, Vec2f{-inf, 5.5f});
                    c.drag(tool, Vec2f{3.0e6f, 3.0e6f}, Vec2f{3.0e6f, 3.0e6f});
                    c.drag(tool, Vec2f{1.0e5f, -1.0e5f}, Vec2f{2.5f, 2.5f});
                } else {
                    c.drag(tool, Vec2f{1.0e30f, -1.0e30f}, Vec2f{nan, inf});
                    c.drag(tool, Vec2f{-inf, 5.5f}, Vec2f{2.5f, 2.0e9f});
                    c.drag(tool, Vec2f{3.0e6f, 3.0e6f}, Vec2f{-3.0e6f, 4.5f});
                }
            }
        }
    }
    CHECK(true); // reaching here without a crash or sanitizer report is the check
}

TEST_CASE("Pixel-perfect pencil with rotation matches the union of the copies on a long wiggle") {
    const std::vector<std::pair<int, int>> path{{1, 1}, {2, 1}, {2, 2}, {3, 2}, {3, 3}, {4, 3}, {5, 3}, {5, 4},
                                                {6, 4}, {6, 5}, {7, 5}, {8, 5}, {8, 6}, {9, 6}, {9, 7}};
    for (const Mirror& m : rotationSetups()) {
        MirrorCanvas plain(20, 20, Mirror{});
        plain.tb.settings().pixelPerfect = true;
        plain.strokeThrough(path);
        MirrorCanvas rotated(20, 20, m);
        rotated.tb.settings().pixelPerfect = true;
        rotated.strokeThrough(path);
        CHECK(pixelsOf(rotated.doc) == handMirrored(plain.doc, m));
    }
}

// --- symmetry lists: mirror lines at any position and angle, rotation centers --------------------------

namespace {

SymmetryItem mirrorItem(int angle, int px = kSymmetryCenter, int py = kSymmetryCenter) {
    SymmetryItem it = makeMirrorX();
    it.angle = angle;
    it.posX = px;
    it.posY = py;
    return it;
}

SymmetryItem rotateItem(int count, int px = kSymmetryCenter, int py = kSymmetryCenter) {
    SymmetryItem it = makeRotate(count);
    it.posX = px;
    it.posY = py;
    return it;
}

SymmetrySet listOf(std::initializer_list<SymmetryItem> items) {
    SymmetrySet set;
    for (const SymmetryItem& it : items)
        addSymmetryItem(set, it);
    return set;
}

// The plain picture plus a copy of every drawn pixel at `map(x, y)` (pixels that land off the canvas are dropped):
// a mirror worked out by hand from its formula, independent of SymmetryPlan.
template <class Map>
std::vector<Rgba8> reflectedBy(const Document& plain, Map&& map) {
    const Image& img = plain.image();
    std::vector<Rgba8> out = pixelsOf(plain);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const Rgba8 p = img.get(x, y);
            if (p == kWhite)
                continue;
            const std::pair<int, int> to = map(x, y);
            if (img.contains(to.first, to.second))
                out[static_cast<std::size_t>(to.second) * static_cast<std::size_t>(img.width()) + static_cast<std::size_t>(to.first)] = p;
        }
    return out;
}

// Draws `draw` plainly and with the list `set` on a size x size canvas; the second must equal `map` applied to the first.
template <class Draw, class Map>
void checkReflection(int size, const SymmetrySet& set, Map&& map, Draw&& draw) {
    MirrorCanvas plain(size, size, SymmetrySet{});
    draw(plain);
    MirrorCanvas mirrored(size, size, set);
    draw(mirrored);
    CHECK(pixelsOf(mirrored.doc) == reflectedBy(plain.doc, map));
}

// The diagonal mirrors every diagonal test runs: the set, and the pixel each pixel (x, y) of a 20x20 canvas goes to.
struct Diagonal {
    SymmetrySet set;
    std::pair<int, int> (*map)(int, int);
};
std::vector<Diagonal> diagonals() {
    return {
        {listOf({mirrorItem(135)}), [](int x, int y) { return std::pair<int, int>{y, x}; }},
        {listOf({mirrorItem(45)}), [](int x, int y) { return std::pair<int, int>{19 - y, 19 - x}; }},
        // The 135 line through (7, 4) is y = x - 3; the 45 line through it has x + y = 11.
        {listOf({mirrorItem(135, 14, 8)}), [](int x, int y) { return std::pair<int, int>{y + 3, x - 3}; }},
        {listOf({mirrorItem(45, 14, 8)}), [](int x, int y) { return std::pair<int, int>{10 - y, 10 - x}; }},
    };
}

std::vector<SymmetrySet> mixedSets() {
    return {listOf({mirrorItem(45), rotateItem(4)}),
            listOf({rotateItem(4), mirrorItem(45)}),
            listOf({mirrorItem(135, 14, 8), rotateItem(2, 14, 8)}),
            listOf({mirrorItem(0, 13, 9), rotateItem(4, 13, 9)}),
            listOf({mirrorItem(90, 13, 9), mirrorItem(135, 13, 9)}),
            listOf({rotateItem(4, 14, 8), mirrorItem(0, 20)}),
            listOf({mirrorItem(0, 20), rotateItem(4, 14, 8)}),
            listOf({mirrorItem(0), mirrorItem(90), mirrorItem(45), mirrorItem(135)})};
}

} // namespace

TEST_CASE("Two mirrors of the same kind at different positions paint four copies") {
    // Lines at x = 5 and x = 10: the pixel 2 goes to 7 and 17, and the reflection of one in the other to 12.
    MirrorCanvas c(30, 10, listOf({mirrorItem(0, 10), mirrorItem(0, 20)}));
    c.tb.select(ToolId::Pencil);
    c.tb.press(c.doc, at(2.5f, 3.5f));
    c.tb.release(c.doc, at(2.5f, 3.5f));
    CHECK(count(c.doc.image(), kRed) == 4);
    for (const int x : {2, 7, 12, 17})
        CHECK(c.doc.image().get(x, 3) == kRed);

    checkSets({listOf({mirrorItem(0, 10), mirrorItem(0, 20)}),
               listOf({mirrorItem(90, kSymmetryCenter, 6), mirrorItem(90, kSymmetryCenter, 13)})},
              [](MirrorCanvas& m) {
                  m.drag(ToolId::Pencil, Vec2f{1.5f, 2.5f}, Vec2f{4.5f, 4.5f});
                  m.drag(ToolId::Line, Vec2f{2.5f, 3.5f}, Vec2f{3.5f, 5.5f});
                  m.tb.settings().secondary = kRed;
                  m.drag(ToolId::Rectangle, Vec2f{1.5f, 1.5f}, Vec2f{3.5f, 4.5f});
              });
}

TEST_CASE("Mirror positions off the center, at 0 and 90 degrees, equal the hand-mirrored image") {
    const std::vector<SymmetrySet> sets{listOf({mirrorItem(90, kSymmetryCenter, 8)}),
                                        listOf({mirrorItem(90, 13, 9)}),
                                        listOf({mirrorItem(0, 5), mirrorItem(90, kSymmetryCenter, 31)}),
                                        listOf({mirrorItem(0, 14), mirrorItem(90, kSymmetryCenter, 8), mirrorItem(0, 26)})};
    checkSets(sets, [](MirrorCanvas& c) { c.drag(ToolId::Pencil, Vec2f{2.5f, 3.5f}, Vec2f{10.5f, 6.5f}); });
    checkSets(sets, [](MirrorCanvas& c) {
        c.tb.settings().brushSize = 4;
        c.tb.settings().squareBrush = true;
        c.drag(ToolId::Brush, Vec2f{4.5f, 5.5f}, Vec2f{7.5f, 5.5f});
    });
    checkSets(sets, [](MirrorCanvas& c) { c.drag(ToolId::Line, Vec2f{1.5f, 1.5f}, Vec2f{9.5f, 12.5f}); });
    checkSets(sets, [](MirrorCanvas& c) {
        c.tb.settings().shapeMode = ShapeMode::Filled;
        c.drag(ToolId::Rectangle, Vec2f{2.5f, 3.5f}, Vec2f{7.5f, 9.5f});
    });
    // Mirror Y at half pixel 8 (y = 4): the pixel row 2 goes to row 8 - 2 - 1 = 5.
    MirrorCanvas c(20, 20, listOf({mirrorItem(90, kSymmetryCenter, 8)}));
    c.tb.select(ToolId::Pencil);
    c.tb.press(c.doc, at(5.5f, 2.5f));
    c.tb.release(c.doc, at(5.5f, 2.5f));
    CHECK(count(c.doc.image(), kRed) == 2);
    CHECK(c.doc.image().get(5, 5) == kRed);
}

TEST_CASE("Mirrors at 45 and 135 degrees reflect a pencil stroke across the diagonal, exactly") {
    for (const Diagonal& d : diagonals()) {
        for (const bool perfect : {false, true}) {
            checkReflection(20, d.set, d.map, [&](MirrorCanvas& c) {
                c.tb.settings().pixelPerfect = perfect;
                c.drag(ToolId::Pencil, Vec2f{2.5f, 3.5f}, Vec2f{10.5f, 6.5f});
            });
            checkReflection(20, d.set, d.map, [&](MirrorCanvas& c) {
                c.tb.settings().pixelPerfect = perfect;
                c.tb.select(ToolId::Pencil);
                c.tb.press(c.doc, at(8.5f, 1.5f));
                c.tb.drag(c.doc, at(9.5f, 2.5f));
                c.tb.drag(c.doc, at(9.5f, 8.5f));
                c.tb.release(c.doc, at(14.5f, 11.5f));
            });
        }
        // Square stamps: the covered rectangle is reflected, whatever the size's anchor.
        for (const int size : {1, 2, 3, 4})
            checkReflection(20, d.set, d.map, [&](MirrorCanvas& c) {
                c.tb.settings().squareBrush = true;
                c.tb.settings().brushSize = size;
                c.drag(ToolId::Brush, Vec2f{6.5f, 7.5f}, Vec2f{9.5f, 7.5f});
            });
    }
}

TEST_CASE("Mirrors at 45 and 135 degrees reflect a line and a rectangle's box exactly") {
    for (const Diagonal& d : diagonals()) {
        checkReflection(20, d.set, d.map, [](MirrorCanvas& c) { c.drag(ToolId::Line, Vec2f{2.5f, 3.5f}, Vec2f{10.5f, 6.5f}); });
        checkReflection(20, d.set, d.map, [](MirrorCanvas& c) { c.drag(ToolId::Line, Vec2f{12.5f, 1.5f}, Vec2f{12.5f, 9.5f}); });
        for (const ShapeMode mode : {ShapeMode::Outline, ShapeMode::Filled, ShapeMode::Both}) {
            for (const int width : {1, 2}) {
                checkReflection(20, d.set, d.map, [&](MirrorCanvas& c) {
                    c.tb.settings().shapeMode = mode;
                    c.tb.settings().strokeWidth = width;
                    c.tb.settings().secondary = kRed;
                    c.drag(ToolId::Rectangle, Vec2f{2.5f, 3.5f}, Vec2f{7.5f, 5.5f});
                });
            }
        }
    }
    // The box 6x3 at x 2..7, y 3..5, reflected in the main diagonal: 3x6 at x 3..5, y 2..7; they share 3x3.
    MirrorCanvas c(20, 20, listOf({mirrorItem(135)}));
    c.tb.settings().shapeMode = ShapeMode::Filled;
    c.drag(ToolId::Rectangle, Vec2f{2.5f, 3.5f}, Vec2f{7.5f, 5.5f});
    CHECK(count(c.doc.image(), kRed) == 18 + 18 - 9);
    CHECK(c.doc.image().get(3, 2) == kRed);
    CHECK(c.doc.image().get(5, 7) == kRed);
    CHECK(c.doc.image().get(7, 3) == kRed);
    CHECK(c.doc.image().get(6, 6) == kWhite);
}

TEST_CASE("A diagonal mirror blends a translucent stroke once per pixel") {
    const Rgba8 ink{0, 0, 0, 128};
    const Rgba8 expected = blendOver(kWhite, ink);
    for (const bool perfect : {false, true}) {
        MirrorCanvas c(20, 20, listOf({mirrorItem(135)}));
        c.tb.settings().primary = ink;
        c.tb.settings().pixelPerfect = perfect;
        c.drag(ToolId::Pencil, Vec2f{2.5f, 2.5f}, Vec2f{15.5f, 15.5f}); // along the mirror line: every pixel is its own copy
        for (int i = 2; i <= 15; ++i)
            CHECK(c.doc.image().get(i, i) == expected);
        CHECK(count(c.doc.image(), expected) == 14);
    }
}

TEST_CASE("A mirror at an odd angle draws gap-free strokes in both copies") {
    // The line through the center of a 41x41 canvas flips along 30 degrees; strokes stay well away from it.
    const SymmetrySet set = listOf({mirrorItem(30)});
    for (const double deg : {-40.0, 0.0, 30.0, 60.0, 100.0}) {
        for (const OrbitTool& t : {OrbitTool{ToolId::Pencil}, OrbitTool{ToolId::Pencil, true}, OrbitTool{ToolId::Line},
                                   OrbitTool{ToolId::Brush, false, false, 3}, OrbitTool{ToolId::Brush, false, true, 3}}) {
            MirrorCanvas c(41, 41, set);
            configure(c, t);
            const double a = deg * 3.141592653589793 / 180.0;
            const Vec2f from{static_cast<float>(20.5 + 7.0 * std::cos(a)), static_cast<float>(20.5 + 7.0 * std::sin(a))};
            const Vec2f to{static_cast<float>(20.5 + 18.0 * std::cos(a)), static_cast<float>(20.5 + 18.0 * std::sin(a))};
            c.drag(t.tool, from, to);
            INFO("angle " << deg << " tool " << static_cast<int>(t.tool) << " size " << t.size);
            CHECK(componentCount(c.doc.image()) == 2);
            // The mirrored ends are painted where the plan puts them.
            const SymmetryPlan plan = resolveSymmetry(set, 41, 41);
            const PixelPos start = plan.pixel(1, static_cast<int>(std::floor(from.x)), static_cast<int>(std::floor(from.y)));
            const PixelPos end = plan.pixel(1, static_cast<int>(std::floor(to.x)), static_cast<int>(std::floor(to.y)));
            CHECK(c.doc.image().get(start.x, start.y) != kWhite);
            CHECK(c.doc.image().get(end.x, end.y) != kWhite);
        }
    }
    // Dragged in many small steps.
    MirrorCanvas c(41, 41, set);
    c.tb.select(ToolId::Pencil);
    c.tb.press(c.doc, at(28.5f, 16.5f));
    for (int i = 1; i <= 24; ++i)
        c.tb.drag(c.doc, at(28.5f + static_cast<float>(i) * 0.25f, 16.5f - static_cast<float>(i % 5) * 0.4f));
    c.tb.release(c.doc, at(34.5f, 16.5f));
    CHECK(componentCount(c.doc.image()) == 2);
}

TEST_CASE("Mirrors and rotations in one list equal the hand-built union of their copies") {
    checkSets(mixedSets(), [](MirrorCanvas& c) { c.drag(ToolId::Pencil, Vec2f{2.5f, 3.5f}, Vec2f{10.5f, 6.5f}); });
    checkSets(mixedSets(), [](MirrorCanvas& c) {
        c.tb.settings().pixelPerfect = true;
        c.tb.select(ToolId::Pencil);
        c.tb.press(c.doc, at(1.5f, 1.5f));
        c.tb.drag(c.doc, at(5.5f, 2.5f));
        c.tb.drag(c.doc, at(4.5f, 9.5f));
        c.tb.release(c.doc, at(8.5f, 11.5f));
    });
    checkSets(mixedSets(), [](MirrorCanvas& c) { c.drag(ToolId::Line, Vec2f{2.5f, 3.5f}, Vec2f{10.5f, 6.5f}); });
    checkSets(mixedSets(), [](MirrorCanvas& c) {
        c.tb.settings().squareBrush = true;
        c.tb.settings().brushSize = 3;
        c.drag(ToolId::Brush, Vec2f{4.5f, 5.5f}, Vec2f{9.5f, 8.5f});
    });
    for (const ShapeMode mode : {ShapeMode::Outline, ShapeMode::Filled}) {
        checkSets(mixedSets(), [&](MirrorCanvas& c) {
            c.tb.settings().shapeMode = mode;
            c.tb.settings().secondary = kRed;
            c.drag(ToolId::Rectangle, Vec2f{2.5f, 3.5f}, Vec2f{8.5f, 6.5f});
        });
    }

    // Eight copies, each its own stroke, for a mirror and a rotation together.
    for (const SymmetrySet& set : {listOf({mirrorItem(45), rotateItem(4)}), listOf({rotateItem(4), mirrorItem(0)})}) {
        CHECK(resolveSymmetry(set, 41, 41).count() == 8);
        for (const OrbitTool& t : orbitTools()) {
            MirrorCanvas c(41, 41, set);
            configure(c, t);
            orbitStroke(c, t.tool, -20.0);
            INFO("tool " << static_cast<int>(t.tool) << " size " << t.size);
            CHECK(componentCount(c.doc.image()) == 8);
        }
    }
}

TEST_CASE("The order of the items changes the picture") {
    // Rotating about (7, 4) and mirroring across x = 10 in either order: the click lands on different pixels.
    const SymmetrySet turnFirst = listOf({rotateItem(4, 14, 8), mirrorItem(0, 20)});
    const SymmetrySet mirrorFirst = listOf({mirrorItem(0, 20), rotateItem(4, 14, 8)});
    MirrorCanvas a(20, 20, turnFirst);
    MirrorCanvas b(20, 20, mirrorFirst);
    a.drag(ToolId::Pencil, Vec2f{3.5f, 4.5f}, Vec2f{3.5f, 4.5f});
    b.drag(ToolId::Pencil, Vec2f{3.5f, 4.5f}, Vec2f{3.5f, 4.5f});
    CHECK(count(a.doc.image(), kRed) > 4); // 8 copies, some of which may fall off the canvas
    CHECK(count(b.doc.image(), kRed) > 4);
    CHECK(pixelsOf(a.doc) != pixelsOf(b.doc));
    // With the center on the line, the order only reorders the copies.
    MirrorCanvas c(20, 20, listOf({rotateItem(4), mirrorItem(0)}));
    MirrorCanvas d(20, 20, listOf({mirrorItem(0), rotateItem(4)}));
    c.drag(ToolId::Pencil, Vec2f{3.5f, 4.5f}, Vec2f{3.5f, 4.5f});
    d.drag(ToolId::Pencil, Vec2f{3.5f, 4.5f}, Vec2f{3.5f, 4.5f});
    CHECK(pixelsOf(c.doc) == pixelsOf(d.doc));
}

TEST_CASE("Coincident items paint nothing extra") {
    // The same mirror twice, a repeated rotation and a half turn the mirrors already make.
    const std::pair<SymmetrySet, SymmetrySet> pairs[] = {
        {listOf({mirrorItem(0), mirrorItem(0)}), listOf({mirrorItem(0)})},
        {listOf({mirrorItem(0, 20), mirrorItem(180, 20)}), listOf({mirrorItem(0)})},
        {listOf({rotateItem(4), rotateItem(2)}), listOf({rotateItem(4)})},
        {listOf({mirrorItem(0), mirrorItem(90), rotateItem(2)}), listOf({mirrorItem(0), mirrorItem(90)})},
    };
    for (const auto& pr : pairs) {
        for (const ToolId tool : {ToolId::Pencil, ToolId::Brush, ToolId::Line, ToolId::Rectangle, ToolId::Ellipse}) {
            MirrorCanvas twice(20, 20, pr.first);
            MirrorCanvas once(20, 20, pr.second);
            twice.tb.settings().primary = Rgba8{0, 0, 0, 128}; // a translucent color would show a double hit
            once.tb.settings().primary = Rgba8{0, 0, 0, 128};
            twice.drag(tool, Vec2f{2.5f, 3.5f}, Vec2f{7.5f, 8.5f});
            once.drag(tool, Vec2f{2.5f, 3.5f}, Vec2f{7.5f, 8.5f});
            CHECK(pixelsOf(twice.doc) == pixelsOf(once.doc));
        }
    }
}

TEST_CASE("Disabled items and an empty list draw exactly like no symmetry") {
    SymmetrySet disabled = listOf({mirrorItem(0, 7), mirrorItem(135, 9, 9), rotateItem(3, 5, 5), mirrorItem(30)});
    for (int i = 0; i < disabled.count; ++i)
        disabled.items[static_cast<std::size_t>(i)].enabled = false;
    for (const SymmetrySet& set : {disabled, SymmetrySet{}}) {
        for (const ToolId tool : {ToolId::Pencil, ToolId::Brush, ToolId::Eraser, ToolId::Line, ToolId::Rectangle,
                                  ToolId::Ellipse, ToolId::Fill}) {
            MirrorCanvas plain(20, 20, Mirror{});
            MirrorCanvas off(20, 20, set);
            plain.tb.settings().pixelPerfect = true;
            off.tb.settings().pixelPerfect = true;
            plain.drag(tool, Vec2f{2.5f, 3.5f}, Vec2f{9.5f, 7.5f});
            off.drag(tool, Vec2f{2.5f, 3.5f}, Vec2f{9.5f, 7.5f});
            CHECK(pixelsOf(off.doc) == pixelsOf(plain.doc));
        }
    }
    // One enabled item among disabled ones is the only one that counts: the mirror at half pixel 7 sends pixel 2 to 4.
    SymmetrySet one = disabled;
    one.items[0].enabled = true;
    MirrorCanvas c(20, 20, one);
    c.drag(ToolId::Pencil, Vec2f{2.5f, 3.5f}, Vec2f{2.5f, 3.5f});
    CHECK(count(c.doc.image(), kRed) == 2);
    CHECK(c.doc.image().get(4, 3) == kRed);
}

TEST_CASE("Positions far outside the canvas are clamped onto it") {
    // A line at half pixel INT_MAX is clamped to 2 * width, the right edge: the mirror of pixel 3 is off the image.
    MirrorCanvas c(20, 20, listOf({mirrorItem(0, std::numeric_limits<int>::max())}));
    c.drag(ToolId::Pencil, Vec2f{3.5f, 4.5f}, Vec2f{3.5f, 4.5f});
    CHECK(count(c.doc.image(), kRed) == 1);
    // A very negative position is the center, not the left edge.
    MirrorCanvas m(20, 20, listOf({mirrorItem(0, std::numeric_limits<int>::min()), mirrorItem(90, kSymmetryCenter, -5)}));
    m.drag(ToolId::Pencil, Vec2f{3.5f, 4.5f}, Vec2f{3.5f, 4.5f});
    CHECK(count(m.doc.image(), kRed) == 4);
    CHECK(m.doc.image().get(16, 15) == kRed);
}

TEST_CASE("Fill floods every seed a diagonal mirror makes") {
    // A closed 5x5 wall box at x 2..6, y 8..12 and its transpose at x 8..12, y 2..6.
    MirrorCanvas c(20, 20, listOf({mirrorItem(135)}));
    for (int i = 0; i < 5; ++i)
        for (const std::pair<int, int>& e : {std::pair<int, int>{2 + i, 8}, std::pair<int, int>{2 + i, 12},
                                             std::pair<int, int>{2, 8 + i}, std::pair<int, int>{6, 8 + i}}) {
            c.doc.image().set(e.first, e.second, kBlack);
            c.doc.image().set(e.second, e.first, kBlack);
        }
    c.settle();
    c.tb.settings().primary = kBlue;
    c.tb.select(ToolId::Fill);
    c.tb.press(c.doc, at(4.5f, 10.5f));
    c.tb.release(c.doc, at(4.5f, 10.5f));
    CHECK(count(c.doc.image(), kBlue) == 2 * 3 * 3);
    CHECK(c.doc.image().get(10, 4) == kBlue);
    CHECK(c.doc.image().get(15, 15) == kWhite);

    // A list with a mirror and a rotation fills a region that all copies share once.
    MirrorCanvas r(20, 20, listOf({mirrorItem(45), rotateItem(4)}));
    r.tb.settings().primary = kBlue;
    r.tb.select(ToolId::Fill);
    r.tb.press(r.doc, at(3.5f, 3.5f));
    r.tb.release(r.doc, at(3.5f, 3.5f));
    CHECK(count(r.doc.image(), kBlue) == 400);
}

TEST_CASE("The picker ignores a symmetry list") {
    MirrorCanvas c(20, 20, listOf({mirrorItem(45), mirrorItem(0), rotateItem(3)}));
    c.doc.image().set(3, 3, kBlue);
    c.settle();
    c.tb.select(ToolId::Picker);
    c.tb.press(c.doc, at(3.5f, 3.5f));
    c.tb.release(c.doc, at(3.5f, 3.5f));
    CHECK(c.tb.settings().primary == kBlue);
    CHECK(count(c.doc.image(), kBlue) == 1);
}

TEST_CASE("Changing the list in the middle of a gesture does not split it") {
    MirrorCanvas c(41, 41, listOf({mirrorItem(135), rotateItem(4)}));
    c.tb.select(ToolId::Rectangle);
    c.tb.settings().shapeMode = ShapeMode::Filled;
    c.tb.press(c.doc, at(28.5f, 4.5f));
    c.tb.drag(c.doc, at(30.5f, 6.5f));
    c.tb.settings().symmetry = SymmetrySet{};
    c.tb.drag(c.doc, at(31.5f, 7.5f));
    c.tb.release(c.doc, at(31.5f, 7.5f));
    CHECK(count(c.doc.image(), kRed) == 8 * 16); // 4x4 in each of 8 separate copies
    CHECK(componentCount(c.doc.image()) == 8);
}

TEST_CASE("Cancelling a shape with a list of mirrors and rotations restores the image exactly") {
    for (const SymmetrySet& set : mixedSets()) {
        for (const ToolId tool : {ToolId::Line, ToolId::Rectangle, ToolId::Ellipse}) {
            MirrorCanvas c(20, 20, set);
            c.doc.fillTestPattern();
            c.doc.takeDirty();
            const std::vector<Rgba8> before = pixelsOf(c.doc);
            c.tb.settings().shapeMode = ShapeMode::Both;
            c.tb.settings().strokeWidth = 2;
            c.tb.select(tool);
            c.tb.press(c.doc, at(2.5f, 2.5f));
            c.tb.drag(c.doc, at(6.5f, 5.5f));
            c.tb.drag(c.doc, at(11.5f, 8.5f));
            CHECK(pixelsOf(c.doc) != before);
            c.tb.cancel(c.doc);
            CHECK(pixelsOf(c.doc) == before);
        }
    }
}

TEST_CASE("A stroke with a symmetry list is one undo step") {
    MirrorCanvas c(41, 41, listOf({mirrorItem(30), mirrorItem(90, kSymmetryCenter, 30), rotateItem(3)}));
    orbitStroke(c, ToolId::Brush);
    CHECK(count(c.doc.image(), kRed) > 0);
    c.doc.undo();
    CHECK(count(c.doc.image(), kRed) == 0);
}

TEST_CASE("Tools survive hostile coordinates with item lists of any kind") {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const int hugePos = std::numeric_limits<int>::max();
    const SymmetrySet sets[] = {
        listOf({mirrorItem(30, hugePos, -1), rotateItem(3, 13, 9)}),
        listOf({mirrorItem(45, 13, 9), mirrorItem(135, std::numeric_limits<int>::min(), hugePos), rotateItem(6, 5, 5)}),
        listOf({mirrorItem(0, 5), mirrorItem(0, 9), mirrorItem(90, kSymmetryCenter, 3), mirrorItem(77)}),
    };
    for (const SymmetrySet& set : sets) {
        for (const ToolId tool : {ToolId::Pencil, ToolId::Brush, ToolId::Eraser, ToolId::Line, ToolId::Rectangle,
                                  ToolId::Ellipse, ToolId::Fill}) {
            for (const int variant : {0, 1}) {
                const bool brushLike = tool == ToolId::Pencil || tool == ToolId::Brush || tool == ToolId::Eraser;
                if (variant == 1 && !brushLike)
                    continue;
                MirrorCanvas c(20, 20, set);
                c.tb.settings().pixelPerfect = variant == 1;
                c.tb.settings().squareBrush = variant == 1;
                c.tb.settings().brushSize = 16;
                c.tb.settings().strokeWidth = 16;
                // As in the rotation test: tools that step through every pixel of a segment get hostile values that
                // stay put and a long but bounded segment; the tools that clip in constant time the full range.
                if (tool == ToolId::Pencil || (brushLike && variant == 1)) {
                    c.drag(tool, Vec2f{1.0e30f, -1.0e30f}, Vec2f{1.0e30f, -1.0e30f});
                    c.drag(tool, Vec2f{nan, inf}, Vec2f{-inf, 5.5f});
                    c.drag(tool, Vec2f{1.0e4f, -1.0e4f}, Vec2f{2.5f, 2.5f});
                } else {
                    c.drag(tool, Vec2f{1.0e30f, -1.0e30f}, Vec2f{nan, inf});
                    c.drag(tool, Vec2f{-inf, 5.5f}, Vec2f{2.5f, 2.0e9f});
                    c.drag(tool, Vec2f{3.0e6f, 3.0e6f}, Vec2f{-3.0e6f, 4.5f});
                }
            }
        }
    }
    CHECK(true); // reaching here without a crash or sanitizer report is the check
}
