#pragma once

#include <cstdint>

namespace trace {

// Non-premultiplied RGBA, 8 bits per channel.
struct Rgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;

    constexpr bool operator==(const Rgba8&) const = default;
};
static_assert(sizeof(Rgba8) == 4);

namespace detail {

// round(a * b / 255) for a, b in [0, 255].
constexpr unsigned mul255(unsigned a, unsigned b) noexcept {
    const unsigned t = a * b + 128u;
    return (t + (t >> 8)) >> 8;
}

} // namespace detail

// Source-over compositing of `src` onto `dst`. `coverage` (0..255) scales the source alpha, which is how
// antialiased edges are expressed. Exact for the common cases: opaque source and full coverage returns
// the source, zero alpha returns the destination.
constexpr Rgba8 blendOver(Rgba8 dst, Rgba8 src, unsigned coverage = 255) noexcept {
    const unsigned sa = coverage >= 255 ? src.a : detail::mul255(src.a, coverage);
    if (sa == 0)
        return dst;
    if (sa == 255)
        return Rgba8{src.r, src.g, src.b, 255};
    if (dst.a == 0)
        return Rgba8{src.r, src.g, src.b, static_cast<std::uint8_t>(sa)};

    const unsigned dw = detail::mul255(dst.a, 255u - sa); // destination weight after the source covers it
    const unsigned oa = sa + dw;                           // result alpha, never above 255
    const auto channel = [&](std::uint8_t s, std::uint8_t d) {
        return static_cast<std::uint8_t>((s * sa + d * dw + oa / 2u) / oa);
    };
    return Rgba8{channel(src.r, dst.r), channel(src.g, dst.g), channel(src.b, dst.b),
                 static_cast<std::uint8_t>(oa)};
}

// Removes `coverage` (0..255) of the destination's opacity; the color channels are left untouched.
constexpr Rgba8 eraseAlpha(Rgba8 dst, unsigned coverage = 255) noexcept {
    const unsigned keep = coverage >= 255 ? 0u : 255u - coverage;
    dst.a = static_cast<std::uint8_t>(detail::mul255(dst.a, keep));
    return dst;
}

} // namespace trace
