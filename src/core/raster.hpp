#pragma once

#include <cstdint>
#include <vector>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/image.hpp"

namespace trace {

enum class BlendMode {
    Over,       // source-over compositing
    EraseAlpha, // remove opacity; the color is ignored
};

struct Paint {
    Rgba8 color;
    BlendMode mode = BlendMode::Over;
};

// Remembers, per pixel, how much coverage the current stroke has already applied. A freehand stroke is
// many overlapping capsules; without this every overlap would composite again, hardening antialiased
// edges and darkening semi-transparent colors. With it the stroke composites as one shape (maximum
// coverage per pixel wins). Must match the image size; call end() when the stroke is finished.
class StrokeMask {
public:
    // Prepares an all-zero mask for a width x height image, discarding any previous stroke.
    void begin(int width, int height);
    // Zeroes the pixels touched since begin(), so the allocation can be reused by the next stroke.
    void end() noexcept;

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    bool matches(const Image& img) const noexcept { return width_ == img.width() && height_ == img.height(); }

    std::uint8_t get(int x, int y) const noexcept { return data_[index(x, y)]; }
    void set(int x, int y, std::uint8_t coverage) noexcept { data_[index(x, y)] = coverage; }
    void markTouched(const IRect& r) noexcept { touched_ = unionOf(touched_, r); }

private:
    std::size_t index(int x, int y) const noexcept {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> data_;
    IRect touched_;
};

// Calls `visit(x, y)` for every pixel of the Bresenham line from (x0, y0) to (x1, y1), both ends included,
// in order from the start. Works in 64 bits so extreme off-canvas coordinates cannot overflow the error term.
template <class Visit>
void forEachLinePixel(int x0, int y0, int x1, int y1, Visit&& visit) {
    const std::int64_t dx = (x1 > x0 ? std::int64_t{x1} - x0 : std::int64_t{x0} - x1);
    const std::int64_t dy = -(y1 > y0 ? std::int64_t{y1} - y0 : std::int64_t{y0} - y1);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    std::int64_t err = dx + dy;
    int x = x0;
    int y = y0;
    for (;;) {
        visit(x, y);
        if (x == x1 && y == y1)
            break;
        const std::int64_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

// Every function clips to the image and returns the pixels it may have changed (empty when none).
// Out-of-range, non-finite or degenerate input is never an error: it simply draws nothing.

// 1-pixel Bresenham line including the end point. The start point is skipped when `includeStart` is false,
// so a polyline of consecutive segments paints each joint once.
IRect drawLine(Image& img, int x0, int y0, int x1, int y1, const Paint& paint, bool includeStart = true);

// Thick line with round caps: every pixel within `radius` of the segment a-b. Antialiased edges use
// distance-based coverage. Without antialiasing the endpoints snap to pixel centers, so a radius of 0.5
// paints exactly one pixel. Pass the stroke's mask to composite overlapping capsules as one shape; a mask
// of the wrong size is ignored.
IRect drawCapsule(Image& img, Vec2f a, Vec2f b, float radius, const Paint& paint, bool antialias,
                  StrokeMask* mask = nullptr);

// A `size` x `size` square stamped at every pixel of the line (x0, y0) - (x1, y1), as a hard-edged brush for
// pixel art: the square covering pixel (x, y) spans x - size / 2 up to x - size / 2 + size - 1, so an odd
// size is centered on the pixel. Pass the stroke's mask so overlapping stamps (and a translucent color) land
// on each pixel once; a mask of the wrong size is ignored. A size below 1 draws nothing.
IRect stampSquareLine(Image& img, int x0, int y0, int x1, int y1, int size, const Paint& paint,
                      StrokeMask* mask = nullptr);

IRect fillRect(Image& img, const IRect& rect, const Paint& paint);

// Rectangle outline of `thickness` pixels drawn inside `rect`. Becomes a filled rectangle when the
// outline would meet itself.
IRect strokeRect(Image& img, const IRect& rect, int thickness, const Paint& paint);

// Ellipse inscribed in `box`. `inset` shrinks both radii, which lets a stroke and its interior fill share
// the exact same boundary.
IRect fillEllipse(Image& img, const IRect& box, const Paint& paint, int inset = 0);
IRect strokeEllipse(Image& img, const IRect& box, int thickness, const Paint& paint);

// Replaces the connected region around (x, y) whose pixels are within `tolerance` (0..255, compared per
// channel against the seed pixel) with `color`. With `contiguous` false every pixel of the image that is
// within the tolerance of the seed is replaced, connected or not.
IRect floodFill(Image& img, int x, int y, Rgba8 color, int tolerance, bool contiguous = true);

} // namespace trace
