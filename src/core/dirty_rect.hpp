#pragma once

#include "core/geometry.hpp"

namespace trace {

// Accumulates modified regions as a single bounding rectangle. One upload per frame beats many small ones.
class DirtyRegion {
public:
    void add(const IRect& r) noexcept { bounds_ = unionOf(bounds_, r); }
    void addPixel(int x, int y) noexcept { add(IRect{x, y, x + 1, y + 1}); }

    bool empty() const noexcept { return bounds_.empty(); }
    const IRect& bounds() const noexcept { return bounds_; }

    // Returns the accumulated bounds clipped to `limit` and resets the region.
    IRect take(const IRect& limit) noexcept {
        const IRect result = intersectionOf(bounds_, limit);
        bounds_ = IRect{};
        return result;
    }

private:
    IRect bounds_;
};

} // namespace trace
