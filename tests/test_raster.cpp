#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <limits>

#include "core/raster.hpp"

using namespace trace;

namespace {

constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kBlack{0, 0, 0, 255};
constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kGreen{0, 255, 0, 255};

int count(const Image& img, Rgba8 color) {
    int n = 0;
    for (const Rgba8& p : img.pixels())
        n += p == color ? 1 : 0;
    return n;
}

int maxChannelDiff(const Image& a, const Image& b) {
    int worst = 0;
    for (std::size_t i = 0; i < a.pixels().size(); ++i) {
        const Rgba8 p = a.pixels()[i];
        const Rgba8 q = b.pixels()[i];
        worst = std::max({worst, std::abs(p.r - q.r), std::abs(p.g - q.g), std::abs(p.b - q.b), std::abs(p.a - q.a)});
    }
    return worst;
}

} // namespace

// --- drawLine ------------------------------------------------------------------------------------

TEST_CASE("drawLine paints a horizontal run including both endpoints") {
    Image img(10, 5, kWhite);
    const IRect touched = drawLine(img, 1, 2, 6, 2, Paint{kBlack});

    CHECK(touched == IRect{1, 2, 7, 3});
    CHECK(count(img, kBlack) == 6);
    for (int x = 1; x <= 6; ++x)
        CHECK(img.get(x, 2) == kBlack);
}

TEST_CASE("drawLine diagonal hits every diagonal pixel and nothing else") {
    Image img(6, 6, kWhite);
    drawLine(img, 0, 0, 4, 4, Paint{kBlack});

    CHECK(count(img, kBlack) == 5);
    for (int i = 0; i <= 4; ++i)
        CHECK(img.get(i, i) == kBlack);
}

TEST_CASE("drawLine is symmetric in its endpoints") {
    Image a(20, 20, kWhite);
    Image b(20, 20, kWhite);
    drawLine(a, 2, 3, 17, 9, Paint{kBlack});
    drawLine(b, 17, 9, 2, 3, Paint{kBlack});
    CHECK(maxChannelDiff(a, b) == 0);
}

TEST_CASE("drawLine can skip its start pixel so joints are painted once") {
    Image img(10, 3, kWhite);
    const Paint translucent{Rgba8{0, 0, 0, 128}};
    drawLine(img, 0, 1, 4, 1, translucent);
    drawLine(img, 4, 1, 8, 1, translucent, false);

    // The joint at x = 4 was blended exactly once.
    CHECK(img.get(4, 1) == img.get(3, 1));
    CHECK(img.get(4, 1) == img.get(5, 1));
}

TEST_CASE("drawLine clips to the image, including absurd coordinates") {
    Image img(8, 5, kWhite);
    CHECK(drawLine(img, -10, 2, 20, 2, Paint{kBlack}) == IRect{0, 2, 8, 3});
    CHECK(count(img, kBlack) == 8);

    Image big(8, 5, kWhite);
    drawLine(big, -100000, 0, 100000, 0, Paint{kBlack});
    CHECK(count(big, kBlack) == 8);

    Image off(8, 5, kWhite);
    CHECK(drawLine(off, 20, 20, 30, 30, Paint{kBlack}).empty());
    CHECK(count(off, kWhite) == 40);
}

TEST_CASE("drawLine on an empty image is a no-op") {
    Image none;
    CHECK(drawLine(none, 0, 0, 5, 5, Paint{kBlack}).empty());
}

// --- drawCapsule ---------------------------------------------------------------------------------

TEST_CASE("A hard radius-0.5 capsule is exactly one pixel, wherever in the pixel the point falls") {
    for (const Vec2f p : {Vec2f{3.2f, 4.7f}, Vec2f{3.0f, 4.0f}, Vec2f{3.99f, 4.99f}}) {
        Image img(8, 8, kWhite);
        drawCapsule(img, p, p, 0.5f, Paint{kBlack}, false);
        CHECK(count(img, kBlack) == 1);
        CHECK(img.get(static_cast<int>(p.x), static_cast<int>(p.y)) == kBlack);
    }
}

TEST_CASE("Capsule rejects non-finite or non-positive input without drawing") {
    Image img(8, 8, kWhite);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    CHECK(drawCapsule(img, Vec2f{nan, 1}, Vec2f{2, 2}, 1.0f, Paint{kBlack}, true).empty());
    CHECK(drawCapsule(img, Vec2f{1, 1}, Vec2f{inf, 2}, 1.0f, Paint{kBlack}, true).empty());
    CHECK(drawCapsule(img, Vec2f{1, 1}, Vec2f{2, 2}, nan, Paint{kBlack}, true).empty());
    CHECK(drawCapsule(img, Vec2f{1, 1}, Vec2f{2, 2}, 0.0f, Paint{kBlack}, true).empty());
    CHECK(drawCapsule(img, Vec2f{1, 1}, Vec2f{2, 2}, -3.0f, Paint{kBlack}, true).empty());
    CHECK(count(img, kWhite) == 64);
}

