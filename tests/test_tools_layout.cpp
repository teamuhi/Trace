#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "core/document.hpp"
#include "core/raster.hpp"
#include "core/text_raster.hpp"
#include "core/tools_layout.hpp"
#include "font_util.hpp"
#include "tools/toolbox.hpp"

using namespace trace;
using namespace trace::toolslayout;

namespace {

constexpr Rgba8 kBlack{0, 0, 0, 255};
constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kClear{};

DockObservation at(std::uint32_t node, DockKind kind, float size) {
    DockObservation o;
    o.docked = true;
    o.node = node;
    o.kind = kind;
    o.size = size;
    return o;
}

PointerEvent pointer(float x, float y) {
    PointerEvent ev;
    ev.pos = Vec2f{x, y};
    return ev;
}

} // namespace

// --- icon grid and shape ---------------------------------------------------------------------------------

TEST_CASE("gridColumns fits as many cells as the width allows, between one and the count") {
    CHECK(gridColumns(200.0f, 40.0f, 4.0f, 10) == 4); // 4 * 40 + 3 * 4 = 172, a fifth would need 216
    CHECK(gridColumns(216.0f, 40.0f, 4.0f, 10) == 5);
    CHECK(gridColumns(1000.0f, 40.0f, 4.0f, 10) == 10); // never more than there are tools
    CHECK(gridColumns(10.0f, 40.0f, 4.0f, 10) == 1);    // narrower than one cell: one per row
    CHECK(gridColumns(0.0f, 40.0f, 4.0f, 10) == 1);
    CHECK(gridColumns(-50.0f, 40.0f, 4.0f, 10) == 1);
}

TEST_CASE("gridColumns survives degenerate input") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(gridColumns(nan, 40.0f, 4.0f, 10) == 1);
    CHECK(gridColumns(inf, 40.0f, 4.0f, 10) == 1);
    CHECK(gridColumns(200.0f, 0.0f, 4.0f, 10) == 1);
    CHECK(gridColumns(200.0f, -3.0f, 4.0f, 10) == 1);
    CHECK(gridColumns(200.0f, 40.0f, -9.0f, 10) == 5); // a negative gap counts as none
    CHECK(gridColumns(200.0f, 40.0f, 4.0f, 0) == 1);
}

TEST_CASE("barShape: wide and short is a bar, a column or a square is not") {
    CHECK(barShape(1900.0f, 160.0f, 16.0f));
    CHECK_FALSE(barShape(220.0f, 900.0f, 16.0f));
    CHECK_FALSE(barShape(600.0f, 500.0f, 16.0f));  // wider than tall, but not by much
    CHECK_FALSE(barShape(400.0f, 100.0f, 16.0f));  // under 28 em: too narrow to hold several sections
    CHECK(barShape(500.0f, 100.0f, 16.0f));
    CHECK_FALSE(barShape(800.0f, 100.0f, 32.0f)); // the same pixels hold fewer em at a bigger font
}

TEST_CASE("classifyDock tells a side column from a bar") {
    CHECK(classifyDock(false, 220.0f, 900.0f) == DockKind::Side);
    CHECK(classifyDock(false, 900.0f, 220.0f) == DockKind::Side); // beside the canvas, however wide
    CHECK(classifyDock(true, 1900.0f, 500.0f) == DockKind::Bar);  // just docked on the top: half the window tall
    CHECK(classifyDock(true, 220.0f, 400.0f) == DockKind::Other); // stacked in a narrow column
}

// --- keeping the size after moving the panel -------------------------------------------------------------

TEST_CASE("A layout restored from disk is adopted untouched") {
    DockSizeTracker t;
    const DockCommand c = t.update(at(7, DockKind::Side, 300.0f), 240.0f, 0.0f);
    CHECK_FALSE(c.apply);
}

TEST_CASE("Moved to a bar for the first time: the panel asks for the height its content needs") {
    DockSizeTracker t;
    t.update(at(1, DockKind::Side, 240.0f), 240.0f, 0.0f); // restored
    DockCommand c = t.update(at(2, DockKind::Bar, 520.0f), 240.0f, 0.0f); // landed on the top: half the window
    CHECK_FALSE(c.apply);                                                  // content height not known yet
    c = t.update(at(2, DockKind::Bar, 520.0f), 240.0f, 130.0f);
    REQUIRE(c.apply);
    CHECK(c.size == 130.0f);
    c = t.update(at(2, DockKind::Bar, 130.0f), 240.0f, 130.0f); // applied: nothing more to say
    CHECK_FALSE(c.apply);
}

TEST_CASE("Moved to a side without a remembered width: the default width") {
    DockSizeTracker t;
    t.update(at(1, DockKind::Bar, 130.0f), 240.0f, 130.0f); // restored as a bar
    const DockCommand c = t.update(at(3, DockKind::Side, 900.0f), 240.0f, 0.0f);
    REQUIRE(c.apply);
    CHECK(c.size == 240.0f);
}

