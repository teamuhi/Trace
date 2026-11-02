#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/image.hpp"
#include "core/text_raster.hpp"
#include "font_util.hpp"

using namespace trace;
using trace_test::inter;
using trace_test::mono;

namespace {

int covered(const TextBitmap& b) {
    int n = 0;
    for (const std::uint8_t v : b.coverage)
        n += v != 0 ? 1 : 0;
    return n;
}

bool onlyFullOrZero(const TextBitmap& b) {
    for (const std::uint8_t v : b.coverage) {
        if (v != 0 && v != 255)
            return false;
    }
    return true;
}

constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kRed{255, 0, 0, 255};

} // namespace

TEST_CASE("The test fonts load") {
    CHECK(inter().size() > 1000);
    CHECK(mono().size() > 1000);
}

TEST_CASE("sanitizeText keeps newline and tab and drops other control characters") {
    CHECK(sanitizeText("a\r\nb\tc\x01\x7f") == "a\nb\tc");
    CHECK(sanitizeText("").empty());
}

TEST_CASE("sanitizeText cuts to the limit without splitting a UTF-8 sequence") {
    // "é" is two bytes (C3 A9). A limit of 3 would end inside the second one.
    CHECK(sanitizeText("aaé", 3) == "aa");
    CHECK(sanitizeText("aaé", 4) == "aaé");
    CHECK(sanitizeText(std::string(10, 'x'), 4) == "xxxx");
    CHECK(sanitizeText("abc", 0).empty());
}

TEST_CASE("Text renders with ink, and a bigger size gives a bigger bitmap") {
    TextBitmap small;
    TextBitmap large;
    REQUIRE(rasterizeText(inter(), "Hello", 12, true, small));
    REQUIRE(rasterizeText(inter(), "Hello", 36, true, large));
    CHECK(covered(small) > 0);
    CHECK(large.width > small.width * 2);
    CHECK(large.height > small.height * 2);
    CHECK(covered(large) > covered(small) * 4);
}

TEST_CASE("Without antialiasing every pixel is fully covered or not at all") {
    TextBitmap hard;
    TextBitmap soft;
    REQUIRE(rasterizeText(inter(), "Sample 123", 20, false, hard));
    REQUIRE(rasterizeText(inter(), "Sample 123", 20, true, soft));
    CHECK(onlyFullOrZero(hard));
    CHECK_FALSE(onlyFullOrZero(soft));
}

TEST_CASE("Empty, whitespace-only and unusable input draw nothing") {
    TextBitmap b;
    CHECK_FALSE(rasterizeText(inter(), "", 20, true, b));
    CHECK(b.empty());
    CHECK_FALSE(rasterizeText(inter(), "   ", 20, true, b));
    CHECK_FALSE(rasterizeText(inter(), "\n\n", 20, true, b));
    CHECK_FALSE(rasterizeText(std::vector<std::uint8_t>{}, "Hi", 20, true, b));
    const std::vector<std::uint8_t> junk(4096, 0x41);
    CHECK_FALSE(rasterizeText(junk, "Hi", 20, true, b));
}

TEST_CASE("Newlines stack lines, and an extra line makes the bitmap taller") {
    TextBitmap one;
    TextBitmap two;
    TextBitmap three;
    REQUIRE(rasterizeText(inter(), "Hi", 20, true, one));
    REQUIRE(rasterizeText(inter(), "Hi\nHi", 20, true, two));
    REQUIRE(rasterizeText(inter(), "Hi\r\nHi\nHi", 20, true, three));
    CHECK(two.height == one.height * 2);
    CHECK(three.height == one.height * 3);
    CHECK(two.width == one.width);
    CHECK(covered(two) == covered(one) * 2);
}

TEST_CASE("The monospace font gives every character the same width") {
    TextBitmap narrow;
    TextBitmap wide;
    REQUIRE(rasterizeText(mono(), "iiii", 20, false, narrow));
    REQUIRE(rasterizeText(mono(), "WWWW", 20, false, wide));
    CHECK(narrow.width == wide.width);
}

