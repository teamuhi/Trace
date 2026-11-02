#pragma once

#include <string>
#include <string_view>

#include "core/image.hpp"

namespace trace {

// Copies the declarations of the simple class rules of an SVG's <style> blocks (".a, .b { fill: none; }") onto the
// elements that use those classes, as a style attribute. The rasterizer below reads presentation attributes and
// style attributes but not style sheets, and many icon sets are exported with classes. Anything other than a
// class selector is ignored, and an SVG without a style sheet is returned unchanged.
std::string inlineSvgClassStyles(std::string_view svg);

// Draws an SVG into a square image of `sizePx` pixels: the artwork is scaled to fit, centered, and keeps its aspect
// ratio. The result is a coverage mask: every pixel is white and the alpha channel holds how much of the pixel
// the artwork covers, so a caller can tint it with any color (fills, strokes and their colors in the file are
// ignored). Empty when the SVG cannot be parsed, has no size, or `sizePx` is not positive. Embedded raster
// images, text, gradients and filters are not drawn.
Image rasterizeSvgCoverage(std::string_view svg, int sizePx);

} // namespace trace
