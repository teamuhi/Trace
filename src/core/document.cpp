#include "core/document.hpp"

#include <algorithm>
#include <new>
#include <utility>

namespace trace {

Document::Document(int width, int height, Rgba8 fill) : image_(width, height, fill), committed_(image_) {
    markAllDirty();
}

void Document::reset(int width, int height, Rgba8 fill) {
    load(Image(width, height, fill));
}

void Document::load(Image image) {
    Image reference = image; // the only step that can throw; nothing has changed yet
    image_ = std::move(image);
    committed_ = std::move(reference);
    upload_ = DirtyRegion{};
    edits_ = DirtyRegion{};
    history_.clear();
    selection_.clear();
    guides_.clear();
    ++generation_;
    markAllDirty();
}

void Document::markDirty(const IRect& r) noexcept {
    upload_.add(r);
    edits_.add(r);
}

void Document::markAllDirty() noexcept {
    upload_.add(bounds());
}

IRect Document::takeDirty() noexcept {
    return upload_.take(bounds());
}

bool Document::commit() {
    const IRect region = intersectionOf(edits_.bounds(), bounds());
    if (region.empty()) {
        edits_ = DirtyRegion{};
        return false;
    }
    try {
        const bool recorded = history_.commit(image_, committed_, region);
        edits_ = DirtyRegion{};
        return recorded;
    } catch (const std::bad_alloc&) {
        return false; // out of memory: the edits stay pending and join the next step
    }
}

bool Document::undo() {
    commit();
    selection_.clear();
    try {
        const IRect changed = history_.undo(image_, committed_);
        upload_.add(changed);
        return !changed.empty();
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool Document::redo() {
    commit();
    selection_.clear();
    try {
        const IRect changed = history_.redo(image_, committed_);
        upload_.add(changed);
        return !changed.empty();
    } catch (const std::bad_alloc&) {
        return false;
    }
}

Document::TransformResult Document::replaceImage(Image next) {
    commit();
    if (!edits_.empty())
        return TransformResult::NoMemory; // the pending edits could not be recorded, and would be lost
    try {
        if (!history_.commitReplace(image_, committed_, std::move(next)))
            return TransformResult::Nothing;
    } catch (const std::bad_alloc&) {
        return TransformResult::NoMemory;
    }
    upload_ = DirtyRegion{};
    edits_ = DirtyRegion{};
    ++generation_; // a gesture that began on the old image must not keep drawing
    markAllDirty();
    return TransformResult::Applied;
}

Document::TransformResult Document::resizeImage(int width, int height, ResizeFilter filter) {
    const int oldW = image_.width();
    const int oldH = image_.height();
    if (image_.empty() || (width == oldW && height == oldH))
        return TransformResult::Nothing;
    clearSelection();
    try {
        Image next = resizedImage(image_, width, height, filter);
        if (next.empty())
            return TransformResult::Nothing;
        const TransformResult result = replaceImage(std::move(next));
        if (result == TransformResult::Applied)
            guides_.scale(oldW, oldH, width, height);
        return result;
    } catch (const std::bad_alloc&) {
        return TransformResult::NoMemory;
    }
}

Document::TransformResult Document::resizeCanvas(int width, int height, Anchor anchor, Rgba8 fill) {
    const int oldW = image_.width();
    const int oldH = image_.height();
    if (image_.empty() || (width == oldW && height == oldH))
        return TransformResult::Nothing;
    clearSelection();
    try {
        Image next = canvasResized(image_, width, height, anchor, fill);
        if (next.empty())
            return TransformResult::Nothing;
        const TransformResult result = replaceImage(std::move(next));
        if (result == TransformResult::Applied) {
            const auto [dx, dy] = anchorOffset(anchor, oldW, oldH, width, height);
            guides_.shift(dx, dy, width, height);
        }
        return result;
    } catch (const std::bad_alloc&) {
        return TransformResult::NoMemory;
    }
}

Document::TransformResult Document::cropToSelection() {
    const IRect rect = intersectionOf(selection_.rect(), bounds());
    if (!selection_.active() || rect.empty())
        return TransformResult::Nothing;
    clearSelection(); // floating pixels are committed into the image first, so the crop includes them
    try {
        Image next = cropped(image_, rect);
        if (next.empty())
            return TransformResult::Nothing;
        const TransformResult result = replaceImage(std::move(next));
        if (result == TransformResult::Applied)
            guides_.shift(-rect.x0, -rect.y0, rect.width(), rect.height());
        return result;
    } catch (const std::bad_alloc&) {
        return TransformResult::NoMemory;
    }
}

void Document::selectRect(const IRect& r) {
    if (selection_.floating())
        clearSelection();
    selection_.setRect(intersectionOf(r, bounds()));
}

void Document::selectAll() {
    clearSelection();
    selection_.setRect(bounds());
}

void Document::clearSelection() {
    commit(); // the floating pixels are already in the image; record them before forgetting the bookkeeping
    selection_.clear();
}

bool Document::liftSelection(Rgba8 clearColor) {
    try {
        markDirty(selection_.lift(image_, clearColor));
        return selection_.floating();
    } catch (const std::bad_alloc&) {
        return false;
    }
}

void Document::moveSelectionTo(int x, int y) {
    markDirty(selection_.moveTo(image_, x, y));
}

void Document::deleteSelection(Rgba8 clearColor) {
    markDirty(selection_.erase(image_, clearColor));
}

bool Document::paste(Image pixels, int x, int y) {
    commit();
    try {
        markDirty(selection_.paste(image_, std::move(pixels), x, y));
        return selection_.floating();
    } catch (const std::bad_alloc&) {
        return false;
    }
}

void Document::fillTestPattern() {
    const int w = image_.width();
    const int h = image_.height();
    if (w == 0 || h == 0)
        return;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Rgba8 c{static_cast<std::uint8_t>(x * 255 / std::max(w - 1, 1)),
                    static_cast<std::uint8_t>(y * 255 / std::max(h - 1, 1)), 160, 255};
            if (x % 64 == 0 || y % 64 == 0)
                c = Rgba8{20, 20, 20, 255};
            else if (x % 8 == 0 && y % 8 == 0)
                c = Rgba8{255, 255, 255, 255};
            image_.set(x, y, c);
        }
    }

    // Edge markers: any off-by-one at the borders shows up immediately when zoomed in.
    const Rgba8 red{230, 30, 30, 255};
    for (int x = 0; x < w; ++x) {
        image_.set(x, 0, red);
        image_.set(x, h - 1, red);
    }
    for (int y = 0; y < h; ++y) {
        image_.set(0, y, red);
        image_.set(w - 1, y, red);
    }

    // Transparent window so the checkerboard shows through.
    const int side = std::min({128, w / 4, h / 4});
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            image_.set(w - 1 - 16 - x, 16 + y, Rgba8{});

    markDirty(bounds());
}

} // namespace trace
