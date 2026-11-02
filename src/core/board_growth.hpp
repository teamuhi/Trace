#pragma once

#include <algorithm>

#include "core/cel_grid.hpp"
#include "core/geometry.hpp"

namespace trace {

// How much an endless whiteboard must grow so that a wanted area lies inside it. Pure arithmetic.
struct BoardGrowth {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    bool capped = false; // the board is at its limit: the wanted area does not fit and nothing can be added

    bool any() const noexcept { return left > 0 || top > 0 || right > 0 || bottom > 0; }
};

inline constexpr int kBoardChunk = 256;        // growth is rounded up to a multiple of this
inline constexpr int kBoardMinGrowth = 512;    // a side that grows grows by at least this
inline constexpr int kBoardStartSide = 2048;   // size of a new board

// `want` is the area (in image pixels of a width x height board, so it may reach below 0 or past the size) that
// should be inside the board. A side that must grow grows by the missing amount plus a quarter of the board's size
// (at least kBoardMinGrowth), so the number of growths stays small however far the user travels. When that is more
// than the pixel budget or the side limit allows, the exact missing amount is tried; when even that does not fit the
// result is `capped` and adds nothing.
inline BoardGrowth planBoardGrowth(int width, int height, const IRect& want, int layers, int frames) noexcept {
    BoardGrowth out;
    if (width < 1 || height < 1 || want.empty())
        return out;
    const int needLeft = std::max(0, -want.x0);
    const int needTop = std::max(0, -want.y0);
    const int needRight = std::max(0, want.x1 - width);
    const int needBottom = std::max(0, want.y1 - height);
    if (needLeft == 0 && needTop == 0 && needRight == 0 && needBottom == 0)
        return out;

    const auto roundUp = [](long long v) { return static_cast<int>((v + kBoardChunk - 1) / kBoardChunk * kBoardChunk); };
    const auto slack = [&](int dim) { return std::max(kBoardMinGrowth, dim / 4); };
    const auto sized = [&](int need, int dim, bool extra) -> long long {
        return need == 0 ? 0 : (extra ? roundUp(static_cast<long long>(need) + slack(dim)) : need);
    };

    for (const bool extra : {true, false}) {
        const long long l = sized(needLeft, width, extra);
        const long long t = sized(needTop, height, extra);
        const long long r = sized(needRight, width, extra);
        const long long b = sized(needBottom, height, extra);
        if (fitsDocumentBudget(width + l + r, height + t + b, layers, frames)) {
            out.left = static_cast<int>(l);
            out.top = static_cast<int>(t);
            out.right = static_cast<int>(r);
            out.bottom = static_cast<int>(b);
            return out;
        }
    }
    out.capped = true;
    return out;
}

} // namespace trace