TEST_CASE("Antialiased capsule: solid core, soft edge, nothing beyond") {
    Image img(21, 21, kWhite);
    drawCapsule(img, Vec2f{10.5f, 10.5f}, Vec2f{10.5f, 10.5f}, 3.0f, Paint{kBlack}, true);

    CHECK(img.get(10, 10) == kBlack);
    const Rgba8 edge = img.get(13, 10); // distance 3 from the center: half covered
    CHECK(edge.r > 0);
    CHECK(edge.r < 255);
    CHECK(img.get(15, 10) == kWhite);
}

TEST_CASE("Capsule far outside the image paints nothing and does not overflow") {
    Image img(8, 8, kWhite);
    CHECK(drawCapsule(img, Vec2f{1e9f, 1e9f}, Vec2f{-1e9f, 1e9f}, 4.0f, Paint{kBlack}, true).empty());
    CHECK(count(img, kWhite) == 64);
}

TEST_CASE("Capsule can paint transparency") {
    Image img(9, 9, kWhite);
    drawCapsule(img, Vec2f{4.5f, 4.5f}, Vec2f{4.5f, 4.5f}, 2.0f, Paint{Rgba8{}, BlendMode::EraseAlpha}, false);
    CHECK(img.get(4, 4).a == 0);
    CHECK(img.get(0, 0) == kWhite);
}

// --- StrokeMask ----------------------------------------------------------------------------------

TEST_CASE("A masked stroke of many short capsules matches one long capsule") {
    const float radius = 3.0f;
    Image reference(40, 21, kWhite);
    drawCapsule(reference, Vec2f{5.5f, 10.5f}, Vec2f{30.5f, 10.5f}, radius, Paint{kBlack}, true);

    Image masked(40, 21, kWhite);
    Image unmasked(40, 21, kWhite);
    StrokeMask mask;
    mask.begin(40, 21);
    for (int i = 0; i < 25; ++i) {
        const Vec2f a{5.5f + static_cast<float>(i), 10.5f};
        const Vec2f b{6.5f + static_cast<float>(i), 10.5f};
        drawCapsule(masked, a, b, radius, Paint{kBlack}, true, &mask);
        drawCapsule(unmasked, a, b, radius, Paint{kBlack}, true);
    }

    CHECK(maxChannelDiff(masked, reference) <= 8);   // rounding only
    CHECK(maxChannelDiff(unmasked, reference) > 40); // the compounding the mask exists to prevent
}

TEST_CASE("A mask makes overlapping translucent capsules composite once") {
    const Paint translucent{Rgba8{0, 0, 0, 128}};

    Image single(12, 12, kWhite);
    drawCapsule(single, Vec2f{5.5f, 5.5f}, Vec2f{5.5f, 5.5f}, 2.0f, translucent, false);

    Image masked(12, 12, kWhite);
    StrokeMask mask;
    mask.begin(12, 12);
    for (int i = 0; i < 5; ++i)
        drawCapsule(masked, Vec2f{5.5f, 5.5f}, Vec2f{5.5f, 5.5f}, 2.0f, translucent, false, &mask);

    CHECK(maxChannelDiff(masked, single) == 0);
}

TEST_CASE("StrokeMask end resets it for the next stroke") {
    Image img(10, 10, kWhite);
    StrokeMask mask;
    mask.begin(10, 10);
    drawCapsule(img, Vec2f{5.5f, 5.5f}, Vec2f{5.5f, 5.5f}, 2.0f, Paint{kBlack}, false, &mask);
    CHECK(mask.get(5, 5) == 255);
    mask.end();
    CHECK(mask.get(5, 5) == 0);

    mask.begin(10, 10);
    CHECK(mask.get(5, 5) == 0);
}

TEST_CASE("A mask of the wrong size is ignored instead of corrupting memory") {
    Image img(10, 10, kWhite);
    StrokeMask mask;
    mask.begin(2, 2);
    drawCapsule(img, Vec2f{5.5f, 5.5f}, Vec2f{5.5f, 5.5f}, 2.0f, Paint{kBlack}, false, &mask);
    CHECK(img.get(5, 5) == kBlack);
}

// --- rectangles ----------------------------------------------------------------------------------

