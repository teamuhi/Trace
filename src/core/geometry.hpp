#pragma once

#include <algorithm>

namespace trace {

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

// Half-open pixel rectangle [x0, x1) x [y0, y1). Empty when it has no area.
struct IRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    constexpr int width() const noexcept { return x1 > x0 ? x1 - x0 : 0; }
    constexpr int height() const noexcept { return y1 > y0 ? y1 - y0 : 0; }
    constexpr bool empty() const noexcept { return x1 <= x0 || y1 <= y0; }

    constexpr bool operator==(const IRect&) const = default;
};

constexpr IRect unionOf(const IRect& a, const IRect& b) noexcept {
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    return IRect{std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}

constexpr IRect intersectionOf(const IRect& a, const IRect& b) noexcept {
    const IRect r{std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
    return r.empty() ? IRect{} : r;
}

} // namespace trace