TEST_CASE("The size of each kind of place is remembered and given back") {
    DockSizeTracker t;
    t.update(at(1, DockKind::Side, 260.0f), 240.0f, 0.0f);
    for (int i = 0; i < 10; ++i)
        t.update(at(1, DockKind::Side, 260.0f), 240.0f, 0.0f); // settled: 260 is the user's width
    CHECK(t.saved(DockKind::Side) == 260.0f);

    t.update(at(2, DockKind::Bar, 520.0f), 240.0f, 0.0f); // moved to the top
    for (int i = 0; i < 12; ++i)
        t.update(at(2, DockKind::Bar, 140.0f), 240.0f, 140.0f);
    CHECK(t.saved(DockKind::Bar) == 140.0f);

    // Back to a side column (a different node, the right edge instead of the left): the 260 comes back.
    const DockCommand c = t.update(at(4, DockKind::Side, 900.0f), 240.0f, 0.0f);
    REQUIRE(c.apply);
    CHECK(c.size == 260.0f);
    // And to a bar again: the 140 comes back, with no fitting needed.
    for (int i = 0; i < 8; ++i)
        t.update(at(4, DockKind::Side, 260.0f), 240.0f, 0.0f);
    const DockCommand b = t.update(at(5, DockKind::Bar, 600.0f), 240.0f, 0.0f);
    REQUIRE(b.apply);
    CHECK(b.size == 140.0f);
}

TEST_CASE("The size ImGui hands a freshly docked panel is not remembered") {
    DockSizeTracker t;
    t.update(at(1, DockKind::Side, 260.0f), 240.0f, 0.0f);
    for (int i = 0; i < 10; ++i)
        t.update(at(1, DockKind::Side, 260.0f), 240.0f, 0.0f);
    t.update(at(2, DockKind::Side, 960.0f), 240.0f, 0.0f); // moved to the other side: half the window
    t.update(at(2, DockKind::Side, 960.0f), 240.0f, 0.0f);
    CHECK(t.saved(DockKind::Side) == 260.0f); // not 960
}

TEST_CASE("Floating or tabbed panels are left alone and the next dock counts as a move") {
    DockSizeTracker t;
    t.update(at(1, DockKind::Side, 260.0f), 240.0f, 0.0f);
    for (int i = 0; i < 10; ++i)
        t.update(at(1, DockKind::Side, 260.0f), 240.0f, 0.0f);
    CHECK_FALSE(t.update(DockObservation{}, 240.0f, 0.0f).apply); // dragged out: floating
    CHECK_FALSE(t.update(at(9, DockKind::Other, 300.0f), 240.0f, 0.0f).apply);
    const DockCommand c = t.update(at(1, DockKind::Side, 900.0f), 240.0f, 0.0f); // docked again
    REQUIRE(c.apply);
    CHECK(c.size == 260.0f);
}

TEST_CASE("Fitting a bar gives up after a few frames and tolerates a missing measurement") {
    DockSizeTracker t;
    t.update(at(1, DockKind::Side, 240.0f), 240.0f, 0.0f);
    t.update(at(2, DockKind::Bar, 520.0f), 240.0f, 0.0f);
    int applied = 0;
    for (int i = 0; i < 20; ++i) {
        // The node never reaches the fit (something else holds it): the tracker must not push forever.
        if (t.update(at(2, DockKind::Bar, 520.0f), 240.0f, 130.0f).apply)
            ++applied;
    }
    CHECK(applied <= DockSizeTracker::kFitFrames);
    CHECK(t.update(at(2, DockKind::Bar, 520.0f), 240.0f, 0.0f).apply == false);
}

// --- brush style -----------------------------------------------------------------------------------------

TEST_CASE("Brush styles map onto the two settings behind them") {
    ToolSettings s;
    CHECK(brushStyleOf(s) == BrushStyle::Round);

    setBrushStyle(s, BrushStyle::Square);
    CHECK(s.squareBrush);
    CHECK(brushStyleOf(s) == BrushStyle::Square);

    setBrushStyle(s, BrushStyle::Soft);
    CHECK_FALSE(s.squareBrush);
    CHECK(s.brushHardness == 50);
    CHECK(brushStyleOf(s) == BrushStyle::Soft);

    s.brushHardness = 20; // the user's own softness is kept when coming back to Soft
    setBrushStyle(s, BrushStyle::Round);
    CHECK(s.brushHardness == 100);
    CHECK(brushStyleOf(s) == BrushStyle::Round);
    s.brushHardness = 20;
    setBrushStyle(s, BrushStyle::Soft);
    CHECK(s.brushHardness == 20);

    s.brushHardness = 0; // out of range stays at least the minimum
    setBrushStyle(s, BrushStyle::Soft);
    CHECK(s.brushHardness == ToolSettings::kMinHardness);

    setBrushStyle(s, BrushStyle::Square);
    CHECK(s.brushHardness == 100);
    CHECK(brushStyleOf(s) == BrushStyle::Square);
}

