#include "app/pattern_draw.hpp"

#include <algorithm>
#include <cmath>

namespace trace {

void drawPattern(ImDrawList* dl, PatternStyle style, ImVec2 min, ImVec2 max, ImVec2 anchor, float spacing,
                 ImU32 base, ImU32 marks, ImU32 alt) {
    if (!(max.x > min.x) || !(max.y > min.y))
        return;
    dl->AddRectFilled(min, max, base);
    if (style == PatternStyle::Solid || !std::isfinite(anchor.x) || !std::isfinite(anchor.y))
        return;

    const float step = patternSpacingFor(spacing, max.x - min.x, max.y - min.y);
    // First grid index at or before each edge.
    const long long kx0 = static_cast<long long>(std::floor((min.x - anchor.x) / step));
    const long long kx1 = static_cast<long long>(std::ceil((max.x - anchor.x) / step));
    const long long ky0 = static_cast<long long>(std::floor((min.y - anchor.y) / step));
    const long long ky1 = static_cast<long long>(std::ceil((max.y - anchor.y) / step));
    const auto xAt = [&](long long k) { return anchor.x + static_cast<float>(k) * step; };
    const auto yAt = [&](long long k) { return anchor.y + static_cast<float>(k) * step; };

    dl->PushClipRect(min, max, true);
    switch (style) {
    case PatternStyle::Solid:
        break;
    case PatternStyle::Checker:
        for (long long ky = ky0; ky < ky1; ++ky) {
            for (long long kx = kx0; kx < kx1; ++kx) {
                if (((kx + ky) & 1) == 0)
                    continue;
                dl->AddRectFilled(ImVec2(xAt(kx), yAt(ky)), ImVec2(xAt(kx + 1), yAt(ky + 1)), alt);
            }
        }
        break;
    case PatternStyle::Grid:
        for (long long kx = kx0; kx <= kx1; ++kx)
            dl->AddLine(ImVec2(std::round(xAt(kx)), min.y), ImVec2(std::round(xAt(kx)), max.y), marks);
        for (long long ky = ky0; ky <= ky1; ++ky)
            dl->AddLine(ImVec2(min.x, std::round(yAt(ky))), ImVec2(max.x, std::round(yAt(ky))), marks);
        break;
    case PatternStyle::Dots: {
        const float half = std::max(1.0f, std::round(step * 0.07f));
        for (long long ky = ky0; ky <= ky1; ++ky) {
            for (long long kx = kx0; kx <= kx1; ++kx) {
                const float x = std::round(xAt(kx));
                const float y = std::round(yAt(ky));
                dl->AddRectFilled(ImVec2(x - half, y - half), ImVec2(x + half, y + half), marks);
            }
        }
        break;
    }
    case PatternStyle::Cross: {
        const float arm = std::max(2.0f, std::round(step * 0.14f));
        for (long long ky = ky0; ky <= ky1; ++ky) {
            for (long long kx = kx0; kx <= kx1; ++kx) {
                const float x = std::round(xAt(kx));
                const float y = std::round(yAt(ky));
                dl->AddLine(ImVec2(x - arm, y), ImVec2(x + arm + 1.0f, y), marks);
                dl->AddLine(ImVec2(x, y - arm), ImVec2(x, y + arm + 1.0f), marks);
            }
        }
        break;
    }
    case PatternStyle::Diagonal: {
        // 45 degree lines, one every `step` along x. Lines leave the rectangle at the clip edge.
        const float height = max.y - min.y;
        const long long first = static_cast<long long>(std::floor((min.x - height - anchor.x) / step));
        for (long long k = first; k <= kx1; ++k) {
            const float x = xAt(k);
            dl->AddLine(ImVec2(x, min.y), ImVec2(x + height, max.y), marks);
        }
        break;
    }
    }
    dl->PopClipRect();
}

} // namespace trace
