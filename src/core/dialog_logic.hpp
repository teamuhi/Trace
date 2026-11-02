#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include "core/transform.hpp"

namespace trace {

// Pure logic behind the modal dialogs (Canvas Size, Resize), kept apart from ImGui so it can be tested.

// --- the 3 x 3 anchor grid ---------------------------------------------------------------------

constexpr int anchorColumn(Anchor a) noexcept {
    return static_cast<int>(a) % 3;
}
constexpr int anchorRow(Anchor a) noexcept {
    return static_cast<int>(a) / 3;
}
// The anchor at a grid cell; a column or row outside 0..2 is clamped to the nearest edge.
constexpr Anchor anchorAt(int column, int row) noexcept {
    return static_cast<Anchor>(std::clamp(row, 0, 2) * 3 + std::clamp(column, 0, 2));
}

// --- number fields -----------------------------------------------------------------------------

// `value` moved by `delta` (an arrow button of a number field), kept inside [lo, hi]. Safe for any int.
constexpr int steppedValue(int value, int delta, int lo, int hi) noexcept {
    const std::int64_t moved = static_cast<std::int64_t>(value) + static_cast<std::int64_t>(delta);
    return static_cast<int>(std::clamp<std::int64_t>(moved, lo, std::max(lo, hi)));
}

// --- size summaries ----------------------------------------------------------------------------

// "16 x 16" when nothing changes, otherwise "16 x 16 -> 32 x 24".
inline std::string sizeChangeText(int oldWidth, int oldHeight, int newWidth, int newHeight) {
    std::string text = std::to_string(oldWidth) + " x " + std::to_string(oldHeight);
    if (oldWidth != newWidth || oldHeight != newHeight)
        text += " -> " + std::to_string(newWidth) + " x " + std::to_string(newHeight);
    return text;
}

// A rectangle in pixels of the new canvas.
struct KeptRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0; // exclusive
    int y1 = 0;
    bool empty() const noexcept { return x1 <= x0 || y1 <= y0; }
};

// The part of the old picture that is still on a canvas of newWidth x newHeight when the picture is placed by
// `anchor`, in pixels of the new canvas (what the Canvas Size preview draws on top of the new area).
inline KeptRect keptPictureRect(int oldWidth, int oldHeight, int newWidth, int newHeight, Anchor anchor) noexcept {
    const auto [ox, oy] = anchorOffset(anchor, oldWidth, oldHeight, newWidth, newHeight);
    KeptRect r;
    r.x0 = std::clamp(ox, 0, newWidth);
    r.y0 = std::clamp(oy, 0, newHeight);
    r.x1 = std::clamp(ox + oldWidth, 0, newWidth);
    r.y1 = std::clamp(oy + oldHeight, 0, newHeight);
    return r;
}

} // namespace trace
