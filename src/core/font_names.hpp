#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace trace {

// The names a font file gives itself: the family ("Calibri") and the style inside it ("Bold Italic").
struct FontNames {
    std::string family;
    std::string style;
};

// Where the `name` table of a font file lies, in the file.
struct FontTableRange {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

// Finds the `name` table from the start of a font file (a few kilobytes are enough: the sfnt header and its table
// directory). A collection (.ttc) answers for its first font. Returns nothing for anything that is not an sfnt font or
// when the directory is cut off.
std::optional<FontTableRange> findFontNameTable(std::span<const std::uint8_t> fileStart);

// Reads the family and style out of the bytes of a `name` table, as UTF-8. The typographic names (ids 16 and 17) win
// over the legacy ones (1 and 2), so "Calibri Light" files under "Calibri" with the style "Light"; English names win
// over other languages. A missing style reads "Regular". Returns false when no family can be read. Any damage in the
// table (offsets out of range, odd lengths) is tolerated, never read past the end.
bool parseFontNameTable(std::span<const std::uint8_t> table, FontNames& out);

} // namespace trace
