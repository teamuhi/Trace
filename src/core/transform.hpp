#pragma once

#include <utility>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/image.hpp"

namespace trace {

inline constexpr int kMaxImageSide = 16384; // same limit as image loading and the new-canvas size

enum class ResizeFilter { Nearest, Bilinear };

// Where the old image sits inside a resized canvas: a 3 x 3 grid of positions.
enum class Anchor {
    TopLeft, Top, TopRight,
    Left, Center, Right,
    BottomLeft, Bottom, BottomRight
};

// Scales the whole image to width x height. Nearest keeps hard pixel edges; bilinear blends, with colors
// weighted by alpha so a transparent neighbour never tints an opaque pixel. Returns an empty image when
// the source is empty or either target side is outside 1..kMaxImageSide. May throw std::bad_alloc.
Image resizedImage(const Image& src, int width, int height, ResizeFilter filter);

// Position of the old image's top-left corner inside a new canvas of newWidth x newHeight (negative when
// the canvas shrinks and the image is cut off on that side).
std::pair<int, int> anchorOffset(Anchor anchor, int oldWidth, int oldHeight, int newWidth, int newHeight) noexcept;

// A new canvas of the given size with the old image placed by `anchor`; the uncovered area is `fill` and
// anything that no longer fits is cut off. Empty when the sizes are out of range. May throw std::bad_alloc.
Image canvasResized(const Image& src, int width, int height, Anchor anchor, Rgba8 fill);

// The part of `src` inside `rect` (clipped to the image). Empty when nothing is left. May throw std::bad_alloc.
Image cropped(const Image& src, const IRect& rect);

// A small copy of `src` for previews: no side above `maxSide` (at least 1), the aspect ratio kept, and an image
// that already fits copied as it is. Each preview pixel averages a few evenly spread samples of the block it
// covers (weighted by alpha, so transparent pixels never tint the color), which keeps the cost bounded by the
// preview size however large the source is. Empty for an empty source. May throw std::bad_alloc.
Image thumbnailOf(const Image& src, int maxSide);

// Rounds a proportional size to at least 1 pixel (for "keep aspect ratio" fields).
int scaledSide(int side, double factor) noexcept;

} // namespace trace
