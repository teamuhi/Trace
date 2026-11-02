#include "core/font_names.hpp"

#include <algorithm>

namespace trace {

namespace {

constexpr std::uint16_t kNameFamily = 1;
constexpr std::uint16_t kNameStyle = 2;
constexpr std::uint16_t kNameTypographicFamily = 16;
constexpr std::uint16_t kNameTypographicStyle = 17;
constexpr std::size_t kMaxNameBytes = 256; // a family or style longer than this is not a name

std::uint16_t be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// UTF-16 big endian to UTF-8; a lone surrogate or a control character ends the name's usable part.
std::string fromUtf16Be(const std::uint8_t* p, std::size_t bytes) {
    std::string out;
    for (std::size_t i = 0; i + 1 < bytes; i += 2) {
        std::uint32_t cp = be16(p + i);
        if (cp >= 0xD800 && cp < 0xDC00) {
            if (i + 3 >= bytes)
                break;
            const std::uint32_t low = be16(p + i + 2);
            if (low < 0xDC00 || low >= 0xE000)
                break;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            i += 2;
        } else if (cp >= 0xDC00 && cp < 0xE000) {
            break;
        }
        if (cp < 0x20)
            break;
        appendUtf8(out, cp);
    }
    return out;
}

// Mac Roman names are plain ASCII for every font family name in practice; anything else ends the name.
std::string fromMacRoman(const std::uint8_t* p, std::size_t bytes) {
    std::string out;
    for (std::size_t i = 0; i < bytes; ++i) {
        if (p[i] < 0x20 || p[i] >= 0x7F)
            break;
        out.push_back(static_cast<char>(p[i]));
    }
    return out;
}

// The best name with id `nameId`: Windows English, then any Windows language, then Unicode, then Macintosh.
std::string findName(std::span<const std::uint8_t> table, std::uint16_t nameId) {
    if (table.size() < 6)
        return {};
    const std::size_t count = be16(table.data() + 2);
    const std::size_t stringBase = be16(table.data() + 4);
    std::string best;
    int bestRank = 0; // higher is better; 0 = nothing yet
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t rec = 6 + i * 12;
        if (rec + 12 > table.size())
            break;
        const std::uint8_t* r = table.data() + rec;
        if (be16(r + 6) != nameId)
            continue;
        const std::uint16_t platform = be16(r);
        const std::uint16_t language = be16(r + 4);
        const std::size_t length = be16(r + 8);
        const std::size_t offset = stringBase + be16(r + 10);
        if (length == 0 || length > kMaxNameBytes * 2 || offset > table.size() || length > table.size() - offset)
            continue;
        int rank = 0;
        std::string text;
        if (platform == 3) {
            rank = (language == 0x0409) ? 4 : 3;
            text = fromUtf16Be(table.data() + offset, length);
        } else if (platform == 0) {
            rank = 2;
            text = fromUtf16Be(table.data() + offset, length);
        } else if (platform == 1 && language == 0) {
            rank = 1;
            text = fromMacRoman(table.data() + offset, length);
        }
        if (rank > bestRank && !text.empty() && text.size() <= kMaxNameBytes) {
            best = std::move(text);
            bestRank = rank;
        }
    }
    return best;
}

} // namespace

std::optional<FontTableRange> findFontNameTable(std::span<const std::uint8_t> fileStart) {
    if (fileStart.size() < 12)
        return std::nullopt;
    std::size_t base = 0; // where the offset table of the face starts
    if (be32(fileStart.data()) == 0x74746366u) { // "ttcf": a collection; the first face
        if (fileStart.size() < 16 || be32(fileStart.data() + 8) == 0)
            return std::nullopt;
        base = be32(fileStart.data() + 12);
        if (base > fileStart.size() || fileStart.size() - base < 12)
            return std::nullopt;
    }
    const std::uint8_t* head = fileStart.data() + base;
    const std::uint32_t version = be32(head);
    if (version != 0x00010000u && version != 0x74727565u && version != 0x4F54544Fu)
        return std::nullopt;
    const std::size_t tables = be16(head + 4);
    for (std::size_t i = 0; i < tables; ++i) {
        const std::size_t rec = base + 12 + i * 16;
        if (rec + 16 > fileStart.size())
            return std::nullopt;
        const std::uint8_t* r = fileStart.data() + rec;
        if (be32(r) == 0x6E616D65u) // "name"
            return FontTableRange{be32(r + 8), be32(r + 12)};
    }
    return std::nullopt;
}

bool parseFontNameTable(std::span<const std::uint8_t> table, FontNames& out) {
    out = FontNames{};
    std::string family = findName(table, kNameTypographicFamily);
    std::string style;
    if (family.empty()) {
        family = findName(table, kNameFamily);
        style = findName(table, kNameStyle);
    } else {
        style = findName(table, kNameTypographicStyle);
        if (style.empty())
            style = findName(table, kNameStyle);
    }
    if (family.empty())
        return false;
    out.family = std::move(family);
    out.style = style.empty() ? "Regular" : std::move(style);
    return true;
}

} // namespace trace
