#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>

#include "core/color.hpp"
#include "core/color_text.hpp"

using trace::blendOver;
using trace::eraseAlpha;
using trace::Rgba8;

namespace {

constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kBlack{0, 0, 0, 255};

bool near(int a, int b, int eps) {
    return std::abs(a - b) <= eps;
}

} // namespace

TEST_CASE("An opaque source replaces the destination") {
    CHECK(blendOver(kWhite, Rgba8{10, 20, 30, 255}) == Rgba8{10, 20, 30, 255});
    CHECK(blendOver(Rgba8{}, Rgba8{10, 20, 30, 255}) == Rgba8{10, 20, 30, 255});
}

TEST_CASE("A transparent source or zero coverage leaves the destination alone") {
    const Rgba8 dst{40, 50, 60, 200};
    CHECK(blendOver(dst, Rgba8{255, 0, 0, 0}) == dst);
    CHECK(blendOver(dst, Rgba8{255, 0, 0, 255}, 0) == dst);
}

TEST_CASE("Half coverage mixes the colors evenly") {
    const Rgba8 mixed = blendOver(kWhite, kBlack, 128);
    CHECK(near(mixed.r, 127, 1));
    CHECK(near(mixed.g, 127, 1));
    CHECK(near(mixed.b, 127, 1));
    CHECK(mixed.a == 255);
}

TEST_CASE("Blending onto a transparent pixel keeps the source color and scales the alpha") {
    const Rgba8 out = blendOver(Rgba8{}, Rgba8{200, 100, 50, 255}, 128);
    CHECK(out.r == 200);
    CHECK(out.g == 100);
    CHECK(out.b == 50);
    CHECK(near(out.a, 128, 1));
}

TEST_CASE("Translucent over translucent accumulates alpha without exceeding 255") {
    const Rgba8 out = blendOver(Rgba8{0, 0, 255, 128}, Rgba8{255, 0, 0, 128});
    CHECK(out.a > 128);
    CHECK(out.a < 255);
    CHECK(out.r > out.b); // the newer source dominates
}

TEST_CASE("Blending never overflows for any source alpha and coverage") {
    for (unsigned a = 0; a <= 255; a += 5) {
        for (unsigned c = 0; c <= 255; c += 5) {
            const Rgba8 out = blendOver(Rgba8{255, 255, 255, 255}, Rgba8{255, 255, 255, static_cast<std::uint8_t>(a)},
                                        c);
            CHECK(out == kWhite); // white over white stays exactly white at every setting
        }
    }
}

TEST_CASE("eraseAlpha removes opacity and keeps the color") {
    const Rgba8 px{10, 20, 30, 255};
    CHECK(eraseAlpha(px).a == 0);
    CHECK(eraseAlpha(px).r == 10);
    CHECK(near(eraseAlpha(px, 128).a, 127, 1));
    CHECK(eraseAlpha(px, 0) == px);
}

TEST_CASE("parseColor reads #RRGGBB and #RRGGBBAA") {
    using trace::parseColor;
    CHECK(parseColor("#102030") == Rgba8{0x10, 0x20, 0x30, 255});
    CHECK(parseColor("#10203040") == Rgba8{0x10, 0x20, 0x30, 0x40});
    CHECK(parseColor("#abCDef") == Rgba8{0xAB, 0xCD, 0xEF, 255});
}

TEST_CASE("parseColor rejects malformed text") {
    using trace::parseColor;
    CHECK_FALSE(parseColor("").has_value());
    CHECK_FALSE(parseColor("102030").has_value());
    CHECK_FALSE(parseColor("#12345").has_value());
    CHECK_FALSE(parseColor("#1234567").has_value());
    CHECK_FALSE(parseColor("#12345g").has_value());
    CHECK_FALSE(parseColor("# 12345").has_value());
}

TEST_CASE("formatColor round-trips through parseColor for every channel value") {
    using trace::formatColor;
    using trace::parseColor;
    CHECK(formatColor(Rgba8{1, 2, 255, 128}) == "#0102FF80");
    for (int v = 0; v < 256; ++v) {
        const Rgba8 c{static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(255 - v), static_cast<std::uint8_t>(v / 2),
                      static_cast<std::uint8_t>(v)};
        CHECK(parseColor(formatColor(c)) == c);
    }
}
