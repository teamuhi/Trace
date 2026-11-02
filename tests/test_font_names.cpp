#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/font_names.hpp"
#include "io/system_fonts.hpp"

using namespace trace;

namespace {

void put16(std::vector<std::uint8_t>& v, unsigned x) {
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
}

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    put16(v, x >> 16);
    put16(v, x & 0xFFFF);
}

struct NameRecord {
    unsigned platform;
    unsigned language;
    unsigned id;
    std::string text; // ASCII; written as UTF-16BE for platforms 0 and 3
};

// A `name` table (format 0) holding `records`.
std::vector<std::uint8_t> nameTable(const std::vector<NameRecord>& records) {
    std::vector<std::uint8_t> strings;
    std::vector<std::uint8_t> out;
    put16(out, 0);
    put16(out, static_cast<unsigned>(records.size()));
    put16(out, static_cast<unsigned>(6 + records.size() * 12));
    for (const NameRecord& r : records) {
        const std::size_t start = strings.size();
        for (const char c : r.text) {
            if (r.platform != 1)
                strings.push_back(0);
            strings.push_back(static_cast<std::uint8_t>(c));
        }
        put16(out, r.platform);
        put16(out, r.platform == 1 ? 0 : 1);
        put16(out, r.language);
        put16(out, r.id);
        put16(out, static_cast<unsigned>(strings.size() - start));
        put16(out, static_cast<unsigned>(start));
    }
    out.insert(out.end(), strings.begin(), strings.end());
    return out;
}

// A minimal font file: the sfnt header, a directory of one `name` table and that table. Not drawable, but enough for
// the name reader and the folder scan.
std::vector<std::uint8_t> fontFile(const std::vector<NameRecord>& records) {
    const std::vector<std::uint8_t> table = nameTable(records);
    std::vector<std::uint8_t> out;
    put32(out, 0x00010000u);
    put16(out, 1);
    put16(out, 16);
    put16(out, 0);
    put16(out, 0);
    put32(out, 0x6E616D65u); // "name"
    put32(out, 0);
    put32(out, 28); // right behind the directory
    put32(out, static_cast<std::uint32_t>(table.size()));
    out.insert(out.end(), table.begin(), table.end());
    while (out.size() < 300)
        out.push_back(0);
    return out;
}

} // namespace

TEST_CASE("The family and style come from the name table, English Windows names first") {
    FontNames names;
    const auto table = nameTable({{1, 0, 1, "MacName"}, {3, 0x0407, 1, "Deutsch"}, {3, 0x0409, 1, "Calibri"},
                                  {3, 0x0409, 2, "Bold Italic"}});
    REQUIRE(parseFontNameTable(table, names));
    CHECK(names.family == "Calibri");
    CHECK(names.style == "Bold Italic");
}

TEST_CASE("The typographic names win, so Light files under the family") {
    FontNames names;
    const auto table = nameTable({{3, 0x0409, 1, "Calibri Light"}, {3, 0x0409, 2, "Regular"},
                                  {3, 0x0409, 16, "Calibri"}, {3, 0x0409, 17, "Light"}});
    REQUIRE(parseFontNameTable(table, names));
    CHECK(names.family == "Calibri");
    CHECK(names.style == "Light");
}

TEST_CASE("A missing style reads Regular, a missing family fails") {
    FontNames names;
    REQUIRE(parseFontNameTable(nameTable({{3, 0x0409, 1, "Arial"}}), names));
    CHECK(names.style == "Regular");
    CHECK_FALSE(parseFontNameTable(nameTable({{3, 0x0409, 2, "Bold"}}), names));
    CHECK(names.family.empty());
}

TEST_CASE("Macintosh names are read when nothing else is there") {
    FontNames names;
    REQUIRE(parseFontNameTable(nameTable({{1, 0, 1, "Courier"}, {1, 0, 2, "Bold"}}), names));
    CHECK(names.family == "Courier");
    CHECK(names.style == "Bold");
}

TEST_CASE("A damaged name table never reads past its end") {
    FontNames names;
    CHECK_FALSE(parseFontNameTable({}, names));
    std::vector<std::uint8_t> table = nameTable({{3, 0x0409, 1, "Verdana"}});
    // Cut anywhere: either a name comes out whole or nothing does, and nothing crashes.
    for (std::size_t cut = 0; cut <= table.size(); ++cut) {
        const std::vector<std::uint8_t> part(table.begin(), table.begin() + static_cast<std::ptrdiff_t>(cut));
        if (parseFontNameTable(part, names))
            CHECK(names.family == "Verdana");
    }
    // A record whose string lies far outside the table.
    table[6 + 10] = 0xFF;
    table[6 + 11] = 0xFF;
    CHECK_FALSE(parseFontNameTable(table, names));
    // Absurd record counts.
    std::vector<std::uint8_t> huge = {0, 0, 0xFF, 0xFF, 0, 6};
    CHECK_FALSE(parseFontNameTable(huge, names));
}

TEST_CASE("The name table is found from the start of a font file") {
    const auto file = fontFile({{3, 0x0409, 1, "Georgia"}});
    const auto range = findFontNameTable(file);
    REQUIRE(range.has_value());
    CHECK(range->offset == 28);
    CHECK(range->length > 6);
    CHECK_FALSE(findFontNameTable({}).has_value());
    const std::vector<std::uint8_t> notAFont(64, 7);
    CHECK_FALSE(findFontNameTable(notAFont).has_value());
    // A directory cut short.
    const std::vector<std::uint8_t> cutShort(file.begin(), file.begin() + 20);
    CHECK_FALSE(findFontNameTable(cutShort).has_value());
}

TEST_CASE("Scanning folders lists fonts by family and skips what is not a font") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "trace_font_scan_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "sub");
    const auto write = [](const fs::path& path, const std::vector<std::uint8_t>& bytes) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    write(root / "b.ttf", fontFile({{3, 0x0409, 1, "Beta"}, {3, 0x0409, 2, "Bold"}}));
    write(root / "sub" / "b2.TTF", fontFile({{3, 0x0409, 1, "Beta"}, {3, 0x0409, 2, "Regular"}}));
    write(root / "a.otf", fontFile({{3, 0x0409, 1, "alpha"}}));
    write(root / "dup.ttf", fontFile({{3, 0x0409, 1, "ALPHA"}})); // same family and style as a.otf (case aside)
    write(root / "broken.ttf", std::vector<std::uint8_t>(400, 1));
    write(root / "readme.txt", std::vector<std::uint8_t>(400, 1));

    const std::vector<fs::path> dirs = {root, root / "missing"};
    const std::vector<SystemFont> fonts = scanFontFiles(dirs);
    REQUIRE(fonts.size() == 3);
    CHECK(fonts[0].family.substr(0, 5) == std::string("alpha").substr(0, 5)); // sorted ignoring case
    CHECK(fonts[1].family == "Beta");
    CHECK(fonts[1].style == "Regular"); // Regular before Bold
    CHECK(fonts[2].style == "Bold");

    std::vector<std::uint8_t> data;
    CHECK_FALSE(readFontData((root / "broken.ttf").string(), data));
    CHECK(data.empty());
    CHECK_FALSE(readFontData((root / "nothing.ttf").string(), data));
    fs::remove_all(root, ec);
}
