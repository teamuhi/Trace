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
