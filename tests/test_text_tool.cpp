#include <doctest/doctest.h>

#include <limits>
#include <string>

#include "font_util.hpp"
#include "tools/toolbox.hpp"

using namespace trace;
using trace_test::inter;
using trace_test::mono;

namespace {

constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kGreen{0, 255, 0, 255};

PointerEvent at(float x, float y, bool secondary = false) {
    PointerEvent ev;
    ev.pos = Vec2f{x, y};
    ev.secondary = secondary;
    return ev;
}

int count(const Image& img, Rgba8 color) {
    int n = 0;
    for (const Rgba8& p : img.pixels())
        n += p == color ? 1 : 0;
    return n;
}

// A toolbox with the Text tool selected, hard edges, red primary and green secondary.
struct TextFixture {
    Document doc{120, 80, kWhite};
    Toolbox tb;

    TextFixture() {
        tb.text().setFonts(inter(), mono());
        ToolSettings& s = tb.settings();
        s.primary = kRed;
        s.secondary = kGreen;
        s.antialias = false;
        s.textSize = 20;
        tb.select(ToolId::Text);
        doc.takeDirty();
    }

    void click(float x, float y, bool secondary = false) {
        tb.press(doc, at(x, y, secondary));
        tb.release(doc, at(x, y, secondary));
    }
};

} // namespace

TEST_CASE("A click opens a text box and draws nothing yet") {
    TextFixture f;
    f.click(10.5f, 12.5f);
    CHECK(f.tb.textEditing());
    CHECK_FALSE(f.tb.gestureActive());
    CHECK(f.tb.text().originX() == 10);
    CHECK(f.tb.text().originY() == 12);
    f.tb.text().setText("Hi");
    CHECK(count(f.doc.image(), kRed) == 0); // the text is only a preview until it is applied
    CHECK_FALSE(f.tb.text().bitmap(f.tb.settings()).empty());
}

TEST_CASE("Committing draws the text at the click and is one undo step") {
    TextFixture f;
    f.click(10.0f, 10.0f);
    f.tb.text().setText("Hi");
    CHECK(f.tb.commitText(f.doc));
    CHECK_FALSE(f.tb.textEditing());
    CHECK(count(f.doc.image(), kRed) > 20);
    // Nothing above or left of the click.
    for (int y = 0; y < 80; ++y) {
        for (int x = 0; x < 120; ++x) {
            if (x < 10 || y < 10)
                CHECK(f.doc.image().get(x, y) == kWhite);
        }
    }
    CHECK(f.doc.canUndo());
    CHECK(f.doc.undo());
    CHECK(count(f.doc.image(), kRed) == 0);
    CHECK_FALSE(f.doc.canUndo()); // one step, not several
}

TEST_CASE("A right-button click draws in the secondary color") {
    TextFixture f;
    f.click(5.0f, 5.0f, true);
    f.tb.text().setText("Hi");
    f.tb.commitText(f.doc);
    CHECK(count(f.doc.image(), kGreen) > 20);
    CHECK(count(f.doc.image(), kRed) == 0);
}

TEST_CASE("Changing the size, font or color while typing changes the result") {
    TextFixture f;
    f.click(2.0f, 2.0f);
    f.tb.text().setText("Hi");
    const int smallWidth = f.tb.text().bitmap(f.tb.settings()).width;
    f.tb.settings().textSize = 40;
    CHECK(f.tb.text().bitmap(f.tb.settings()).width > smallWidth);
    const int sansWidth = f.tb.text().bitmap(f.tb.settings()).width;
    f.tb.settings().textFontFamily = "JetBrains Mono";
    CHECK(f.tb.text().bitmap(f.tb.settings()).width != sansWidth);

    f.tb.settings().primary = Rgba8{0, 0, 255, 255};
    f.tb.commitText(f.doc);
    CHECK(count(f.doc.image(), Rgba8{0, 0, 255, 255}) > 20);
}

TEST_CASE("Empty text commits nothing and records no undo step") {
    TextFixture f;
    f.click(10.0f, 10.0f);
    CHECK_FALSE(f.tb.commitText(f.doc));
    CHECK_FALSE(f.tb.textEditing());
    CHECK_FALSE(f.doc.canUndo());
    CHECK(count(f.doc.image(), kRed) == 0);
}

TEST_CASE("Discarding drops the text without touching the image") {
    TextFixture f;
    f.click(10.0f, 10.0f);
    f.tb.text().setText("gone");
    f.tb.discardText();
    CHECK_FALSE(f.tb.textEditing());
    CHECK_FALSE(f.tb.commitText(f.doc));
    CHECK(count(f.doc.image(), kRed) == 0);
    CHECK_FALSE(f.doc.canUndo());
}