TEST_CASE("fillRect clips to the image") {
    Image img(10, 10, kWhite);
    CHECK(fillRect(img, IRect{-5, -5, 3, 3}, Paint{kBlack}) == IRect{0, 0, 3, 3});
    CHECK(count(img, kBlack) == 9);

    CHECK(fillRect(img, IRect{20, 20, 30, 30}, Paint{kBlack}).empty());
    CHECK(fillRect(img, IRect{5, 5, 5, 9}, Paint{kBlack}).empty());
}

TEST_CASE("strokeRect draws an outline inside its rectangle") {
    Image img(10, 10, kWhite);
    strokeRect(img, IRect{0, 0, 10, 10}, 2, Paint{kBlack});

    CHECK(img.get(0, 0) == kBlack);
    CHECK(img.get(9, 9) == kBlack);
    CHECK(img.get(5, 1) == kBlack);
    CHECK(img.get(1, 5) == kBlack);
    CHECK(img.get(5, 2) == kWhite);
    CHECK(img.get(5, 5) == kWhite);
    CHECK(count(img, kBlack) == 100 - 6 * 6);
}

TEST_CASE("strokeRect that would meet itself becomes a solid fill") {
    Image img(10, 10, kWhite);
    strokeRect(img, IRect{0, 0, 10, 10}, 5, Paint{kBlack});
    CHECK(count(img, kBlack) == 100);

    Image thin(10, 10, kWhite);
    strokeRect(thin, IRect{2, 2, 5, 9}, 3, Paint{kBlack}); // 3 wide: a 3-pixel outline fills it
    CHECK(count(thin, kBlack) == 3 * 7);
}

TEST_CASE("strokeRect ignores bad thickness") {
    Image img(10, 10, kWhite);
    CHECK(strokeRect(img, IRect{0, 0, 10, 10}, 0, Paint{kBlack}).empty());
    CHECK(strokeRect(img, IRect{0, 0, 10, 10}, -4, Paint{kBlack}).empty());
    CHECK(count(img, kWhite) == 100);
}

// --- ellipses ------------------------------------------------------------------------------------

TEST_CASE("fillEllipse is symmetric in both axes and under transposition for a circle") {
    Image img(11, 11, kWhite);
    fillEllipse(img, IRect{0, 0, 11, 11}, Paint{kBlack});

    CHECK(img.get(5, 5) == kBlack);
    CHECK(img.get(0, 0) == kWhite);
    CHECK(img.get(10, 10) == kWhite);
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11; ++x) {
            CHECK(img.get(x, y) == img.get(10 - x, y));
            CHECK(img.get(x, y) == img.get(x, 10 - y));
            CHECK(img.get(x, y) == img.get(y, x));
        }
    }
}

TEST_CASE("fillEllipse handles tiny boxes") {
    Image one(4, 4, kWhite);
    fillEllipse(one, IRect{1, 1, 2, 2}, Paint{kBlack});
    CHECK(count(one, kBlack) == 1);
    CHECK(one.get(1, 1) == kBlack);

    Image two(4, 4, kWhite);
    fillEllipse(two, IRect{1, 1, 3, 3}, Paint{kBlack});
    CHECK(count(two, kBlack) == 4);

    Image line(6, 3, kWhite);
    fillEllipse(line, IRect{0, 1, 6, 2}, Paint{kBlack}); // one pixel tall
    CHECK(count(line, kBlack) == 6);
}

TEST_CASE("strokeEllipse leaves a hole and has the requested thickness on the axes") {
    Image img(20, 20, kWhite);
    strokeEllipse(img, IRect{0, 0, 20, 20}, 3, Paint{kBlack});

    CHECK(img.get(10, 10) == kWhite);
    CHECK(img.get(0, 10) == kBlack);
    CHECK(img.get(2, 10) == kBlack);
    CHECK(img.get(3, 10) == kWhite);
    CHECK(img.get(10, 0) == kBlack);
    CHECK(img.get(10, 2) == kBlack);
    CHECK(img.get(10, 3) == kWhite);
}

TEST_CASE("An ellipse stroke and its inset fill partition the full ellipse exactly") {
    const IRect box{2, 1, 23, 16};
    for (const int t : {1, 2, 3, 5}) {
        Image whole(26, 18, kWhite);
        fillEllipse(whole, box, Paint{kBlack});

        Image parts(26, 18, kWhite);
        fillEllipse(parts, box, Paint{kGreen}, t);
        strokeEllipse(parts, box, t, Paint{kRed});

        int mismatches = 0;
        for (int y = 0; y < 18; ++y) {
            for (int x = 0; x < 26; ++x) {
                const bool inWhole = whole.get(x, y) == kBlack;
                const bool inParts = parts.get(x, y) != kWhite;
                mismatches += inWhole != inParts ? 1 : 0;
            }
        }
        CHECK(mismatches == 0);
    }
}

