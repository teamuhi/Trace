#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/color.hpp"
#include "core/image.hpp"

namespace trace {

// A named list of opaque colors (alpha is always 255) for the Pixel Art Palette window and the `.tpix` file.
// Pure data: no ImGui, no files (the app reads and writes the text through NativeDialogs and file_write).

inline constexpr std::size_t kMaxPaletteColors = 256;
inline constexpr std::size_t kMaxPaletteFileBytes = 1u << 20; // 1 MB: longer text is cut here, never read whole
inline constexpr std::size_t kMaxPaletteNameBytes = 64;

struct Palette {
    std::string name;
    std::vector<Rgba8> colors; // at most kMaxPaletteColors, every alpha 255

    bool operator==(const Palette&) const = default;
};

// The 27 opaque colors that used to be hard-coded in the Pixel tools panel (it keeps them as the default).
Palette defaultPalette();

// GIMP `.gpl`: "GIMP Palette", then `Name:` and `Columns:` lines, `#` comments, and `R G B [name]` rows.
// Tolerates a UTF-8 BOM, CRLF and blank lines; rows that do not parse (missing or non-numeric channel, a value
// outside 0..255) are skipped; text beyond kMaxPaletteFileBytes and colors beyond kMaxPaletteColors are
// dropped; the name is cleaned (control characters removed, trimmed, cut to kMaxPaletteNameBytes). A missing
// "GIMP Palette" header is tolerated (rows are still read). Never throws on any input.
Palette parseGpl(std::string_view text);
// Always ends with a newline; uses LF. Columns is written as 8 (or fewer when the palette is smaller).
std::string formatGpl(const Palette& palette);

// `.hex`: one `RRGGBB` (optional `#`, 3-digit shorthand not accepted) per line. The same tolerance and caps.
// The name is left empty (the caller names it after the file).
Palette parseHex(std::string_view text);
std::string formatHex(const Palette& palette); // "RRGGBB\n" per color, upper case, no '#'

// Cleans a palette name the way parseGpl does.
std::string cleanPaletteName(std::string_view name);

// The distinct opaque-or-translucent-visible colors of the images, most frequent first (ties: first seen
// first). Pixels with alpha 0 are ignored; a translucent pixel counts with its RGB and alpha forced to 255.
// At most `maxColors` (clamped to 1..kMaxPaletteColors) are returned. Deterministic.
Palette paletteFromImages(std::span<const Image* const> images, std::size_t maxColors = kMaxPaletteColors);

enum class PaletteSort { Hue, Luminance };
// Stable sort in place. Hue: by hue angle, grays (saturation 0) first by luminance. Luminance: Rec. 709
// luma, dark to light, ties keep their order.
void sortPalette(Palette& palette, PaletteSort by);

// Appends `color` (alpha forced to 255) unless the palette is full or already has it. Returns whether it was
// added.
bool addPaletteColor(Palette& palette, Rgba8 color);

} // namespace trace
