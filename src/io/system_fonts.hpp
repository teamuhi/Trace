#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace trace {

// One installed font file, as the font picker lists it. Only the first face of a collection (.ttc) is offered.
struct SystemFont {
    std::string family; // "Calibri"
    std::string style;  // "Regular", "Bold Italic", "Light"
    std::string path;   // UTF-8
};

inline constexpr std::size_t kMaxFontFiles = 20000;

// The folders fonts are installed in on this system (scanning skips the ones that do not exist).
std::vector<std::filesystem::path> systemFontDirectories();

// Reads the names of every .ttf, .otf and .ttc under `directories` (looking inside the folders too), without
// loading the fonts: only each file's table directory and name table are read. Unreadable or broken files are skipped,
// as are duplicates of the same family and style (the first one found wins). The result is sorted by family (ignoring
// case), then Regular first, then by style. Bounded: at most kMaxFontFiles files are looked at.
std::vector<SystemFont> scanFontFiles(std::span<const std::filesystem::path> directories);

// The bytes of a font file for drawing, at most `maxBytes`. Returns false (and an empty `data`) when the file cannot
// be read, is too big or does not look like a font.
bool readFontData(const std::string& pathUtf8, std::vector<std::uint8_t>& data,
                  std::uintmax_t maxBytes = 48u * 1024u * 1024u);

} // namespace trace