TEST_CASE("A second click applies the first text and opens a new box") {
    TextFixture f;
    f.click(5.0f, 5.0f);
    f.tb.text().setText("A");
    const auto firstSerial = f.tb.text().serial();
    f.click(60.0f, 40.0f);
    CHECK(f.tb.textEditing());
    CHECK(f.tb.text().serial() != firstSerial);
    CHECK(f.tb.text().text().empty());
    CHECK(f.tb.text().originX() == 60);
    CHECK(count(f.doc.image(), kRed) > 5); // the first text is in the image
    CHECK(f.doc.canUndo());                // and the click's release recorded it
}

TEST_CASE("Text partly or wholly outside the image is clipped") {
    TextFixture f;
    f.click(-8.0f, -8.0f);
    f.tb.text().setText("Hello");
    CHECK(f.tb.commitText(f.doc));
    CHECK(count(f.doc.image(), kRed) > 0);

    TextFixture g;
    g.click(500.0f, 500.0f);
    g.tb.text().setText("Hello");
    CHECK_FALSE(g.tb.commitText(g.doc));
    CHECK(count(g.doc.image(), kRed) == 0);
    CHECK_FALSE(g.doc.canUndo());
}

TEST_CASE("Hostile pointer positions and text never crash") {
    TextFixture f;
    f.click(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    CHECK(f.tb.textEditing());
    f.tb.text().setText(std::string(kMaxTextBytes * 2, 'x'));
    CHECK(f.tb.text().text().size() <= kMaxTextBytes);
    f.tb.commitText(f.doc);
    f.click(1e30f, -1e30f);
    f.tb.text().setText("a\xFF\xFEz");
    f.tb.commitText(f.doc);
    CHECK_FALSE(f.tb.textEditing());
}

TEST_CASE("Without fonts the tool still works and draws nothing") {
    Document doc(40, 40, kWhite);
    Toolbox tb;
    tb.select(ToolId::Text);
    tb.press(doc, at(5.0f, 5.0f));
    tb.release(doc, at(5.0f, 5.0f));
    tb.text().setText("Hi");
    CHECK(tb.text().bitmap(tb.settings()).empty());
    CHECK_FALSE(tb.commitText(doc));
    CHECK_FALSE(doc.canUndo());
}

TEST_CASE("A box is dropped when the document is replaced or the layer changes") {
    TextFixture f;
    f.click(5.0f, 5.0f);
    f.tb.text().setText("Hi");
    f.doc.reset(30, 30);
    CHECK(f.tb.text().stale(f.doc));
    CHECK_FALSE(f.tb.commitText(f.doc));
    CHECK_FALSE(f.tb.textEditing());

    TextFixture g;
    REQUIRE(g.doc.addLayer() == Document::OpResult::Applied);
    g.click(5.0f, 5.0f);
    g.tb.text().setText("Hi");
    REQUIRE(g.doc.setActive(0, g.doc.activeFrame()) == Document::OpResult::Applied);
    CHECK(g.tb.text().stale(g.doc));
    CHECK_FALSE(g.tb.commitText(g.doc));
    CHECK(count(g.doc.cel(0, 0), kRed) == 0);
    CHECK(count(g.doc.cel(1, 0), kRed) == 0);
}

TEST_CASE("A locked or hidden layer refuses the click and drops a box that was open") {
    TextFixture f;
    LayerInfo info = f.doc.layer(0);
    info.locked = true;
    f.doc.setLayerProps(0, info);
    f.click(5.0f, 5.0f);
    CHECK_FALSE(f.tb.textEditing());
    CHECK(f.tb.lastRefusal() == Toolbox::Refusal::LayerLocked);

    TextFixture g;
    g.click(5.0f, 5.0f);
    g.tb.text().setText("Hi");
    LayerInfo lock = g.doc.layer(0);
    lock.locked = true;
    g.doc.setLayerProps(0, lock);
    CHECK_FALSE(g.tb.commitText(g.doc));
    CHECK_FALSE(g.tb.textEditing());
    CHECK(count(g.doc.image(), kRed) == 0);
}

TEST_CASE("An empty document never opens a box") {
    Document doc(0, 0);
    Toolbox tb;
    tb.select(ToolId::Text);
    tb.press(doc, at(1.0f, 1.0f));
    tb.release(doc, at(1.0f, 1.0f));
    CHECK_FALSE(tb.textEditing());
}

TEST_CASE("Escape during the click itself cancels the new box") {
    TextFixture f;
    f.tb.press(f.doc, at(5.0f, 5.0f));
    CHECK(f.tb.textEditing());
    f.tb.cancel(f.doc);
    CHECK_FALSE(f.tb.textEditing());
    CHECK_FALSE(f.tb.gestureActive());
}
