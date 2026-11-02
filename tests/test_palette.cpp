#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>

#include "core/image.hpp"
#include "core/palette.hpp"

using trace::addPaletteColor;
using trace::cleanPaletteName;
using trace::defaultPalette;
using trace::formatGpl;
using trace::formatHex;
using trace::Image;
using trace::kMaxPaletteColors;
using trace::kMaxPaletteFileBytes;
using trace::kMaxPaletteNameBytes;
using trace::Palette;
using trace::paletteFromImages;
using trace::parseGpl;
using trace::parseHex;
using trace::PaletteSort;
using trace::Rgba8;
using trace::sortPalette;

namespace {

Palette sample() {
    Palette p;
    p.name = "Sample";
    p.colors = {{255, 0, 0, 255}, {0, 255, 0, 255}, {1, 2, 3, 255}, {0, 0, 0, 255}};
    return p;
}

Palette colorsOnly(std::initializer_list<Rgba8> list) {
    Palette p;
    p.colors = list;
    return p;
}

} // namespace

TEST_CASE("The default palette is 27 opaque colors") {
    const Palette p = defaultPalette();
    CHECK(p.name == "Default");
    CHECK(p.colors.size() == 27);
    for (const Rgba8 c : p.colors)
        CHECK(c.a == 255);
    CHECK(p.colors.front() == Rgba8{0, 0, 0, 255});
}

TEST_CASE("A gpl file round trips") {
    const Palette p = sample();
    const std::string text = formatGpl(p);
    CHECK(text.starts_with("GIMP Palette\nName: Sample\nColumns: 4\n"));
    CHECK(text.back() == '\n');
    CHECK(text.find('\r') == std::string::npos);
    CHECK(parseGpl(text) == p);
}

TEST_CASE("A gpl file writes at most 8 columns and survives an empty palette") {
    Palette big;
    big.colors.assign(20, Rgba8{9, 9, 9, 255});
    CHECK(formatGpl(big).find("Columns: 8\n") != std::string::npos);

    const Palette empty;
    const std::string text = formatGpl(empty);
    CHECK(text.find("Columns: 0\n") != std::string::npos);
    CHECK(parseGpl(text) == empty);
}

TEST_CASE("A gpl file with a BOM, CRLF, comments and color names is read") {
    const std::string text = "\xEF\xBB\xBFGIMP Palette\r\nName: Test Pal\r\nColumns: 4\r\n# a comment\r\n\r\n"
                             "255   0   0\tRed\r\n  0 128 255 Sky blue\r\n# another\r\n10 20 30\r\n";
    const Palette p = parseGpl(text);
    CHECK(p.name == "Test Pal");
    REQUIRE(p.colors.size() == 3);
    CHECK(p.colors[0] == Rgba8{255, 0, 0, 255});
    CHECK(p.colors[1] == Rgba8{0, 128, 255, 255});
    CHECK(p.colors[2] == Rgba8{10, 20, 30, 255});
}

TEST_CASE("A gpl file without the header still yields its rows") {
    const Palette p = parseGpl("1 2 3\n4 5 6\n");
    CHECK(p.name.empty());
    CHECK(p.colors.size() == 2);
}

TEST_CASE("Malformed or out of range gpl rows are skipped") {
    const Palette p = parseGpl("GIMP Palette\n"
                               "256 0 0\n"      // out of range
                               "-1 0 0\n"       // negative
                               "1 2\n"          // missing channel
                               "a b c\n"        // not numbers
                               "1 2 3x\n"       // glued suffix
                               "1000 2 3\n"     // too many digits
                               "1.5 2 3\n"      // fractional
                               "7 8 9\n"        // the only good one
                               "99999999999999999999 1 1\n");
    REQUIRE(p.colors.size() == 1);
    CHECK(p.colors[0] == Rgba8{7, 8, 9, 255});
}

