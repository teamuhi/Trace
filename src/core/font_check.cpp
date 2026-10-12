#include "core/font_check.hpp"

#include <cstddef>
#include <cstring>

namespace trace {

namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint16_t be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

} // namespace

bool looksLikeFont(const std::vector<std::uint8_t>& data) {
    if (data.size() < 12)
        return false;
    const std::uint8_t* p = data.data();
    const std::uint32_t version = be32(p);
    // 0x00010000 and 'true' are TrueType, 'OTTO' is CFF-based OpenType. Collections ('ttcf') are not supported.
    if (version != 0x00010000u && version != 0x74727565u && version != 0x4F54544Fu)
        return false;
    const std::size_t tables = be16(p + 4);
    if (tables == 0 || tables > 256 || data.size() < 12 + tables * 16)
        return false;

    bool cmap = false;
    bool outlines = false;
    for (std::size_t i = 0; i < tables; ++i) {
        const std::uint8_t* rec = p + 12 + i * 16;
        const std::uint32_t offset = be32(rec + 8);
        const std::uint32_t length = be32(rec + 12);
        if (static_cast<std::uint64_t>(offset) + length > data.size())
            return false; // a table that runs past the end of the file
        if (std::memcmp(rec, "cmap", 4) == 0)
            cmap = true;
        if (std::memcmp(rec, "glyf", 4) == 0 || std::memcmp(rec, "CFF ", 4) == 0 || std::memcmp(rec, "CFF2", 4) == 0)
            outlines = true;
    }
    return cmap && outlines;
}

} // namespace trace
