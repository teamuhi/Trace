#include <doctest/doctest.h>

#include <limits>
#include <utility>
#include <vector>

#include "core/transform.hpp"

using namespace trace;

namespace {

constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kBlack{0, 0, 0, 255};
constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kGreen{0, 255, 0, 255};
constexpr Rgba8 kBlue{0, 0, 255, 255};

// A 2 x 2 image: red, green / blue, white.
Image quad() {
    Image img(2, 2);
    img.set(0, 0, kRed);
    img.set(1, 0, kGreen);
    img.set(0, 1, kBlue);
    img.set(1, 1, kWhite);
    return img;
}

} // namespace

TEST_CASE("Nearest resize scales up in blocks") {
    const Image out = resizedImage(quad(), 4, 4, ResizeFilter::Nearest);
    REQUIRE(out.width() == 4);
    REQUIRE(out.height() == 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const Rgba8 expected = y < 2 ? (x < 2 ? kRed : kGreen) : (x < 2 ? kBlue : kWhite);
            CHECK(out.get(x, y) == expected);
        }
    }
}

TEST_CASE("Nearest resize down picks a source pixel and never invents colors") {
    Image img(4, 4, kBlack);
    img.set(0, 0, kRed);
    const Image out = resizedImage(img, 2, 2, ResizeFilter::Nearest);
    REQUIRE(out.width() == 2);
    for (const Rgba8& p : out.pixels())
        CHECK((p == kBlack || p == kRed));
}

TEST_CASE("Resizing to the same size copies the image") {
    const Image src = quad();
    for (const ResizeFilter filter : {ResizeFilter::Nearest, ResizeFilter::Bilinear}) {
        const Image out = resizedImage(src, 2, 2, filter);
        CHECK(std::vector<Rgba8>(out.pixels().begin(), out.pixels().end()) ==
              std::vector<Rgba8>(src.pixels().begin(), src.pixels().end()));
    }
}

TEST_CASE("Resize rejects empty sources and sizes out of range") {
    CHECK(resizedImage(Image(), 4, 4, ResizeFilter::Nearest).empty());
    CHECK(resizedImage(quad(), 0, 4, ResizeFilter::Nearest).empty());
    CHECK(resizedImage(quad(), 4, -1, ResizeFilter::Bilinear).empty());
    CHECK(resizedImage(quad(), kMaxImageSide + 1, 4, ResizeFilter::Bilinear).empty());
}

TEST_CASE("Bilinear resize keeps a flat color flat, at any scale") {
    const Rgba8 teal{10, 120, 130, 255};
    const Image src(7, 5, teal);
    const std::vector<std::pair<int, int>> sizes = {{1, 1}, {3, 2}, {7, 5}, {20, 13}, {100, 3}};
    for (const auto& [w, h] : sizes) {
        const Image out = resizedImage(src, w, h, ResizeFilter::Bilinear);
        REQUIRE(out.width() == w);
        REQUIRE(out.height() == h);
        for (const Rgba8& p : out.pixels())
            CHECK(p == teal);
    }
}

TEST_CASE("Bilinear downscale averages the pixels it covers") {
    const Image out = resizedImage(quad(), 1, 1, ResizeFilter::Bilinear);
    REQUIRE(out.width() == 1);
    // (255+0+0+255)/4, (0+255+0+255)/4, (0+0+255+255)/4
    const Rgba8 p = out.get(0, 0);
    CHECK(p.a == 255);
    CHECK(p.r == doctest::Approx(128).epsilon(0.02));
    CHECK(p.g == doctest::Approx(128).epsilon(0.02));
    CHECK(p.b == doctest::Approx(128).epsilon(0.02));
}

TEST_CASE("Bilinear resize does not tint opaque pixels with transparent neighbours") {
    Image img(2, 1);
    img.set(0, 0, kRed);
    img.set(1, 0, Rgba8{0, 0, 0, 0}); // fully transparent
    const Image out = resizedImage(img, 1, 1, ResizeFilter::Bilinear);
    const Rgba8 p = out.get(0, 0);
    CHECK(p.r == 255); // still pure red, only less opaque
    CHECK(p.g == 0);
    CHECK(p.b == 0);
    CHECK(p.a > 100);
    CHECK(p.a < 160);
}

TEST_CASE("Bilinear resize of a fully transparent image stays transparent") {
    const Image out = resizedImage(Image(4, 4), 9, 2, ResizeFilter::Bilinear);
    for (const Rgba8& p : out.pixels())
        CHECK(p == Rgba8{});
}

TEST_CASE("Extreme aspect ratios resize without reading out of range") {
    const Image tall = resizedImage(Image(3, 3, kRed), 1, 600, ResizeFilter::Bilinear);
    CHECK(tall.height() == 600);
    const Image wide = resizedImage(Image(600, 2, kRed), 1, 1, ResizeFilter::Bilinear);
    CHECK(wide.get(0, 0) == kRed);
    const Image single = resizedImage(Image(1, 1, kGreen), 50, 50, ResizeFilter::Bilinear);
    CHECK(single.get(49, 49) == kGreen);
}

