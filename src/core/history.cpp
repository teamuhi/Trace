#include "core/history.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

namespace trace {

namespace {

bool sameSize(const Image& a, const Image& b) noexcept {
    return a.width() == b.width() && a.height() == b.height();
}

bool rowsEqual(const Image& a, const Image& b, int y, int x0, int count) noexcept {
    const std::span<const Rgba8> ra = a.row(y);
    const std::span<const Rgba8> rb = b.row(y);
    return std::memcmp(ra.data() + x0, rb.data() + x0, static_cast<std::size_t>(count) * sizeof(Rgba8)) == 0;
}

bool tileDiffers(const Image& a, const Image& b, int x0, int y0, int w, int h) noexcept {
    for (int y = y0; y < y0 + h; ++y) {
        if (!rowsEqual(a, b, y, x0, w))
            return true;
    }
    return false;
}

std::size_t imageBytes(const Image& img) noexcept {
    return img.pixels().size() * sizeof(Rgba8);
}

} // namespace

bool History::fits(const Image& img) const noexcept {
    if (steps_.empty())
        return true;
    if (cursor_ > 0)
        return img.width() == steps_[cursor_ - 1].width && img.height() == steps_[cursor_ - 1].height;
    // Everything is undone: the image has the size it had before the oldest step.
    return img.width() == steps_.front().beforeWidth && img.height() == steps_.front().beforeHeight;
}

void History::discardRedo() {
    for (std::size_t i = cursor_; i < steps_.size(); ++i)
        bytes_ -= steps_[i].bytes;
    steps_.resize(cursor_);
    if (saved_ && *saved_ > cursor_)
        saved_.reset();
}

void History::clear() noexcept {
    steps_.clear();
    cursor_ = 0;
    bytes_ = 0;
    saved_ = 0;
}

void History::setMaxBytes(std::size_t maxBytes) noexcept {
    maxBytes_ = maxBytes;
    evictOldest();
}

bool History::commit(const Image& current, Image& reference, const IRect& region) {
    if (!sameSize(current, reference) || current.empty() || !fits(current))
        return false;
    const IRect r = intersectionOf(region, IRect{0, 0, current.width(), current.height()});
    if (r.empty())
        return false;

    constexpr int T = kTileSize;
    Step step;
    for (int ty = r.y0 / T; ty <= (r.y1 - 1) / T; ++ty) {
        for (int tx = r.x0 / T; tx <= (r.x1 - 1) / T; ++tx) {
            const int x0 = tx * T;
            const int y0 = ty * T;
            const int w = std::min(T, current.width() - x0);
            const int h = std::min(T, current.height() - y0);
            if (!tileDiffers(current, reference, x0, y0, w, h))
                continue;

            Tile tile{x0, y0, w, h, {}};
            tile.pixels.reserve(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
            for (int y = y0; y < y0 + h; ++y) {
                const std::span<const Rgba8> before = reference.row(y);
                tile.pixels.insert(tile.pixels.end(), before.begin() + x0, before.begin() + x0 + w);
            }
            step.bytes += tile.pixels.size() * sizeof(Rgba8);
            step.tiles.push_back(std::move(tile));
        }
    }
    if (step.tiles.empty())
        return false;
    steps_.reserve(cursor_ + 1); // the last step that can throw: everything below only mutates

    for (const Tile& tile : step.tiles) {
        for (int y = tile.y; y < tile.y + tile.height; ++y) {
            const std::span<const Rgba8> after = current.row(y);
            std::copy(after.begin() + tile.x, after.begin() + tile.x + tile.width, reference.row(y).begin() + tile.x);
        }
    }

    // A new edit forks the timeline: anything that could have been redone is gone, and so is a saved state
    // that lived in that discarded future.
    discardRedo();

    step.width = step.beforeWidth = current.width();
    step.height = step.beforeHeight = current.height();
    bytes_ += step.bytes;
    steps_.push_back(std::move(step));
    cursor_ = steps_.size();
    evictOldest();
    return true;
}

bool History::commitReplace(Image& current, Image& reference, Image next) {
    if (!sameSize(current, reference) || current.empty() || next.empty() || !fits(current))
        return false;

    Image nextReference = next; // everything that can throw happens before anything changes
    steps_.reserve(cursor_ + 1);

    Step step;
    step.width = next.width();
    step.height = next.height();
    step.beforeWidth = reference.width();
    step.beforeHeight = reference.height();
    step.bytes = imageBytes(reference);
    step.whole = std::move(reference);
    reference = std::move(nextReference);
    current = std::move(next);

    discardRedo();
    bytes_ += step.bytes;
    steps_.push_back(std::move(step));
    cursor_ = steps_.size();
    evictOldest();
    return true;
}

// Exchanges the image with the one the step holds. The copy for `reference` is made first, so a failed
// allocation leaves everything as it was.
IRect History::swapWhole(Step& step, Image& current, Image& reference) {
    Image other = *step.whole;
    std::swap(current, *step.whole);
    reference = std::move(other);
    bytes_ = bytes_ - step.bytes + imageBytes(*step.whole);
    step.bytes = imageBytes(*step.whole);
    return IRect{0, 0, current.width(), current.height()};
}

IRect History::swapWithImage(Step& step, Image& current, Image& reference) {
    IRect changed;
    for (Tile& tile : step.tiles) {
        for (int row = 0; row < tile.height; ++row) {
            Rgba8* stored = tile.pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(tile.width);
            const std::span<Rgba8> line = current.row(tile.y + row);
            std::swap_ranges(stored, stored + tile.width, line.begin() + tile.x);
            std::copy(line.begin() + tile.x, line.begin() + tile.x + tile.width,
                      reference.row(tile.y + row).begin() + tile.x);
        }
        changed = unionOf(changed, IRect{tile.x, tile.y, tile.x + tile.width, tile.y + tile.height});
    }
    return changed;
}

IRect History::undo(Image& current, Image& reference) {
    if (!canUndo() || !sameSize(current, reference) || !fits(current))
        return {};
    Step& step = steps_[cursor_ - 1];
    const IRect changed = step.whole ? swapWhole(step, current, reference) : swapWithImage(step, current, reference);
    --cursor_;
    return changed;
}

IRect History::redo(Image& current, Image& reference) {
    if (!canRedo() || !sameSize(current, reference) || !fits(current))
        return {};
    Step& step = steps_[cursor_];
    const IRect changed = step.whole ? swapWhole(step, current, reference) : swapWithImage(step, current, reference);
    ++cursor_;
    return changed;
}

void History::evictOldest() {
    std::size_t drop = 0;
    std::size_t freed = 0;
    // Never drop the newest applied step: one undo is always available.
    while (bytes_ - freed > maxBytes_ && drop + 1 < cursor_) {
        freed += steps_[drop].bytes;
        ++drop;
    }
    if (drop == 0)
        return;

    steps_.erase(steps_.begin(), steps_.begin() + static_cast<std::ptrdiff_t>(drop));
    bytes_ -= freed;
    cursor_ -= drop;
    if (saved_)
        saved_ = *saved_ >= drop ? std::optional<std::size_t>(*saved_ - drop) : std::nullopt;
}

} // namespace trace
