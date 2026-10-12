#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/font_check.hpp"

using trace::looksLikeFont;

namespace {

void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    v[at] = static_cast<std::uint8_t>(x >> 24);
    v[at + 1] = static_cast<std::uint8_t>(x >> 16);
    v[at + 2] = static_cast<std::uint8_t>(x >> 8);
    v[at + 3] = static_cast<std::uint8_t>(x);
}

// A minimal sfnt: header plus a table directory naming `cmap` and one outline table. The tables themselves
// are empty and sit at the end of the data.
std::vector<std::uint8_t> sfnt(std::uint32_t version, const char* outlineTag) {
    std::vector<std::uint8_t> v(12 + 2 * 16 + 8, 0);
    put32(v, 0, version);
    v[5] = 2; // numTables
    std::memcpy(&v[12], "cmap", 4);
    put32(v, 12 + 8, 44);
    put32(v, 12 + 12, 4);
    std::memcpy(&v[28], outlineTag, 4);
    put32(v, 28 + 8, 48);
    put32(v, 28 + 12, 4);
    return v;
}

} // namespace

TEST_CASE("looksLikeFont accepts TrueType and CFF layouts") {
    CHECK(looksLikeFont(sfnt(0x00010000u, "glyf")));
    CHECK(looksLikeFont(sfnt(0x74727565u, "glyf")));
    CHECK(looksLikeFont(sfnt(0x4F54544Fu, "CFF ")));
    CHECK(looksLikeFont(sfnt(0x4F54544Fu, "CFF2")));
}

TEST_CASE("looksLikeFont rejects other files") {
    CHECK_FALSE(looksLikeFont({}));
    CHECK_FALSE(looksLikeFont(std::vector<std::uint8_t>(11, 0)));
    CHECK_FALSE(looksLikeFont(sfnt(0x89504E47u, "glyf"))); // a PNG header
    CHECK_FALSE(looksLikeFont(sfnt(0x74746366u, "glyf"))); // 'ttcf': collections are not supported
    CHECK_FALSE(looksLikeFont(sfnt(0x00010000u, "head"))); // no outlines
}

TEST_CASE("looksLikeFont rejects a table directory that does not fit or points outside the data") {
    auto tooMany = sfnt(0x00010000u, "glyf");
    tooMany[5] = 200; // directory would be longer than the data
    CHECK_FALSE(looksLikeFont(tooMany));

    auto zero = sfnt(0x00010000u, "glyf");
    zero[5] = 0;
    CHECK_FALSE(looksLikeFont(zero));

    auto outside = sfnt(0x00010000u, "glyf");
    put32(outside, 28 + 8, 0x7FFFFFF0u); // offset far past the end
    CHECK_FALSE(looksLikeFont(outside));

    auto overflow = sfnt(0x00010000u, "glyf");
    put32(overflow, 28 + 8, 0xFFFFFFFFu);
    put32(overflow, 28 + 12, 0xFFFFFFFFu); // offset + length would wrap a 32-bit sum
    CHECK_FALSE(looksLikeFont(overflow));
}