TEST_CASE("A gpl file stops at 256 colors") {
    std::string text = "GIMP Palette\n";
    for (int i = 0; i < 400; ++i)
        text += std::to_string(i % 256) + " 0 0\n";
    const Palette p = parseGpl(text);
    CHECK(p.colors.size() == kMaxPaletteColors);
    CHECK(p.colors.back() == Rgba8{255, 0, 0, 255});

    Palette big;
    big.colors.assign(300, Rgba8{1, 1, 1, 255});
    CHECK(parseGpl(formatGpl(big)).colors.size() == kMaxPaletteColors);
    CHECK(parseHex(formatHex(big)).colors.size() == kMaxPaletteColors);
}

TEST_CASE("A hex file round trips") {
    Palette p = sample();
    p.name.clear(); // the hex format has no name
    const std::string text = formatHex(p);
    CHECK(text == "FF0000\n00FF00\n010203\n000000\n");
    CHECK(parseHex(text) == p);
}

TEST_CASE("A hex file tolerates a BOM, CRLF, a hash, case and blank lines") {
    const Palette p = parseHex("\xEF\xBB\xBF#ff8000\r\n\r\n  00Ff7a  \r\n#102030");
    REQUIRE(p.colors.size() == 3);
    CHECK(p.colors[0] == Rgba8{255, 128, 0, 255});
    CHECK(p.colors[1] == Rgba8{0, 255, 122, 255});
    CHECK(p.colors[2] == Rgba8{16, 32, 48, 255});
    CHECK(p.name.empty());
}

TEST_CASE("Malformed hex lines are skipped") {
    const Palette p = parseHex("FFF\n"      // shorthand is not accepted
                               "GG0000\n"   // not hex
                               "FF00001\n"  // too long
                               "FF 00 00\n" // spaces inside
                               "\n"
                               "0A0B0C\n");
    REQUIRE(p.colors.size() == 1);
    CHECK(p.colors[0] == Rgba8{10, 11, 12, 255});
}

TEST_CASE("Hostile input ends quickly and stays inside the caps") {
    const auto start = std::chrono::steady_clock::now();

    // A color before the 1 MB cut is kept, one after it is dropped.
    std::string text = "1 2 3\n";
    text += std::string(kMaxPaletteFileBytes, '\n');
    text += "4 5 6\n";
    const Palette gpl = parseGpl(text);
    REQUIRE(gpl.colors.size() == 1);
    CHECK(gpl.colors[0] == Rgba8{1, 2, 3, 255});

    // Several megabytes in one line, and in many comment lines.
    CHECK(parseGpl(std::string(5u << 20, 'x')).colors.empty());
    CHECK(parseHex(std::string(5u << 20, 'F')).colors.empty());
    std::string comments;
    for (int i = 0; i < 300000; ++i)
        comments += "# c\n";
    CHECK(parseGpl(comments).colors.empty());

    // A name of megabytes ends up at the name cap.
    const Palette named = parseGpl("Name: " + std::string(500u << 10, 'n') + "\n1 1 1\n");
    CHECK(named.name.size() == kMaxPaletteNameBytes);
    CHECK(named.colors.size() == 1);

    // A line cut in half by the 1 MB limit is not read as a short color.
    std::string cutRow(kMaxPaletteFileBytes - 5, '\n'); // the cut falls right after "1 2 3"
    cutRow += "1 2 30\n";
    CHECK(parseGpl(cutRow).colors.empty());

    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < std::chrono::seconds(5));
}

TEST_CASE("Binary and empty input never throw and give nothing") {
    CHECK(parseGpl("").colors.empty());
    CHECK(parseHex("").colors.empty());
    constexpr char raw[] = "\0\1\2\xFF\xFE\0\n\0 0 0\0\n";
    const std::string junk(raw, sizeof(raw) - 1);
    CHECK_NOTHROW(parseGpl(junk));
    CHECK_NOTHROW(parseHex(junk));
    CHECK(parseGpl("\xEF\xBB\xBF").colors.empty());
}

