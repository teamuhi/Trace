#include "core/text_raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// stb_truetype is compiled here, once. STBTT_STATIC keeps its functions out of the link, so they cannot clash with
// the copy Dear ImGui carries.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

namespace trace {

namespace {

constexpr int kMinPixelSize = 1;
constexpr int kMaxPixelSize = 1000;
constexpr int kMaxLineWidth = 16384;               // a line wider than this is cut
constexpr std::size_t kMaxBitmapPixels = 32u << 20; // a taller text than this is cut after a whole line
constexpr int kTabSpaces = 4;
constexpr char32_t kReplacement = 0xFFFD;

// One code point from `text` starting at `i`; advances `i`. Malformed input (stray continuation byte, truncated or
// overlong sequence, surrogate, beyond U+10FFFF) is one replacement character per bad byte.
char32_t nextCodePoint(std::string_view text, std::size_t& i) noexcept {
    const auto byteAt = [&](std::size_t k) { return static_cast<unsigned char>(text[k]); };
    const unsigned char lead = byteAt(i);
    if (lead < 0x80) {
        ++i;
        return lead;
    }
    int extra = 0;
    char32_t cp = 0;
    char32_t minimum = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        extra = 1;
        cp = lead & 0x1Fu;
        minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        extra = 2;
        cp = lead & 0x0Fu;
        minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        extra = 3;
        cp = lead & 0x07u;
        minimum = 0x10000;
    } else {
        ++i;
        return kReplacement;
    }
    if (i + static_cast<std::size_t>(extra) >= text.size()) { // truncated: the sequence runs past the end
        ++i;
        return kReplacement;
    }
    for (int k = 1; k <= extra; ++k) {
        const unsigned char c = byteAt(i + static_cast<std::size_t>(k));
        if ((c & 0xC0u) != 0x80u) {
            ++i;
            return kReplacement;
        }
        cp = (cp << 6) | (c & 0x3Fu);
    }
    if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++i;
        return kReplacement;
    }
    i += static_cast<std::size_t>(extra) + 1;
    return cp;
}

struct Glyph {
    int index = 0;
    bool tab = false;
};

// One line of text as glyph indices, plus its width in pixels.
struct Line {
    std::vector<Glyph> glyphs;
    float width = 0.0f;
};

} // namespace

std::string sanitizeText(std::string_view text, std::size_t maxBytes) {
    std::string out;
    out.reserve(std::min(text.size(), maxBytes));
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '\r' || c == 0x7f || (c < 0x20 && c != '\n' && c != '\t'))
            continue;
        out.push_back(ch);
    }
    if (out.size() > maxBytes) {
        std::size_t cut = maxBytes;
        // Back up over continuation bytes so a multi-byte character is dropped whole.
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0u) == 0x80u)
            --cut;
        out.resize(cut);
    }
    return out;
}

