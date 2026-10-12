#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace trace {

// A vertical guide is a line at an x position, a horizontal one a line at a y position. Positions are whole
// pixel boundaries: guide 5 lies between pixel columns 4 and 5, and 0 and the image size are the image edges.
enum class GuideAxis { Vertical, Horizontal };

// The guides of one document, kept sorted without duplicates. Pure data and arithmetic, no input handling.
class Guides {
public:
    static constexpr std::size_t kMaxPerAxis = 128;

    const std::vector<int>& positions(GuideAxis axis) const noexcept {
        return axis == GuideAxis::Vertical ? vertical_ : horizontal_;
    }
    bool empty() const noexcept { return vertical_.empty() && horizontal_.empty(); }
    std::size_t count() const noexcept { return vertical_.size() + horizontal_.size(); }

    // False when the position is already taken or the axis is full.
    bool add(GuideAxis axis, int position);
    bool remove(GuideAxis axis, int position);
    // Moves a guide; landing on another guide merges them. False when `from` does not exist.
    bool move(GuideAxis axis, int from, int to);
    void clear() noexcept;

    // The guide closest to `position` (image pixels) that is at most `radius` away.
    std::optional<int> nearest(GuideAxis axis, float position, float radius) const noexcept;

    // Snapping for tools. All return `value` unchanged when no guide is within `radius` (image pixels).
    //
    // Start of a drag: the pointer position moves onto the guide.
    float snapStart(GuideAxis axis, float value, float radius) const noexcept;
    // Far corner of a box whose corner pixels are inclusive: picks the pixel that puts the box edge on the
    // guide, on the near or far side of `anchor` depending on the drag direction. Returns a pixel center.
    float snapEnd(GuideAxis axis, float value, float anchor, float radius) const noexcept;
    // A span of `length` pixels starting at `start` (a moved selection): lines up whichever of its two edges
    // is closest to a guide. Returns the new start.
    int snapSpan(GuideAxis axis, int start, int length, float radius) const noexcept;

    // After the canvas changed. `shift` moves guides with the content (canvas size, crop); `scale`
    // proportionally follows a resize. Guides that end up outside [0, size] are dropped.
    void shift(int dx, int dy, int width, int height);
    void scale(int oldWidth, int oldHeight, int newWidth, int newHeight);

    bool operator==(const Guides&) const = default;

private:
    std::vector<int>& list(GuideAxis axis) noexcept { return axis == GuideAxis::Vertical ? vertical_ : horizontal_; }

    std::vector<int> vertical_;
    std::vector<int> horizontal_;
};

} // namespace trace