TEST_CASE("Ellipse functions ignore degenerate input") {
    Image img(8, 8, kWhite);
    CHECK(fillEllipse(img, IRect{}, Paint{kBlack}).empty());
    CHECK(strokeEllipse(img, IRect{0, 0, 8, 8}, 0, Paint{kBlack}).empty());
    CHECK(fillEllipse(img, IRect{20, 20, 30, 30}, Paint{kBlack}).empty());
    CHECK(count(img, kWhite) == 64);
}

// --- floodFill -----------------------------------------------------------------------------------

TEST_CASE("floodFill stops at a wall") {
    Image img(5, 5, kWhite);
    for (int y = 0; y < 5; ++y)
        img.set(2, y, kBlack);

    const IRect touched = floodFill(img, 0, 0, kRed, 0);
    CHECK(touched == IRect{0, 0, 2, 5});
    CHECK(count(img, kRed) == 10);
    CHECK(count(img, kBlack) == 5);
    CHECK(img.get(4, 0) == kWhite);
}

TEST_CASE("floodFill leaks through a gap") {
    Image img(5, 5, kWhite);
    for (int y = 0; y < 4; ++y)
        img.set(2, y, kBlack);

    floodFill(img, 0, 0, kRed, 0);
    CHECK(count(img, kRed) == 21); // everything except the four wall pixels, gap included
    CHECK(count(img, kBlack) == 4);
}

TEST_CASE("floodFill is 4-connected: diagonal neighbors are not reached") {
    Image img(2, 2, kWhite);
    img.set(1, 0, kBlack);
    img.set(0, 1, kBlack);

    floodFill(img, 0, 0, kRed, 0);
    CHECK(img.get(0, 0) == kRed);
    CHECK(img.get(1, 1) == kWhite);
}

TEST_CASE("floodFill tolerance is measured against the seed pixel") {
    Image img(5, 1, kWhite);
    for (int x = 0; x < 5; ++x)
        img.set(x, 0, Rgba8{static_cast<std::uint8_t>(x * 10), 0, 0, 255});

    floodFill(img, 0, 0, kGreen, 15);
    CHECK(img.get(0, 0) == kGreen);
    CHECK(img.get(1, 0) == kGreen);
    CHECK(img.get(2, 0) != kGreen); // 20 away from the seed even though only 10 from its neighbor

    Image all(5, 1, kWhite);
    for (int x = 0; x < 5; ++x)
        all.set(x, 0, Rgba8{static_cast<std::uint8_t>(x * 10), 0, 0, 255});
    floodFill(all, 0, 0, kGreen, 255);
    CHECK(count(all, kGreen) == 5);
}

TEST_CASE("floodFill tolerance also considers alpha") {
    Image img(2, 1, kWhite);
    img.set(1, 0, Rgba8{255, 255, 255, 0});
    floodFill(img, 0, 0, kRed, 10);
    CHECK(img.get(1, 0).a == 0); // transparent white is not "the same" as opaque white
}

TEST_CASE("floodFill with the existing color, or outside the image, does nothing") {
    Image img(4, 4, kWhite);
    CHECK(floodFill(img, 1, 1, kWhite, 0).empty());
    CHECK(floodFill(img, -1, 0, kRed, 0).empty());
    CHECK(floodFill(img, 0, 4, kRed, 0).empty());
    CHECK(count(img, kWhite) == 16);
}

TEST_CASE("floodFill with a fill color inside the tolerance still terminates") {
    Image img(6, 6, Rgba8{100, 100, 100, 255});
    floodFill(img, 0, 0, Rgba8{101, 100, 100, 255}, 5);
    CHECK(count(img, Rgba8{101, 100, 100, 255}) == 36);
}

TEST_CASE("floodFill covers a large open image") {
    Image img(1500, 1500, kWhite);
    const IRect touched = floodFill(img, 750, 750, kRed, 0);
    CHECK(touched == IRect{0, 0, 1500, 1500});
    CHECK(count(img, kRed) == 1500 * 1500);
}

TEST_CASE("floodFill follows a long winding corridor") {
    // A serpentine path one pixel wide exercises the span stack without recursion.
    const int w = 41;
    const int h = 41;
    Image img(w, h, kBlack);
    int corridor = 0;
    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; ++x) {
            img.set(x, y, kWhite);
            ++corridor;
        }
        if (y + 2 < h) {
            const int gap = (y / 2) % 2 == 0 ? w - 1 : 0;
            img.set(gap, y + 1, kWhite);
            ++corridor;
        }
    }
    floodFill(img, 0, 0, kRed, 0);
    CHECK(count(img, kRed) == corridor);
}
