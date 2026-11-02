#pragma once

#include <algorithm>
#include <cmath>

namespace trace {

// How a row of icon buttons fills a panel of a given width: as many columns as fit (each at least `minButton` wide,
// `gap` apart), the buttons sharing the width evenly, wrapping to more rows when the panel is too narrow for all of
// them in one. Pure arithmetic so the panel code and the tests agree.
struct ToolbarLayout {
    int columns = 0;
    int rows = 0;
    float buttonWidth = 0.0f; // whole pixels, never below 1 when there is a button to place
};

inline ToolbarLayout toolbarLayout(float available, int count, float minButton, float gap) {
    ToolbarLayout out;
    if (count <= 0)
        return out;
    minButton = std::max(minButton, 1.0f);
    gap = std::max(gap, 0.0f);
    available = std::max(available, 0.0f);
    const int fit = static_cast<int>(std::floor((available + gap) / (minButton + gap)));
    out.columns = std::clamp(fit, 1, count);
    out.rows = (count + out.columns - 1) / out.columns;
    const float share = (available - gap * static_cast<float>(out.columns - 1)) / static_cast<float>(out.columns);
    out.buttonWidth = std::max(1.0f, std::floor(share));
    return out;
}

} // namespace trace
