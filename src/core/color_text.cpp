#include "core/color_text.hpp"

#include <array>
#include <cstdio>

namespace trace {

namespace {

std::optional<int> hexDigit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return std::nullopt;
}

} // namespace

std::optional<Rgba8> parseColor(std::string_view text) {
    if (text.empty() || text.front() != '#')
        return std::nullopt;
    text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8)
        return std::nullopt;

    std::array<int, 4> channels = {0, 0, 0, 255};
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const auto hi = hexDigit(text[i]);
        const auto lo = hexDigit(text[i + 1]);
        if (!hi || !lo)
            return std::nullopt;
        channels[i / 2] = *hi * 16 + *lo;
    }
    return Rgba8{static_cast<std::uint8_t>(channels[0]), static_cast<std::uint8_t>(channels[1]),
                 static_cast<std::uint8_t>(channels[2]), static_cast<std::uint8_t>(channels[3])};
}

std::string formatColor(Rgba8 c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
    return buf;
}

} // namespace trace
