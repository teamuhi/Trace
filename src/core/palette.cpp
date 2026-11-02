#include "core/palette.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace trace {

namespace {

// The opaque swatches the Pixel tools panel used to hard-code.
constexpr Rgba8 kDefaultColors[] = {
    {0, 0, 0, 255},       {64, 64, 64, 255},    {127, 127, 127, 255}, {195, 195, 195, 255}, {255, 255, 255, 255},
    {120, 0, 0, 255},     {136, 0, 21, 255},    {237, 28, 36, 255},   {255, 127, 39, 255},  {255, 201, 14, 255},
    {255, 242, 0, 255},   {181, 230, 29, 255},  {34, 177, 76, 255},   {0, 128, 128, 255},   {0, 162, 232, 255},
    {153, 217, 234, 255}, {63, 72, 204, 255},   {0, 0, 128, 255},     {163, 73, 164, 255},  {255, 0, 255, 255},
    {255, 174, 201, 255}, {185, 122, 87, 255},  {239, 228, 176, 255}, {128, 128, 0, 255},   {112, 146, 190, 255},
    {200, 191, 231, 255}, {135, 206, 235, 255},
};

constexpr std::size_t kGplColumns = 8;

bool isBlank(char c) noexcept {
    return c == ' ' || c == '\t';
}

bool isSpace(char c) noexcept {
    return isBlank(c) || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

// Drops a UTF-8 BOM, cuts the text at kMaxPaletteFileBytes (without keeping a half-read last line) and calls
// `onLine` with every line, CR and LF removed.
template <class OnLine>
void forEachLine(std::string_view text, OnLine&& onLine) {
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);
    if (text.size() > kMaxPaletteFileBytes) {
        const bool atBoundary = text[kMaxPaletteFileBytes] == '\n' || text[kMaxPaletteFileBytes - 1] == '\n';
        text = text.substr(0, kMaxPaletteFileBytes);
        if (!atBoundary) {
            const std::size_t lastBreak = text.rfind('\n');
            text = lastBreak == std::string_view::npos ? std::string_view{} : text.substr(0, lastBreak);
        }
    }

    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        onLine(line);
    }
}

// One decimal channel (1..3 digits, value 0..255) after optional blanks, followed by a blank or the end.
bool takeChannel(std::string_view& s, std::uint8_t& out) noexcept {
    while (!s.empty() && isBlank(s.front()))
        s.remove_prefix(1);
    unsigned value = 0;
    std::size_t digits = 0;
    while (digits < s.size() && s[digits] >= '0' && s[digits] <= '9') {
        if (digits == 3)
            return false;
        value = value * 10u + static_cast<unsigned>(s[digits] - '0');
        ++digits;
    }
    if (digits == 0 || value > 255u || (digits < s.size() && !isBlank(s[digits])))
        return false;
    out = static_cast<std::uint8_t>(value);
    s.remove_prefix(digits);
    return true;
}

int hexDigit(char c) noexcept {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool startsWithKey(std::string_view line, std::string_view key) noexcept {
    return line.size() >= key.size() && std::equal(key.begin(), key.end(), line.begin(), [](char a, char b) {
               return a == b || (b >= 'A' && b <= 'Z' && a == static_cast<char>(b + ('a' - 'A')));
           });
}

// Rec. 709 luma scaled by 10000, so ties are exact.
unsigned luma(Rgba8 c) noexcept {
    return 2126u * c.r + 7152u * c.g + 722u * c.b;
}

// Hue angle in degrees [0, 360); only meaningful when the color is not gray.
int hueDegrees(Rgba8 c) noexcept {
    const int mx = std::max({c.r, c.g, c.b});
    const int mn = std::min({c.r, c.g, c.b});
    const int d = mx - mn;
    if (d == 0)
        return 0;
    // The position around the wheel in sixths, scaled by d (0..6d); integer math keeps the order exact.
    int pos;
    if (mx == c.r)
        pos = (c.g - c.b) + (c.g < c.b ? 6 * d : 0);
    else if (mx == c.g)
        pos = (c.b - c.r) + 2 * d;
    else
        pos = (c.r - c.g) + 4 * d;
    return pos * 60 / d;
}

} // namespace

Palette defaultPalette() {
    Palette p;
    p.name = "Default";
    p.colors.assign(std::begin(kDefaultColors), std::end(kDefaultColors));
    return p;
}

std::string cleanPaletteName(std::string_view name) {
    name = trim(name);
    std::string out;
    out.reserve(std::min(name.size(), kMaxPaletteNameBytes + 8));
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u != 0x7F)
            out.push_back(c);
        if (out.size() > kMaxPaletteNameBytes + 8)
            break; // enough to cut from; a hostile name does not get copied whole
    }

    std::string_view view = trim(out);
    if (view.size() > kMaxPaletteNameBytes) {
        std::size_t cut = kMaxPaletteNameBytes;
        while (cut > 0 && (static_cast<unsigned char>(view[cut]) & 0xC0) == 0x80)
            --cut; // never split a UTF-8 sequence
        view = trim(view.substr(0, cut));
    }
    return std::string(view);
}