TEST_CASE("Palette names are cleaned") {
    CHECK(cleanPaletteName("  Warm\tTones \r\n") == "WarmTones");
    CHECK(cleanPaletteName("A\x01" "B\x7F" "C") == "ABC");
    CHECK(cleanPaletteName("   ").empty());
    CHECK(cleanPaletteName("").empty());

    const std::string longName(200, 'x');
    CHECK(cleanPaletteName(longName).size() == kMaxPaletteNameBytes);

    // A cut that would land inside a multi-byte character backs up to its start.
    std::string utf8(kMaxPaletteNameBytes - 1, 'a');
    utf8 += "\xC3\xA9\xC3\xA9"; // two 2-byte characters straddling the limit
    const std::string cut = cleanPaletteName(utf8);
    CHECK(cut.size() == kMaxPaletteNameBytes - 1);
    CHECK(cut == std::string(kMaxPaletteNameBytes - 1, 'a'));

    // Spaces exposed by the cut are trimmed too.
    const std::string spaced = std::string(kMaxPaletteNameBytes - 1, 'a') + "  b";
    CHECK(cleanPaletteName(spaced) == std::string(kMaxPaletteNameBytes - 1, 'a'));
}

TEST_CASE("The gpl name is cleaned on parse and on format") {
    CHECK(parseGpl("GIMP Palette\nName:   My\x02 Pal  \n").name == "My Pal");
    Palette p;
    p.name = "Line\nBreak";
    const std::string text = formatGpl(p);
    CHECK(text.find("Name: LineBreak\n") != std::string::npos);
    CHECK(parseGpl(text).name == "LineBreak");
}

TEST_CASE("paletteFromImages orders by frequency, ties by first seen") {
    Image a(4, 1);
    a.set(0, 0, {10, 0, 0, 255}); // x1
    a.set(1, 0, {20, 0, 0, 255}); // x3 in total
    a.set(2, 0, {20, 0, 0, 255});
    a.set(3, 0, {30, 0, 0, 255}); // x1
    Image b(2, 1);
    b.set(0, 0, {20, 0, 0, 255});
    b.set(1, 0, {40, 0, 0, 255}); // x1

    const std::array<const Image*, 2> images{&a, &b};
    const Palette p = paletteFromImages(images);
    REQUIRE(p.colors.size() == 4);
    CHECK(p.colors[0] == Rgba8{20, 0, 0, 255});
    CHECK(p.colors[1] == Rgba8{10, 0, 0, 255}); // ties keep the first-seen order
    CHECK(p.colors[2] == Rgba8{30, 0, 0, 255});
    CHECK(p.colors[3] == Rgba8{40, 0, 0, 255});
    CHECK(p == paletteFromImages(images)); // deterministic
}

TEST_CASE("paletteFromImages ignores transparent pixels and forces alpha 255") {
    Image img(4, 1); // starts fully transparent
    img.set(0, 0, {5, 6, 7, 0});    // ignored, even with color data
    img.set(1, 0, {1, 2, 3, 1});    // faint but visible
    img.set(2, 0, {1, 2, 3, 128});  // same RGB: one entry
    img.set(3, 0, {1, 2, 3, 255});
    const std::array<const Image*, 1> images{&img};
    const Palette p = paletteFromImages(images);
    REQUIRE(p.colors.size() == 1);
    CHECK(p.colors[0] == Rgba8{1, 2, 3, 255});

    const Image blank(3, 3);
    const std::array<const Image*, 1> none{&blank};
    CHECK(paletteFromImages(none).colors.empty());
}

TEST_CASE("paletteFromImages honours maxColors and skips null or empty images") {
    Image img(300, 1);
    for (int x = 0; x < 300; ++x)
        img.set(x, 0, {static_cast<std::uint8_t>(x % 256), static_cast<std::uint8_t>(x / 256), 0, 255});
    const Image empty;
    const std::array<const Image*, 3> images{nullptr, &empty, &img};

    CHECK(paletteFromImages(images).colors.size() == kMaxPaletteColors);
    CHECK(paletteFromImages(images, 5).colors.size() == 5);
    CHECK(paletteFromImages(images, 0).colors.size() == 1);              // clamped up to 1
    CHECK(paletteFromImages(images, 100000).colors.size() == kMaxPaletteColors); // clamped down
    CHECK(paletteFromImages({}).colors.empty());

    // The cut keeps the first-seen colors among the equal ones.
    const Palette five = paletteFromImages(images, 5);
    CHECK(five.colors[0] == Rgba8{0, 0, 0, 255});
    CHECK(five.colors[4] == Rgba8{4, 0, 0, 255});
}

