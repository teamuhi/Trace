#include "core/pattern.hpp"

#include <cmath>

namespace trace {

float patternSpacingFor(float spacing, float width, float height, int maxMarks) noexcept {
    if (!std::isfinite(spacing) || spacing < 1.0f)
        spacing = 1.0f;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f || maxMarks < 1)
        return spacing;
    // Each doubling quarters the cell count, so this ends after a handful of steps for any real window.
    for (int i = 0; i < 32; ++i) {
        const double cells = (static_cast<double>(width) / spacing + 1.0) * (static_cast<double>(height) / spacing + 1.0);
        if (cells <= static_cast<double>(maxMarks))
            break;
        spacing *= 2.0f;
    }
    return spacing;
}

} // namespace trace
