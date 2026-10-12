#include <doctest/doctest.h>

#include <cmath>

#include "core/view_transform.hpp"

using trace::Vec2f;
using trace::ViewTransform;

namespace {

bool near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

TEST_CASE("toImage inverts toView") {
    ViewTransform v;
    v.setDevicePixelRatio(1.5f);
    v.zoomAt(Vec2f{100.0f, 80.0f}, 3.0f);
    v.panBy(37.0f, -12.0f);

    const Vec2f image{123.25f, 45.5f};
    const Vec2f back = v.toImage(v.toView(image));
    CHECK(near(back.x, image.x));
    CHECK(near(back.y, image.y));
}

TEST_CASE("zoomAt keeps the point under the anchor fixed") {
    ViewTransform v;
    v.panBy(50.0f, 20.0f);

    const Vec2f anchor{300.0f, 200.0f};
    const Vec2f before = v.toImage(anchor);
    v.zoomByFactorAt(anchor, 2.5f);
    const Vec2f after = v.toImage(anchor);

    CHECK(near(v.zoom(), 2.5f));
    CHECK(near(before.x, after.x));
    CHECK(near(before.y, after.y));
}

TEST_CASE("zoom is clamped to the supported range") {
    ViewTransform v;
    v.zoomAt(Vec2f{0.0f, 0.0f}, 1000.0f);
    CHECK(v.zoom() == ViewTransform::kMaxZoom);
    v.zoomAt(Vec2f{0.0f, 0.0f}, 0.0f);
    CHECK(v.zoom() == ViewTransform::kMinZoom);
}

TEST_CASE("scale accounts for the device pixel ratio") {
    ViewTransform v;
    v.setDevicePixelRatio(2.0f);
    CHECK(near(v.scale(), 0.5f)); // 100% zoom is one device pixel, half a view unit

    v.setDevicePixelRatio(0.0f); // invalid values fall back to 1
    CHECK(near(v.scale(), 1.0f));
}

TEST_CASE("fit shrinks large images to fit and never enlarges small ones") {
    ViewTransform v;
    const Vec2f viewport{800.0f, 600.0f};

    v.fit(viewport, 4000, 3000, 0.0f);
    CHECK(near(v.zoom(), 0.2f));
    CHECK(near(v.origin().x, 0.0f));
    CHECK(near(v.origin().y, 0.0f));

    v.fit(viewport, 100, 100, 0.0f);
    CHECK(near(v.zoom(), 1.0f));
}

TEST_CASE("fit centers the image") {
    ViewTransform v;
    v.fit(Vec2f{1000.0f, 800.0f}, 200, 100, 0.0f);
    CHECK(near(v.origin().x, 400.0f));
    CHECK(near(v.origin().y, 350.0f));
}

TEST_CASE("actualSize resets to 100% and centers") {
    ViewTransform v;
    v.zoomAt(Vec2f{0.0f, 0.0f}, 8.0f);
    v.actualSize(Vec2f{1000.0f, 800.0f}, 200, 100);
    CHECK(near(v.zoom(), 1.0f));
    CHECK(near(v.origin().x, 400.0f));
    CHECK(near(v.origin().y, 350.0f));
}

TEST_CASE("fit ignores empty images") {
    ViewTransform v;
    v.zoomAt(Vec2f{0.0f, 0.0f}, 4.0f);
    v.fit(Vec2f{800.0f, 600.0f}, 0, 0);
    CHECK(near(v.zoom(), 4.0f));
}

TEST_CASE("Zoom limits default to 10% - 3200% and can be changed") {
    ViewTransform v;
    CHECK(near(v.minZoom(), 0.1f));
    CHECK(near(v.maxZoom(), 32.0f));
    v.zoomAt(Vec2f{0.0f, 0.0f}, 1000.0f);
    CHECK(near(v.zoom(), 32.0f));

    REQUIRE(v.setZoomLimits(0.05f, 8.0f));
    CHECK(near(v.minZoom(), 0.05f));
    CHECK(near(v.maxZoom(), 8.0f));
    CHECK(near(v.zoom(), 8.0f)); // the current zoom is pulled into the new range
    v.zoomAt(Vec2f{0.0f, 0.0f}, 0.001f);
    CHECK(near(v.zoom(), 0.05f));
    v.zoomAt(Vec2f{0.0f, 0.0f}, 100.0f);
    CHECK(near(v.zoom(), 8.0f));

    // Widening works too, and the limits apply to fit().
    REQUIRE(v.setZoomLimits(0.5f, 2.0f));
    v.fit(Vec2f{100.0f, 100.0f}, 100000, 100000, 0.0f);
    CHECK(near(v.zoom(), 0.5f));
}

TEST_CASE("Invalid zoom limits are refused and change nothing") {
    ViewTransform v;
    CHECK_FALSE(v.setZoomLimits(0.0f, 8.0f));
    CHECK_FALSE(v.setZoomLimits(-1.0f, 8.0f));
    CHECK_FALSE(v.setZoomLimits(4.0f, 2.0f));
    CHECK_FALSE(v.setZoomLimits(std::nanf(""), 2.0f));
    CHECK_FALSE(v.setZoomLimits(0.5f, std::nanf("")));
    CHECK_FALSE(v.setZoomLimits(0.5f, INFINITY));
    CHECK(near(v.minZoom(), 0.1f));
    CHECK(near(v.maxZoom(), 32.0f));
    CHECK(v.setZoomLimits(1.0f, 1.0f)); // a single allowed zoom is fine
    v.zoomAt(Vec2f{0.0f, 0.0f}, 5.0f);
    CHECK(near(v.zoom(), 1.0f));
}

TEST_CASE("fitRect centers the box and fits it with a margin") {
    ViewTransform v;
    v.fitRect(Vec2f{400.0f, 300.0f}, 1000.0f, 2000.0f, 3000.0f, 3000.0f, 20.0f);
    // Available 360 x 260 for a 2000 x 1000 box: width limits, 0.18 units per unit.
    CHECK(near(v.zoom(), 0.18f, 1e-4f));
    const Vec2f topLeft = v.toView(Vec2f{1000.0f, 2000.0f});
    const Vec2f bottomRight = v.toView(Vec2f{3000.0f, 3000.0f});
    CHECK(near(topLeft.x, 20.0f, 1e-2f));
    CHECK(near(bottomRight.x, 380.0f, 1e-2f));
    CHECK(near((topLeft.y + bottomRight.y) * 0.5f, 150.0f, 1e-2f)); // centered vertically
}

TEST_CASE("fitRect never zooms in past 100% and respects the limits") {
    ViewTransform v;
    v.fitRect(Vec2f{800.0f, 600.0f}, 0.0f, 0.0f, 10.0f, 10.0f);
    CHECK(near(v.zoom(), 1.0f));
    const Vec2f c = v.toView(Vec2f{5.0f, 5.0f});
    CHECK(near(c.x, 400.0f));
    CHECK(near(c.y, 300.0f));

    v.setZoomLimits(0.05f, 8.0f);
    v.fitRect(Vec2f{800.0f, 600.0f}, -1.0e6f, -1.0e6f, 1.0e6f, 1.0e6f);
    CHECK(near(v.zoom(), 0.05f)); // the box is far too big: the lower limit holds

    ViewTransform dpr;
    dpr.setDevicePixelRatio(2.0f);
    dpr.fitRect(Vec2f{100.0f, 100.0f}, 0.0f, 0.0f, 400.0f, 400.0f, 0.0f);
    // 100 view units for 400 board units is 0.25 units per unit = 0.5 device pixels per unit.
    CHECK(near(dpr.zoom(), 0.5f, 1e-4f));
    CHECK(near(dpr.scale(), 0.25f, 1e-4f));
}

TEST_CASE("fitRect handles empty, inverted and non-finite boxes") {
    ViewTransform v;
    v.zoomAt(Vec2f{0.0f, 0.0f}, 3.0f);
    v.panBy(11.0f, 22.0f);
    const Vec2f beforeOrigin = v.origin();

    // Non-finite corners change nothing.
    v.fitRect(Vec2f{100.0f, 100.0f}, std::nanf(""), 0.0f, 1.0f, 1.0f);
    v.fitRect(Vec2f{100.0f, 100.0f}, 0.0f, 0.0f, INFINITY, 1.0f);
    CHECK(near(v.zoom(), 3.0f));
    CHECK(near(v.origin().x, beforeOrigin.x));
    CHECK(near(v.origin().y, beforeOrigin.y));

    // A point: 100% zoom, centered on it.
    v.fitRect(Vec2f{200.0f, 100.0f}, 50.0f, 60.0f, 50.0f, 60.0f);
    CHECK(near(v.zoom(), 1.0f));
    CHECK(near(v.toView(Vec2f{50.0f, 60.0f}).x, 100.0f));
    CHECK(near(v.toView(Vec2f{50.0f, 60.0f}).y, 50.0f));

    // A line: only its extent limits the zoom.
    v.fitRect(Vec2f{300.0f, 200.0f}, 0.0f, 0.0f, 1000.0f, 0.0f, 50.0f);
    CHECK(near(v.zoom(), 0.2f, 1e-4f));

    // An inverted box uses its positive axis only; a tiny view never divides by zero.
    v.fitRect(Vec2f{0.0f, 0.0f}, 10.0f, 10.0f, 0.0f, 0.0f, 16.0f);
    CHECK(std::isfinite(v.zoom()));
    CHECK(std::isfinite(v.origin().x));
}

// --- fit for pixel art ---------------------------------------------------------------------------

TEST_CASE("fit stops at 100% unless the ceiling is raised") {
    ViewTransform v;
    v.fit(Vec2f{800.0f, 600.0f}, 64, 64, 0.0f);
    CHECK(near(v.zoom(), 1.0f));

    v.fit(Vec2f{800.0f, 600.0f}, 64, 64, 0.0f, 32.0f);
    CHECK(near(v.zoom(), 600.0f / 64.0f)); // limited by the shorter side
}

TEST_CASE("fit in whole steps rounds a zoom above 100% down to a whole number") {
    ViewTransform v;
    v.fit(Vec2f{800.0f, 600.0f}, 64, 64, 0.0f, 32.0f, true);
    CHECK(near(v.zoom(), 9.0f)); // 9.375 -> 9
    // The picture is still centered.
    CHECK(near(v.origin().x, (800.0f - 64.0f * 9.0f) * 0.5f));
    CHECK(near(v.origin().y, (600.0f - 64.0f * 9.0f) * 0.5f));
}

TEST_CASE("fit in whole steps leaves zooms below 100% alone and respects the zoom limit") {
    ViewTransform v;
    v.fit(Vec2f{400.0f, 300.0f}, 2000, 1000, 0.0f, 32.0f, true);
    CHECK(near(v.zoom(), 0.2f)); // 400 / 2000, not rounded

    v.fit(Vec2f{4000.0f, 3000.0f}, 4, 4, 0.0f, 100.0f, true);
    CHECK(near(v.zoom(), ViewTransform::kMaxZoom)); // clamped by the view's own limit
}

TEST_CASE("fit with a bad ceiling falls back to 100%") {
    ViewTransform v;
    v.fit(Vec2f{800.0f, 600.0f}, 64, 64, 0.0f, -3.0f);
    CHECK(near(v.zoom(), 1.0f));
    v.fit(Vec2f{800.0f, 600.0f}, 64, 64, 0.0f, std::nanf(""));
    CHECK(near(v.zoom(), 1.0f));
}