TEST_CASE("anchorOffset places the old image on a bigger canvas") {
    // 10 x 10 on a 20 x 30 canvas
    using P = std::pair<int, int>;
    CHECK(anchorOffset(Anchor::TopLeft, 10, 10, 20, 30) == P{0, 0});
    CHECK(anchorOffset(Anchor::Top, 10, 10, 20, 30) == P{5, 0});
    CHECK(anchorOffset(Anchor::TopRight, 10, 10, 20, 30) == P{10, 0});
    CHECK(anchorOffset(Anchor::Left, 10, 10, 20, 30) == P{0, 10});
    CHECK(anchorOffset(Anchor::Center, 10, 10, 20, 30) == P{5, 10});
    CHECK(anchorOffset(Anchor::Right, 10, 10, 20, 30) == P{10, 10});
    CHECK(anchorOffset(Anchor::BottomLeft, 10, 10, 20, 30) == P{0, 20});
    CHECK(anchorOffset(Anchor::Bottom, 10, 10, 20, 30) == P{5, 20});
    CHECK(anchorOffset(Anchor::BottomRight, 10, 10, 20, 30) == P{10, 20});
}

TEST_CASE("anchorOffset rounds the center consistently when shrinking") {
    using P = std::pair<int, int>;
    CHECK(anchorOffset(Anchor::Center, 10, 10, 7, 7) == P{-2, -2}); // floor(-1.5)
    CHECK(anchorOffset(Anchor::Center, 10, 10, 8, 8) == P{-1, -1});
    CHECK(anchorOffset(Anchor::Center, 10, 10, 11, 11) == P{0, 0}); // floor(0.5)
    CHECK(anchorOffset(Anchor::BottomRight, 10, 10, 4, 6) == P{-6, -4});
}

TEST_CASE("Growing the canvas keeps the picture at the anchor and fills the rest") {
    const Rgba8 fill{9, 9, 9, 255};
    const Image out = canvasResized(quad(), 4, 3, Anchor::BottomRight, fill);
    REQUIRE(out.width() == 4);
    REQUIRE(out.height() == 3);
    CHECK(out.get(2, 1) == kRed);
    CHECK(out.get(3, 1) == kGreen);
    CHECK(out.get(2, 2) == kBlue);
    CHECK(out.get(3, 2) == kWhite);
    CHECK(out.get(0, 0) == fill);
    CHECK(out.get(1, 2) == fill);
    CHECK(out.get(3, 0) == fill);
}

TEST_CASE("Shrinking the canvas cuts off what no longer fits") {
    Image src(4, 4, kBlack);
    src.set(3, 3, kRed);
    src.set(0, 0, kGreen);

    const Image topLeft = canvasResized(src, 2, 2, Anchor::TopLeft, kWhite);
    CHECK(topLeft.get(0, 0) == kGreen);
    CHECK(topLeft.get(1, 1) == kBlack);

    const Image bottomRight = canvasResized(src, 2, 2, Anchor::BottomRight, kWhite);
    CHECK(bottomRight.get(1, 1) == kRed);
    CHECK(bottomRight.get(0, 0) == kBlack);
}

TEST_CASE("Growing one side while shrinking the other works") {
    Image src(4, 4, kBlack);
    src.set(0, 3, kRed);
    const Image out = canvasResized(src, 2, 6, Anchor::TopLeft, kWhite);
    REQUIRE(out.width() == 2);
    REQUIRE(out.height() == 6);
    CHECK(out.get(0, 3) == kRed);
    CHECK(out.get(0, 4) == kWhite);
    CHECK(out.get(1, 3) == kBlack);
}

TEST_CASE("Canvas size rejects out of range sizes and accepts an empty source") {
    CHECK(canvasResized(quad(), 0, 4, Anchor::Center, kWhite).empty());
    CHECK(canvasResized(quad(), 4, kMaxImageSide + 1, Anchor::Center, kWhite).empty());
    const Image fromNothing = canvasResized(Image(), 3, 2, Anchor::Center, kRed);
    CHECK(fromNothing.width() == 3);
    CHECK(fromNothing.get(2, 1) == kRed);
}

TEST_CASE("Cropping copies the rectangle and clips it to the image") {
    Image src(5, 4, kBlack);
    src.set(2, 1, kRed);
    src.set(4, 3, kGreen);

    const Image inner = cropped(src, IRect{2, 1, 4, 3});
    REQUIRE(inner.width() == 2);
    REQUIRE(inner.height() == 2);
    CHECK(inner.get(0, 0) == kRed);

    const Image clipped = cropped(src, IRect{3, 2, 100, 100});
    REQUIRE(clipped.width() == 2);
    REQUIRE(clipped.height() == 2);
    CHECK(clipped.get(1, 1) == kGreen);

    CHECK(cropped(src, IRect{-5, -5, 100, 100}).width() == 5);
    CHECK(cropped(src, IRect{10, 10, 12, 12}).empty());
    CHECK(cropped(src, IRect{}).empty());
    CHECK(cropped(Image(), IRect{0, 0, 2, 2}).empty());
}

TEST_CASE("scaledSide rounds, never reaches zero and respects the limit") {
    CHECK(scaledSide(100, 0.5) == 50);
    CHECK(scaledSide(3, 0.5) == 2); // 1.5 rounds up
    CHECK(scaledSide(1, 0.1) == 1);
    CHECK(scaledSide(10000, 100.0) == kMaxImageSide);
    CHECK(scaledSide(10, 0.0) == 10);
    CHECK(scaledSide(10, -2.0) == 10);
    CHECK(scaledSide(10, std::numeric_limits<double>::quiet_NaN()) == 10);
    CHECK(scaledSide(10, std::numeric_limits<double>::infinity()) == 10);
}
