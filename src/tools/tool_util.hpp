#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/color.hpp"
#include "core/geometry.hpp"
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
