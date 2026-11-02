#include <doctest/doctest.h>

#include "core/board_export.hpp"
#include "core/document.hpp"

using namespace trace;

namespace {

constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kBlack{0, 0, 0, 255};
constexpr Rgba8 kWhite{255, 255, 255, 255};

BoardExportOptions plain() {
    BoardExportOptions o;
    o.padAll = 0;
    return o;
}

} // namespace

TEST_CASE("planBoardExport adds the border on every side, or on each side on its own") {
    BoardExportOptions o = plain();
    o.padAll = 10;
    BoardExportPlan plan = planBoardExport(IRect{100, 100, 140, 120}, o);
    REQUIRE(plan.ok());
    CHECK(plan.source == IRect{90, 90, 150, 130});
    CHECK(plan.width == 60);
    CHECK(plan.height == 40);

    o.uniformPad = false;
    o.padLeft = 0;
    o.padTop = 1;
    o.padRight = 2;
    o.padBottom = 3;
    plan = planBoardExport(IRect{100, 100, 140, 120}, o);
    REQUIRE(plan.ok());
    CHECK(plan.source == IRect{100, 99, 142, 123});
}

TEST_CASE("planBoardExport sizes the picture by percentage, width, height or longest side") {
    BoardExportOptions o = plain();
    const IRect region{0, 0, 200, 100};

    o.size = BoardSizeMode::Scale;
    o.scalePct = 50;
    BoardExportPlan plan = planBoardExport(region, o);
    CHECK(plan.width == 100);
    CHECK(plan.height == 50);

    o.size = BoardSizeMode::Width;
    o.target = 400;
    plan = planBoardExport(region, o);
    CHECK(plan.width == 400);
    CHECK(plan.height == 200);

    o.size = BoardSizeMode::Height;
    o.target = 25;
    plan = planBoardExport(region, o);
    CHECK(plan.width == 50);
    CHECK(plan.height == 25);

    o.size = BoardSizeMode::LongEdge;
    o.target = 1000;
    plan = planBoardExport(region, o);
    CHECK(plan.width == 1000);
    CHECK(plan.height == 500);
    plan = planBoardExport(IRect{0, 0, 100, 200}, o);
    CHECK(plan.width == 500);
    CHECK(plan.height == 1000);
}

TEST_CASE("planBoardExport refuses an empty region and a picture that is too large") {
    BoardExportOptions o = plain();
    BoardExportPlan plan = planBoardExport(IRect{}, o);
    CHECK_FALSE(plan.ok());
    CHECK(plan.problem == BoardExportPlan::Problem::Empty);

    o.size = BoardSizeMode::Scale;
    o.scalePct = BoardExportOptions::kMaxScalePct;
    plan = planBoardExport(IRect{0, 0, 4000, 4000}, o); // 32000 px wide at 800%
    CHECK_FALSE(plan.ok());
    CHECK(plan.problem == BoardExportPlan::Problem::TooLarge);

    o.scalePct = 100;
    o.padAll = BoardExportOptions::kMaxPad;
    plan = planBoardExport(IRect{0, 0, 10, 10}, o);
    CHECK_FALSE(plan.ok()); // 32778 px square is beyond the picture limit
}

TEST_CASE("renderBoardExport fills the border beyond the board with the background") {
    Document doc(10, 10, Rgba8{});
    doc.setEndless(true);
    doc.image().set(2, 3, kRed);
    doc.markDirty(IRect{2, 3, 3, 4});
    doc.commit();

    BoardExportOptions o = plain();
    o.padAll = 5; // reaches 5 pixels past the 0..10 board on the left and top
    const BoardExportPlan plan = planBoardExport(IRect{0, 0, 10, 10}, o);
    REQUIRE(plan.ok());

    const Image black = renderBoardExport(doc, plan, kBlack, true);
    REQUIRE(black.width() == 20);
    CHECK(black.get(0, 0) == kBlack);       // outside the board
    CHECK(black.get(5 + 2, 5 + 3) == kRed); // the drawn pixel, moved by the border
    CHECK(black.get(5 + 5, 5 + 5) == kBlack);

    const Image clear = renderBoardExport(doc, plan, Rgba8{}, true);
    CHECK(clear.get(0, 0).a == 0);
    CHECK(clear.get(5 + 2, 5 + 3) == kRed);
}

TEST_CASE("renderBoardExport scales, and leaves hidden layers out") {
    Document doc(4, 4, Rgba8{});
    doc.setEndless(true);
    doc.image().set(0, 0, kRed);
    doc.markDirty(IRect{0, 0, 1, 1});
    doc.commit();
    REQUIRE(doc.addLayer("top") == Document::OpResult::Applied);
    doc.image().set(1, 1, kRed);
    doc.markDirty(IRect{1, 1, 2, 2});
    doc.commit();
    LayerInfo hidden = doc.layer(doc.activeLayer());
    hidden.visible = false;
    REQUIRE(doc.setLayerProps(doc.activeLayer(), hidden) == Document::OpResult::Applied);

    BoardExportOptions o = plain();
    o.size = BoardSizeMode::Scale;
    o.scalePct = 200;
    o.smooth = false;
    const BoardExportPlan plan = planBoardExport(IRect{0, 0, 4, 4}, o);
    REQUIRE(plan.ok());
    const Image out = renderBoardExport(doc, plan, kWhite, false);
    REQUIRE(out.width() == 8);
    CHECK(out.get(0, 0) == kRed);
    CHECK(out.get(1, 1) == kRed);  // the lower layer's pixel, doubled
    CHECK(out.get(2, 2) == kWhite); // the hidden layer's pixel is not drawn
}