bool rasterizeText(std::span<const std::uint8_t> font, std::string_view utf8, int pixelSize, bool antialias,
                   TextBitmap& out, TextAlign align) {
    out = TextBitmap{};
    if (font.size() < 12 || utf8.empty())
        return false;

    stbtt_fontinfo info;
    std::memset(&info, 0, sizeof(info));
    const int offset = stbtt_GetFontOffsetForIndex(font.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&info, font.data(), offset))
        return false;

    const float size = static_cast<float>(std::clamp(pixelSize, kMinPixelSize, kMaxPixelSize));
    const float scale = stbtt_ScaleForPixelHeight(&info, size);
    if (!(scale > 0.0f))
        return false;

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    const int baseline = static_cast<int>(std::lround(static_cast<float>(ascent) * scale));
    const int lineAdvance =
        std::max(1, static_cast<int>(std::lround(static_cast<float>(ascent - descent + lineGap) * scale)));

    const auto glyphFor = [&](char32_t cp) {
        int g = stbtt_FindGlyphIndex(&info, static_cast<int>(cp));
        if (g == 0)
            g = stbtt_FindGlyphIndex(&info, static_cast<int>(kReplacement));
        if (g == 0)
            g = stbtt_FindGlyphIndex(&info, '?');
        return g; // 0 (the font's own "missing" box) when even '?' is absent
    };
    const auto advanceOf = [&](int glyph) {
        int adv = 0;
        int bearing = 0;
        stbtt_GetGlyphHMetrics(&info, glyph, &adv, &bearing);
        const float a = static_cast<float>(adv) * scale;
        return antialias ? a : std::round(a);
    };
    const int spaceGlyph = stbtt_FindGlyphIndex(&info, ' ');
    const float tabAdvance = advanceOf(spaceGlyph) * static_cast<float>(kTabSpaces);

    // Lay the text out: glyph indices per line and each line's width.
    std::vector<Line> lines(1);
    for (std::size_t i = 0; i < utf8.size();) {
        const char32_t cp = nextCodePoint(utf8, i);
        if (cp == U'\n') {
            lines.emplace_back();
            continue;
        }
        if (cp == U'\r')
            continue;
        Line& line = lines.back();
        if (line.width > static_cast<float>(kMaxLineWidth))
            continue; // too wide: the rest of the line is dropped
        Glyph g;
        if (cp == U'\t') {
            g.tab = true;
            line.width += tabAdvance;
        } else {
            g.index = glyphFor(cp);
            if (!line.glyphs.empty() && !line.glyphs.back().tab) {
                const float kern = static_cast<float>(stbtt_GetGlyphKernAdvance(&info, line.glyphs.back().index, g.index)) * scale;
                line.width += antialias ? kern : std::round(kern);
            }
            line.width += advanceOf(g.index);
        }
        line.glyphs.push_back(g);
    }

    float widest = 0.0f;
    for (const Line& line : lines)
        widest = std::max(widest, line.width);
    const int width = std::min(kMaxLineWidth, static_cast<int>(std::ceil(widest)) + 1);
    if (width < 2)
        return false; // nothing but newlines and nothing wide enough to draw

    // Keep the bitmap bounded: a text taller than the budget loses its last lines.
    const std::size_t maxLines = std::max<std::size_t>(
        1, kMaxBitmapPixels / (static_cast<std::size_t>(width) * static_cast<std::size_t>(lineAdvance)));
    if (lines.size() > maxLines)
        lines.resize(maxLines);
    const int height = static_cast<int>(lines.size()) * lineAdvance;

    TextBitmap bitmap;
    bitmap.width = width;
    bitmap.height = height;
    bitmap.coverage.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);

    bool drewAnything = false;
    std::vector<std::uint8_t> glyphBuffer;
    for (std::size_t row = 0; row < lines.size(); ++row) {
        const Line& line = lines[row];
        const int baselineY = static_cast<int>(row) * lineAdvance + baseline;
        // A shorter line starts further right (all the way for Right, half way for Center); without antialiasing the
        // start stays on a whole pixel like every glyph.
        const float slack = widest - line.width;
        float pen = align == TextAlign::Right ? slack : align == TextAlign::Center ? slack * 0.5f : 0.0f;
        if (!antialias)
            pen = std::floor(pen);
        const Glyph* previous = nullptr;
        for (const Glyph& g : line.glyphs) {
            if (g.tab) {
                pen += tabAdvance;
                previous = &g;
                continue;
            }
            if (previous && !previous->tab) {
                const float kern = static_cast<float>(stbtt_GetGlyphKernAdvance(&info, previous->index, g.index)) * scale;
                pen += antialias ? kern : std::round(kern);
            }
            previous = &g;

            const float whole = std::floor(pen);
            const float fraction = antialias ? pen - whole : 0.0f;
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            stbtt_GetGlyphBitmapBoxSubpixel(&info, g.index, scale, scale, fraction, 0.0f, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0;
            const int gh = y1 - y0;
            pen += advanceOf(g.index);
            if (gw <= 0 || gh <= 0 || gw > kMaxLineWidth || gh > kMaxPixelSize * 4)
                continue;

            glyphBuffer.assign(static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh), 0);
            stbtt_MakeGlyphBitmapSubpixel(&info, glyphBuffer.data(), gw, gh, gw, scale, scale, fraction, 0.0f, g.index);

            const int left = static_cast<int>(whole) + x0;
            const int top = baselineY + y0;
            for (int gy = 0; gy < gh; ++gy) {
                const int py = top + gy;
                if (py < 0 || py >= height)
                    continue;
                for (int gx = 0; gx < gw; ++gx) {
                    const int px = left + gx;
                    if (px < 0 || px >= width)
                        continue;
                    std::uint8_t v = glyphBuffer[static_cast<std::size_t>(gy) * static_cast<std::size_t>(gw) + static_cast<std::size_t>(gx)];
                    if (!antialias)
                        v = v >= 128 ? std::uint8_t{255} : std::uint8_t{0};
                    if (v == 0)
                        continue;
                    std::uint8_t& dst = bitmap.coverage[static_cast<std::size_t>(py) * static_cast<std::size_t>(width) + static_cast<std::size_t>(px)];
                    dst = std::max(dst, v);
                    drewAnything = true;
                }
            }
        }
    }
    if (!drewAnything)
        return false;
    out = std::move(bitmap);
    return true;
}

IRect compositeText(Image& img, const TextBitmap& bitmap, int x, int y, Rgba8 color) {
    if (bitmap.empty() || img.empty() || color.a == 0)
        return IRect{};
    // Work in 64 bits: x and y come from the pointer and may be anywhere.
    const std::int64_t left = x;
    const std::int64_t top = y;
    const IRect clip = intersectionOf(
        IRect{0, 0, img.width(), img.height()},
        IRect{static_cast<int>(std::clamp<std::int64_t>(left, -(1 << 29), 1 << 29)),
              static_cast<int>(std::clamp<std::int64_t>(top, -(1 << 29), 1 << 29)),
              static_cast<int>(std::clamp<std::int64_t>(left + bitmap.width, -(1 << 29), 1 << 29)),
              static_cast<int>(std::clamp<std::int64_t>(top + bitmap.height, -(1 << 29), 1 << 29))});
    if (clip.empty())
        return IRect{};

    IRect touched;
    for (int py = clip.y0; py < clip.y1; ++py) {
        const std::span<Rgba8> row = img.row(py);
        for (int px = clip.x0; px < clip.x1; ++px) {
            const std::uint8_t cov = bitmap.at(static_cast<int>(px - left), static_cast<int>(py - top));
            if (cov == 0)
                continue;
            Rgba8& dst = row[static_cast<std::size_t>(px)];
            const Rgba8 next = blendOver(dst, color, cov);
            if (next == dst)
                continue;
            dst = next;
            touched = unionOf(touched, IRect{px, py, px + 1, py + 1});
        }
    }
    return touched;
}

} // namespace trace