Palette parseGpl(std::string_view text) {
    Palette p;
    forEachLine(text, [&p](std::string_view raw) {
        const std::string_view line = trim(raw);
        if (line.empty() || line.front() == '#')
            return;
        if (startsWithKey(line, "GIMP Palette") || startsWithKey(line, "Columns:"))
            return;
        if (startsWithKey(line, "Name:")) {
            p.name = cleanPaletteName(line.substr(5));
            return;
        }
        if (p.colors.size() >= kMaxPaletteColors)
            return;

        std::string_view rest = line;
        std::uint8_t r = 0, g = 0, b = 0;
        if (takeChannel(rest, r) && takeChannel(rest, g) && takeChannel(rest, b))
            p.colors.push_back(Rgba8{r, g, b, 255}); // whatever follows is the color's name
    });
    return p;
}

std::string formatGpl(const Palette& palette) {
    const std::size_t count = std::min(palette.colors.size(), kMaxPaletteColors);
    std::string out = "GIMP Palette\nName: " + cleanPaletteName(palette.name) + "\nColumns: " +
                      std::to_string(std::min(count, kGplColumns)) + "\n#\n";
    out.reserve(out.size() + count * 20);

    constexpr char kHex[] = "0123456789ABCDEF";
    const auto pad = [&out](unsigned v) { // right-aligned in 3 columns, as GIMP writes it
        const std::string s = std::to_string(v);
        out.append(3 - s.size(), ' ');
        out += s;
    };
    for (std::size_t i = 0; i < count; ++i) {
        const Rgba8 c = palette.colors[i];
        pad(c.r);
        out += ' ';
        pad(c.g);
        out += ' ';
        pad(c.b);
        out += '\t';
        for (const std::uint8_t v : {c.r, c.g, c.b}) {
            out += kHex[v >> 4];
            out += kHex[v & 15];
        }
        out += '\n';
    }
    return out;
}

Palette parseHex(std::string_view text) {
    Palette p;
    forEachLine(text, [&p](std::string_view raw) {
        if (p.colors.size() >= kMaxPaletteColors)
            return;
        std::string_view line = trim(raw);
        if (!line.empty() && line.front() == '#')
            line.remove_prefix(1);
        if (line.size() != 6)
            return;

        int v[6];
        for (std::size_t i = 0; i < 6; ++i) {
            v[i] = hexDigit(line[i]);
            if (v[i] < 0)
                return;
        }
        p.colors.push_back(Rgba8{static_cast<std::uint8_t>(v[0] * 16 + v[1]), static_cast<std::uint8_t>(v[2] * 16 + v[3]),
                                 static_cast<std::uint8_t>(v[4] * 16 + v[5]), 255});
    });
    return p;
}

std::string formatHex(const Palette& palette) {
    const std::size_t count = std::min(palette.colors.size(), kMaxPaletteColors);
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(count * 7);
    for (std::size_t i = 0; i < count; ++i) {
        const Rgba8 c = palette.colors[i];
        for (const std::uint8_t v : {c.r, c.g, c.b}) {
            out += kHex[v >> 4];
            out += kHex[v & 15];
        }
        out += '\n';
    }
    return out;
}

Palette paletteFromImages(std::span<const Image* const> images, std::size_t maxColors) {
    maxColors = std::clamp<std::size_t>(maxColors, 1, kMaxPaletteColors);

    struct Entry {
        std::uint32_t rgb;
        std::uint64_t count;
    };
    std::vector<Entry> entries; // in first-seen order
    std::unordered_map<std::uint32_t, std::size_t> slot;

    for (const Image* image : images) {
        if (!image)
            continue;
        for (const Rgba8 px : image->pixels()) {
            if (px.a == 0)
                continue;
            const std::uint32_t rgb = (std::uint32_t{px.r} << 16) | (std::uint32_t{px.g} << 8) | px.b;
            const auto [it, inserted] = slot.try_emplace(rgb, entries.size());
            if (inserted)
                entries.push_back({rgb, 1});
            else
                ++entries[it->second].count;
        }
    }

    // Stable, so equal counts stay in first-seen order.
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.count > b.count; });

    Palette p;
    const std::size_t take = std::min(entries.size(), maxColors);
    p.colors.reserve(take);
    for (std::size_t i = 0; i < take; ++i) {
        const std::uint32_t rgb = entries[i].rgb;
        p.colors.push_back(Rgba8{static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                                 static_cast<std::uint8_t>(rgb), 255});
    }
    return p;
}

void sortPalette(Palette& palette, PaletteSort by) {
    auto& colors = palette.colors;
    if (by == PaletteSort::Luminance) {
        std::stable_sort(colors.begin(), colors.end(), [](Rgba8 a, Rgba8 b) { return luma(a) < luma(b); });
        return;
    }

    const auto gray = [](Rgba8 c) { return c.r == c.g && c.g == c.b; };
    std::stable_sort(colors.begin(), colors.end(), [&gray](Rgba8 a, Rgba8 b) {
        const bool ga = gray(a);
        const bool gb = gray(b);
        if (ga != gb)
            return ga; // grays come first
        if (ga)
            return luma(a) < luma(b);
        return hueDegrees(a) < hueDegrees(b);
    });
}

bool addPaletteColor(Palette& palette, Rgba8 color) {
    color.a = 255;
    if (palette.colors.size() >= kMaxPaletteColors ||
        std::find(palette.colors.begin(), palette.colors.end(), color) != palette.colors.end())
        return false;
    palette.colors.push_back(color);
    return true;
}

} // namespace trace