TEST_CASE("Invalid UTF-8, missing glyphs and tabs never fail or crash") {
    TextBitmap b;
    CHECK(rasterizeText(inter(), "a\xC3(b\xFF\xFE" "c\xE2\x82", 20, true, b));
    CHECK(rasterizeText(inter(), "\xF0\x9F\x98\x80 ok", 20, true, b)); // an emoji the font lacks
    TextBitmap tabbed;
    TextBitmap plain;
    REQUIRE(rasterizeText(inter(), "a\tb", 20, false, tabbed));
    REQUIRE(rasterizeText(inter(), "ab", 20, false, plain));
    CHECK(tabbed.width > plain.width);
}

TEST_CASE("Sizes outside the supported range are clamped") {
    TextBitmap tiny;
    TextBitmap huge;
    CHECK(rasterizeText(inter(), "A", -50, true, tiny));
    CHECK(rasterizeText(inter(), "A", 0, true, tiny));
    CHECK(rasterizeText(inter(), "A", 1000000, true, huge));
    CHECK(huge.height <= 2000);
}

TEST_CASE("Very long lines and very tall texts stay bounded") {
    TextBitmap wide;
    REQUIRE(rasterizeText(inter(), std::string(kMaxTextBytes, 'W'), 200, true, wide));
    CHECK(wide.width <= 16384);

    std::string tall;
    for (int i = 0; i < 3000; ++i)
        tall += "W\n";
    TextBitmap b;
    REQUIRE(rasterizeText(inter(), tall, 200, true, b));
    CHECK(static_cast<std::size_t>(b.width) * static_cast<std::size_t>(b.height) <= (32u << 20));
}

TEST_CASE("compositeText paints the covered pixels and clips to the image") {
    TextBitmap b;
    REQUIRE(rasterizeText(inter(), "HI", 20, false, b));
    Image img(60, 40, kWhite);
    const IRect touched = compositeText(img, b, 5, 5, kRed);
    CHECK_FALSE(touched.empty());
    int red = 0;
    for (const Rgba8& p : img.pixels())
        red += p == kRed ? 1 : 0;
    CHECK(red == covered(b)); // hard edges and an opaque color: one red pixel per covered pixel
    CHECK(touched.x0 >= 5);
    CHECK(touched.y0 >= 5);
    CHECK(touched.x1 <= 5 + b.width);

    // Partly or wholly outside: clipped, never an error.
    Image clipped(10, 10, kWhite);
    CHECK_FALSE(compositeText(clipped, b, -5, -5, kRed).empty());
    CHECK(compositeText(clipped, b, 1000, 1000, kRed).empty());
    CHECK(compositeText(clipped, b, -100000, -100000, kRed).empty());
    CHECK(compositeText(clipped, b, 2000000000, 2000000000, kRed).empty());
    CHECK(compositeText(clipped, b, -2000000000, 3, kRed).empty());
}

TEST_CASE("compositeText respects the alpha of the color and ignores a fully transparent one") {
    TextBitmap b;
    REQUIRE(rasterizeText(inter(), "I", 30, false, b));
    Image img(40, 40, kWhite);
    CHECK(compositeText(img, b, 0, 0, Rgba8{255, 0, 0, 0}).empty());
    for (const Rgba8& p : img.pixels())
        CHECK(p == kWhite);

    CHECK_FALSE(compositeText(img, b, 0, 0, Rgba8{0, 0, 0, 128}).empty());
    bool grey = false;
    for (const Rgba8& p : img.pixels())
        grey |= p.r > 100 && p.r < 160;
    CHECK(grey);
}

TEST_CASE("compositeText on empty input does nothing") {
    Image img(10, 10, kWhite);
    CHECK(compositeText(img, TextBitmap{}, 0, 0, kRed).empty());
    TextBitmap b;
    REQUIRE(rasterizeText(inter(), "A", 12, true, b));
    Image none;
    CHECK(compositeText(none, b, 0, 0, kRed).empty());
}