TEST_CASE("Luminance sort goes dark to light and is stable") {
    Palette p = colorsOnly({{255, 255, 255, 255}, {0, 0, 255, 255}, {0, 0, 0, 255}, {0, 255, 0, 255}, {128, 128, 128, 255}});
    sortPalette(p, PaletteSort::Luminance);
    CHECK(p.colors[0] == Rgba8{0, 0, 0, 255});
    CHECK(p.colors[1] == Rgba8{0, 0, 255, 255});
    CHECK(p.colors[2] == Rgba8{128, 128, 128, 255});
    CHECK(p.colors[3] == Rgba8{0, 255, 0, 255});
    CHECK(p.colors[4] == Rgba8{255, 255, 255, 255});

    // Equal luma keeps the original order: sorting twice changes nothing, and so does the reverse input of equal colors.
    Palette again = p;
    sortPalette(again, PaletteSort::Luminance);
    CHECK(again == p);

    Palette same = colorsOnly({{9, 9, 9, 255}, {9, 9, 9, 255}});
    sortPalette(same, PaletteSort::Luminance);
    CHECK(same.colors.size() == 2);
}

TEST_CASE("Hue sort puts grays first, then goes around the wheel") {
    Palette p = colorsOnly({{0, 0, 255, 255},    // 240
                            {200, 200, 200, 255},
                            {0, 255, 0, 255},    // 120
                            {255, 0, 0, 255},    // 0
                            {10, 10, 10, 255},
                            {255, 0, 128, 255}}); // ~330
    sortPalette(p, PaletteSort::Hue);
    REQUIRE(p.colors.size() == 6);
    CHECK(p.colors[0] == Rgba8{10, 10, 10, 255}); // grays by luminance
    CHECK(p.colors[1] == Rgba8{200, 200, 200, 255});
    CHECK(p.colors[2] == Rgba8{255, 0, 0, 255});
    CHECK(p.colors[3] == Rgba8{0, 255, 0, 255});
    CHECK(p.colors[4] == Rgba8{0, 0, 255, 255});
    CHECK(p.colors[5] == Rgba8{255, 0, 128, 255});
}

TEST_CASE("Hue sort is stable for equal hues and safe on small palettes") {
    // Same hue, different brightness: the input order stays.
    Palette p = colorsOnly({{200, 0, 0, 255}, {100, 0, 0, 255}, {255, 0, 0, 255}});
    const Palette before = p;
    sortPalette(p, PaletteSort::Hue);
    CHECK(p == before);

    Palette empty;
    sortPalette(empty, PaletteSort::Hue);
    sortPalette(empty, PaletteSort::Luminance);
    CHECK(empty.colors.empty());
}

TEST_CASE("addPaletteColor adds once and refuses when full") {
    Palette p;
    CHECK(addPaletteColor(p, {1, 2, 3, 255}));
    CHECK_FALSE(addPaletteColor(p, {1, 2, 3, 255}));
    CHECK_FALSE(addPaletteColor(p, {1, 2, 3, 40})); // alpha is forced, so it is the same color
    CHECK(p.colors.size() == 1);

    CHECK(addPaletteColor(p, {4, 5, 6, 0}));
    CHECK(p.colors.back() == Rgba8{4, 5, 6, 255});

    while (p.colors.size() < kMaxPaletteColors)
        CHECK(addPaletteColor(p, {static_cast<std::uint8_t>(p.colors.size()), 100, 100, 255}));
    CHECK_FALSE(addPaletteColor(p, {200, 200, 200, 255}));
    CHECK(p.colors.size() == kMaxPaletteColors);
}
