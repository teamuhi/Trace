#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/raster.hpp"
#include "core/symmetry.hpp"
#include "tools/tool.hpp"

namespace trace::tools {

// Pointer coordinates beyond this are clamped; they are far outside any real image and keep every
// float-to-int conversion well defined.
inline constexpr float kCoordLimit = 1.0e6f;

// Pixel containing a fractional coordinate. Non-finite input maps to 0.
inline int toPixel(float v) noexcept {
    if (!std::isfinite(v))
        return 0;
    return static_cast<int>(std::floor(std::clamp(v, -kCoordLimit, kCoordLimit)));
}

inline Vec2f sanitize(Vec2f p) noexcept {
    const auto fix = [](float v) { return std::isfinite(v) ? std::clamp(v, -kCoordLimit, kCoordLimit) : 0.0f; };
    return Vec2f{fix(p.x), fix(p.y)};
}

inline Vec2f pixelCenter(Vec2f p) noexcept {
    return Vec2f{static_cast<float>(toPixel(p.x)) + 0.5f, static_cast<float>(toPixel(p.y)) + 0.5f};
}

// Guide snapping for gestures that define corners (shapes, the selection marquee). `radius` is the event's
// snapRadius in image pixels; 0 turns snapping off. The start of a drag lands on the guide, the far corner
// picks the pixel that puts the shape's edge on it.
inline Vec2f snapStartPoint(const Document& doc, Vec2f p, float radius) noexcept {
    if (!(radius > 0.0f))
        return p;
    return Vec2f{doc.guides().snapStart(GuideAxis::Vertical, p.x, radius),
                 doc.guides().snapStart(GuideAxis::Horizontal, p.y, radius)};
}

inline Vec2f snapEndPoint(const Document& doc, Vec2f p, Vec2f anchor, float radius) noexcept {
    if (!(radius > 0.0f))
        return p;
    return Vec2f{doc.guides().snapEnd(GuideAxis::Vertical, p.x, anchor.x, radius),
                 doc.guides().snapEnd(GuideAxis::Horizontal, p.y, anchor.y, radius)};
}

// The color a gesture paints with: primary normally, secondary for right-button gestures.
inline Rgba8 activeColor(const ToolSettings& s, const PointerEvent& ev) noexcept {
    return ev.secondary ? s.secondary : s.primary;
}

inline Rgba8 otherColor(const ToolSettings& s, const PointerEvent& ev) noexcept {
    return ev.secondary ? s.primary : s.secondary;
}

// The square a `size` square brush covers when stamped on pixel (x, y): anchored size / 2 to the left of and
// above the pixel, like stampSquareLine, so an odd size is centered on it.
inline IRect squareStampRect(int x, int y, int size) noexcept {
    const int left = x - size / 2;
    const int top = y - size / 2;
    return IRect{left, top, left + size, top + size};
}

// Calls visit(x, y) for the pixels of symmetry copy `k` of the pixel line (ax, ay) - (bx, by), start first. An
// exact copy (mirrors at multiples of 45 degrees and 90 degree turns) maps the original's Bresenham pixels one
// by one, so it is exactly the original's picture moved. Any other angle cannot: rounding each mapped pixel
// leaves gaps and double hits, so it draws a Bresenham line between the two ends mapped by the copy's base step
// instead (connected, one pixel thick), then moves that line by the copy's exact part, so copies a quarter turn
// apart are exact images of each other. Consecutive segments of a stroke share their end pixel, which maps to
// the same pixel both times.
template <class Visit>
void forEachCopyLinePixel(const SymmetryPlan& plan, std::size_t k, int ax, int ay, int bx, int by, Visit&& visit) {
    const auto emit = [&](int x, int y) {
        const PixelPos p = plan.extraPixel(k, PixelPos{x, y});
        visit(p.x, p.y);
    };
    if (plan.exact(k)) {
        forEachLinePixel(ax, ay, bx, by, emit);
        return;
    }
    const PixelPos a = plan.basePixel(k, ax, ay);
    const PixelPos b = plan.basePixel(k, bx, by);
    forEachLinePixel(a.x, a.y, b.x, b.y, emit);
}

// An end of copy `k` of a capsule whose end is `v`: the continuous point when antialiased, else the center of
// the copy's pixel (what a hard-edged capsule snaps to anyway, done here through plan.pixel so the snapping
// happens before the quarter turns and the copies stay exact images of each other).
inline Vec2f copyCapsuleEnd(const SymmetryPlan& plan, std::size_t k, Vec2f v, bool antialias) noexcept {
    if (antialias)
        return plan.point(k, v);
    const PixelPos p = plan.pixel(k, toPixel(v.x), toPixel(v.y));
    return Vec2f{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 0.5f};
}

// Pixel box spanned by two corners (both inclusive); a square when `square` is set.
inline IRect cornerBox(Vec2f a, Vec2f b, bool square) noexcept {
    const int sx = toPixel(a.x);
    const int sy = toPixel(a.y);
    int ex = toPixel(b.x);
    int ey = toPixel(b.y);
    if (square) {
        const int dx = ex - sx;
        const int dy = ey - sy;
        const int size = std::max(std::abs(dx), std::abs(dy));
        ex = sx + (dx < 0 ? -size : size);
        ey = sy + (dy < 0 ? -size : size);
    }
    return IRect{std::min(sx, ex), std::min(sy, ey), std::max(sx, ex) + 1, std::max(sy, ey) + 1};
}

} // namespace trace::tools
