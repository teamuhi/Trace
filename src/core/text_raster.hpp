#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/image.hpp"

namespace trace {

// Text typed with the Text tool is limited to this many bytes (UTF-8), which keeps a bitmap and its preview small.
inline constexpr std::size_t kMaxTextBytes = 4096;

// Coverage of rendered text: one byte per pixel, 0 = untouched, 255 = fully covered. The top-left pixel is the
// top-left of the first line's box; a line is as tall as the font size (plus the font's line gap).
struct TextBitmap {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> coverage;

    bool empty() const noexcept { return width <= 0 || height <= 0; }
    std::uint8_t at(int x, int y) const noexcept {
        if (x < 0 || y < 0 || x >= width || y >= height)
            return 0;
        return coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }
};

// Cuts `text` to at most `maxBytes` without splitting a UTF-8 sequence, and drops the bytes that cannot be part of
// text (control characters other than newline and tab, carriage returns). Invalid sequences are left to the
// rasterizer, which shows them as a replacement character.
std::string sanitizeText(std::string_view text, std::size_t maxBytes = kMaxTextBytes);

// Where the lines of a text sit against each other: flush with the widest line's left edge, centered on it, or flush
// with its right edge. A single line is as wide as the text, so it only shows with several lines.
enum class TextAlign { Left, Center, Right };

// Renders `utf8` (lines separated by '\n') with the TrueType font in `font` at `pixelSize` pixels per line, the
// lines aligned as `align` says. Without `antialias` every pixel is fully covered or not at all and glyphs sit on
// whole pixels. Returns false and
// leaves `out` empty when the font cannot be read or the text has nothing to draw (empty, only spaces). A size
// outside 1..1000 is clamped; very long lines and very tall texts are cut so the bitmap stays bounded.
bool rasterizeText(std::span<const std::uint8_t> font, std::string_view utf8, int pixelSize, bool antialias,
                   TextBitmap& out, TextAlign align = TextAlign::Left);

// Paints the bitmap with its top-left pixel at (x, y), source-over with `color`. Clipped to the image; returns
// the pixels that may have changed (empty when none).
IRect compositeText(Image& img, const TextBitmap& bitmap, int x, int y, Rgba8 color);

} // namespace trace
