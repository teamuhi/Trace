#pragma once

#include "core/geometry.hpp"
#include "core/image.hpp"

namespace trace {

// A rectangular selection that can be lifted off the image and moved.
//
// While it is *floating*, its pixels are always composited into the image itself: `base_` holds the image
// without them, and every move restores the area it leaves from `base_` and draws the pixels at the new
// place. Rendering, saving and undo therefore never need to know about floating pixels, and dropping the
// selection (clear) is just forgetting the bookkeeping.
//
// Every operation clips to the image and returns the pixels it may have changed.
class Selection {
public:
    bool active() const noexcept { return !rect_.empty(); }
    bool floating() const noexcept { return floating_; }
    const IRect& rect() const noexcept { return rect_; }
    bool contains(int x, int y) const noexcept { return x >= rect_.x0 && x < rect_.x1 && y >= rect_.y0 && y < rect_.y1; }

    // Forgets the selection. A floating selection stays where it is in the image.
    void clear() noexcept;

    // Sets the rectangle of a plain (non-floating) selection, for example while dragging a marquee.
    void setRect(const IRect& rect) noexcept;

    // Cuts the selected pixels out of the image, leaving `clearColor` behind, and makes them floating.
    // Does nothing when already floating or when the selection misses the image.
    IRect lift(Image& image, Rgba8 clearColor);

    // Moves a floating selection so its top-left corner is at (x, y).
    IRect moveTo(Image& image, int x, int y);

    // Removes the selected content: a plain selection is filled with `clearColor`; a floating one is
    // discarded and the selection ends.
    IRect erase(Image& image, Rgba8 clearColor);

    // A copy of the selected pixels (the floating pixels, or the image under a plain selection).
    Image copy(const Image& image) const;

    // Drops `pixels` onto the image as a new floating selection with its top-left corner at (x, y). Any
    // earlier selection is dropped first.
    IRect paste(Image& image, Image pixels, int x, int y);

private:
    void restoreFromBase(Image& image, const IRect& area) const;
    IRect blit(Image& image) const;

    IRect rect_;
    bool floating_ = false;
    Image pixels_; // the floating pixels
    Image base_;   // the image as it is without the floating pixels
};

} // namespace trace