TEST_CASE("A soft brush fades toward its edge; a hard one is the same as before") {
    const auto stroke = [](float hardness) {
        Image img(40, 40);
        for (Rgba8& p : img.pixels())
            p = kWhite;
        drawCapsule(img, Vec2f{20.0f, 20.0f}, Vec2f{20.0f, 20.0f}, 10.0f, Paint{kBlack}, true, nullptr, hardness);
        return img;
    };
    const Image hard = stroke(1.0f);
    const Image soft = stroke(0.2f);

    Image reference(40, 40);
    for (Rgba8& p : reference.pixels())
        p = kWhite;
    drawCapsule(reference, Vec2f{20.0f, 20.0f}, Vec2f{20.0f, 20.0f}, 10.0f, Paint{kBlack}, true);
    CHECK(hard.pixels().size() == reference.pixels().size());
    CHECK(std::equal(hard.pixels().begin(), hard.pixels().end(), reference.pixels().begin()));

    // The center is solid either way; a pixel a few steps inside the rim is solid when hard but only partly covered
    // when soft; nothing is painted past the rim.
    CHECK(soft.get(20, 20) == kBlack);
    CHECK(hard.get(27, 20) == kBlack);
    CHECK(soft.get(27, 20).r > 0);
    CHECK(soft.get(27, 20).r < 255);
    CHECK(soft.get(31, 20) == kWhite);

    // Darkness falls off steadily from the center outward.
    int last = -1;
    for (int x = 20; x <= 30; ++x) {
        const int v = soft.get(x, 20).r;
        CHECK(v >= last);
        last = v;
    }
}

TEST_CASE("Hardness out of range or not a number is treated as a hard edge") {
    const auto stroke = [](float hardness) {
        Image img(24, 24);
        for (Rgba8& p : img.pixels())
            p = kWhite;
        drawCapsule(img, Vec2f{12.0f, 12.0f}, Vec2f{12.0f, 12.0f}, 6.0f, Paint{kBlack}, true, nullptr, hardness);
        return img;
    };
    const Image hard = stroke(1.0f);
    for (const float h : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 5.0f}) {
        const Image other = stroke(h);
        CHECK(std::equal(hard.pixels().begin(), hard.pixels().end(), other.pixels().begin()));
    }
    // Below zero clamps to the softest brush: still a valid stroke, darkest at its middle.
    const Image softest = stroke(-3.0f);
    CHECK(softest.get(12, 12).r < 64);
    CHECK(softest.get(17, 12).r > 0);
}

TEST_CASE("A soft stroke composites once however many capsules overlap") {
    Document doc(40, 40, kWhite);
    Toolbox tb;
    tb.select(ToolId::Brush);
    tb.settings().brushSize = 20;
    tb.settings().brushHardness = 30;
    tb.settings().primary = kBlack;
    tb.press(doc, pointer(10.5f, 20.5f));
    for (int i = 1; i <= 20; ++i)
        tb.drag(doc, pointer(10.5f + static_cast<float>(i), 20.5f));
    tb.release(doc, pointer(30.5f, 20.5f));

    // The same stroke dragged in one jump paints the same pixels in the middle of the line.
    Document jump(40, 40, kWhite);
    Toolbox tb2;
    tb2.select(ToolId::Brush);
    tb2.settings() = tb.settings();
    tb2.press(jump, pointer(10.5f, 20.5f));
    tb2.release(jump, pointer(30.5f, 20.5f));
    for (int x = 14; x <= 26; ++x)
        CHECK(doc.image().get(x, 20) == jump.image().get(x, 20));
}

// --- text alignment --------------------------------------------------------------------------------------

namespace {

// Leftmost and rightmost covered column of one text line (a band of rows) of a bitmap.
void inkSpan(const TextBitmap& b, int y0, int y1, int& left, int& right) {
    left = b.width;
    right = -1;
    for (int y = y0; y < y1; ++y) {
        for (int x = 0; x < b.width; ++x) {
            if (b.at(x, y) != 0) {
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
    }
}

} // namespace

TEST_CASE("Text alignment moves the shorter lines, not the widest one") {
    REQUIRE(trace_test::mono().size() > 1000);
    TextBitmap left;
    TextBitmap center;
    TextBitmap right;
    const std::string text = "WWWWWWWW\nWW";
    REQUIRE(rasterizeText(trace_test::mono(), text, 24, false, left, TextAlign::Left));
    REQUIRE(rasterizeText(trace_test::mono(), text, 24, false, center, TextAlign::Center));
    REQUIRE(rasterizeText(trace_test::mono(), text, 24, false, right, TextAlign::Right));
    CHECK(left.width == center.width);
    CHECK(left.width == right.width);
    CHECK(left.height == right.height);

    const int half = left.height / 2;
    int l0 = 0, r0 = 0, l1 = 0, r1 = 0, l2 = 0, r2 = 0;
    // The widest (first) line is the same in all three.
    inkSpan(left, 0, half, l0, r0);
    inkSpan(right, 0, half, l1, r1);
    inkSpan(center, 0, half, l2, r2);
    CHECK(l0 == l1);
    CHECK(r0 == r1);
    CHECK(l0 == l2);

    // The short second line: flush left, then moved right by half the slack, then by all of it.
    int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
    inkSpan(left, half, left.height, a, b);
    inkSpan(center, half, center.height, c, d);
    inkSpan(right, half, right.height, e, f);
    CHECK(c > a);
    CHECK(e > c);
    CHECK(f <= r0);
    CHECK(f >= r0 - 3); // flush with the right edge of the widest line, give or take the glyph's own bearing
    const int slack = e - a;
    CHECK(std::abs((c - a) - slack / 2) <= 1);
}

TEST_CASE("Text alignment with one line or antialiasing leaves a valid bitmap") {
    REQUIRE(trace_test::inter().size() > 1000);
    for (const TextAlign align : {TextAlign::Left, TextAlign::Center, TextAlign::Right}) {
        TextBitmap one;
        TextBitmap one2;
        REQUIRE(rasterizeText(trace_test::inter(), "Hello", 20, true, one, align));
        REQUIRE(rasterizeText(trace_test::inter(), "Hello", 20, true, one2, TextAlign::Left));
        CHECK(one.coverage == one2.coverage); // a single line is as wide as the text: nothing to align
        TextBitmap two;
        CHECK(rasterizeText(trace_test::inter(), "Hello\nHi", 20, true, two, align));
        CHECK(two.height == 2 * one.height);
    }
}

// --- selection on layers ---------------------------------------------------------------------------------

TEST_CASE("Moving a selection on an upper layer lifts from that layer only and leaves it transparent") {
    Document doc(12, 12, kWhite);
    doc.image().set(3, 3, kRed); // bottom layer pixel under the selection: must never move
    doc.markDirty(IRect{3, 3, 4, 4});
    doc.commit();
    REQUIRE(doc.addLayer() == Document::OpResult::Applied);
    for (int y = 2; y < 5; ++y)
        for (int x = 2; x < 5; ++x)
            doc.image().set(x, y, kBlack);
    doc.markDirty(IRect{2, 2, 5, 5});
    doc.commit();
    REQUIRE(doc.activeLayer() == 1);

    Toolbox tb;
    tb.select(ToolId::Select);
    tb.press(doc, pointer(1.5f, 1.5f)); // marquee over the black square
    tb.drag(doc, pointer(5.5f, 5.5f));
    tb.release(doc, pointer(5.5f, 5.5f));
    REQUIRE(doc.selection().active());

    tb.press(doc, pointer(3.5f, 3.5f)); // drag inside: lift and move by (4, 4)
    tb.drag(doc, pointer(7.5f, 7.5f));
    tb.release(doc, pointer(7.5f, 7.5f));
    doc.clearSelection();
    doc.commit();

    // On the layer: the old square is transparent (not the secondary color) and the black moved.
    CHECK(doc.image().get(3, 3) == kClear);
    CHECK(doc.image().get(7, 7) == kBlack);
    // The bottom layer is untouched.
    REQUIRE(doc.setActive(0, doc.activeFrame()) == Document::OpResult::Applied);
    CHECK(doc.image().get(3, 3) == kRed);
    CHECK(doc.image().get(7, 7) == kWhite);

    // One undo step for the move, and it restores both squares.
    CHECK(doc.undo());
    CHECK(doc.cel(1, 0).get(3, 3) == kBlack);
    CHECK(doc.cel(1, 0).get(7, 7) == kClear);
}

TEST_CASE("Delete and copy on a selection work on the active layer") {
    Document doc(8, 8, kWhite);
    REQUIRE(doc.addLayer() == Document::OpResult::Applied);
    doc.image().set(2, 2, kRed);
    doc.markDirty(IRect{2, 2, 3, 3});
    doc.commit();
    doc.selectRect(IRect{1, 1, 4, 4});
    CHECK(doc.copySelection().get(1, 1) == kRed); // copied from the upper layer, not the white bottom one
    doc.deleteSelection(kClear); // what the workspace passes for a layer above the bottom
    doc.commit();
    CHECK(doc.image().get(2, 2) == kClear);
    REQUIRE(doc.setActive(0, doc.activeFrame()) == Document::OpResult::Applied);
    CHECK(doc.image().get(2, 2) == kWhite);
}
