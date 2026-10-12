#include "core/history.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>
#include <vector>

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

std::vector<CelId> sortedIds(const std::vector<CelId>& grid) {
    std::vector<CelId> ids = grid;
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Ids in `a` that are not in `b`; both sorted.
std::vector<CelId> difference(const std::vector<CelId>& a, const std::vector<CelId>& b) {
    std::vector<CelId> out;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    return out;
}

bool hasDuplicates(const std::vector<CelId>& sorted) noexcept {
    return std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end();
}

bool layoutConsistent(const Layout& l) noexcept {
    return !l.layers.empty() && !l.frames.empty() && l.grid.size() == l.layers.size() * l.frames.size() &&
           l.valid(l.activeLayer, l.activeFrame);
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

std::optional<History::StepInfo> History::peekUndo() const noexcept {
    if (!canUndo())
        return std::nullopt;
    const Step& step = steps_[cursor_ - 1];
    return StepInfo{step.structure ? StepKind::Structure : step.whole ? StepKind::Replace : StepKind::Pixels, step.cel};
}

std::optional<History::StepInfo> History::peekRedo() const noexcept {
    if (!canRedo())
        return std::nullopt;
    const Step& step = steps_[cursor_];
    return StepInfo{step.structure ? StepKind::Structure : step.whole ? StepKind::Replace : StepKind::Pixels, step.cel};
}

bool History::commit(const Image& current, Image& reference, const IRect& region, CelId cel) {
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

    step.cel = cel;
    step.width = step.beforeWidth = current.width();
    step.height = step.beforeHeight = current.height();
    bytes_ += step.bytes;
    steps_.push_back(std::move(step));
    cursor_ = steps_.size();
    evictOldest();
    return true;
}

bool History::commitReplace(Image& current, Image& reference, Image next, CelId cel) {
    if (!sameSize(current, reference) || current.empty() || next.empty() || !fits(current))
        return false;

    Image nextReference = next; // everything that can throw happens before anything changes
    steps_.reserve(cursor_ + 1);

    Step step;
    step.cel = cel;
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

std::size_t History::structureBytes(const StructureData& data) noexcept {
    std::size_t bytes = data.before.approxBytes() + data.after.approxBytes() + sizeof(StructureData);
    for (const auto& [id, img] : data.held)
        bytes += imageBytes(img) + sizeof(id);
    return bytes;
}

bool History::commitStructure(CelStore& store, Image& reference, Layout next, CelPool added) {
    Image* active = store.activeImage();
    if (!active || active->empty() || !sameSize(*active, reference) || !fits(*active))
        return false;
    if (!layoutConsistent(store.layout) || !layoutConsistent(next))
        return false;

    // The cel sets must agree with `added` exactly, or cels would leak or vanish.
    const std::vector<CelId> before = sortedIds(store.layout.grid);
    const std::vector<CelId> after = sortedIds(next.grid);
    if (hasDuplicates(before) || hasDuplicates(after) || before.front() == kNoCel || after.front() == kNoCel)
        return false;
    std::vector<CelId> addedIds = difference(after, before);
    std::vector<CelId> removedIds = difference(before, after);
    if (addedIds.size() != added.size())
        return false;
    {
        std::size_t i = 0;
        for (const auto& entry : added) {
            if (entry.first != addedIds[i++] || entry.second.empty())
                return false;
        }
    }
    for (const CelId id : before) {
        if (store.pool.find(id) == store.pool.end())
            return false;
    }

    // The cel that will be active afterwards decides the size the history is at from now on.
    const CelId nextActiveId = next.activeCel();
    const bool activeChanges = nextActiveId != store.layout.activeCel();
    const Image* nextActive = active;
    if (activeChanges) {
        const auto inAdded = added.find(nextActiveId);
        const auto inPool = store.pool.find(nextActiveId);
        nextActive = inAdded != added.end() ? &inAdded->second : inPool != store.pool.end() ? &inPool->second : nullptr;
        if (!nextActive || nextActive->empty())
            return false;
    }
    const int nextWidth = nextActive->width();
    const int nextHeight = nextActive->height();

    // Everything that can throw happens before anything changes.
    Image newReference;
    if (activeChanges)
        newReference = *nextActive;
    auto data = std::make_unique<StructureData>();
    data->before = store.layout;
    data->after = next;
    data->added = std::move(addedIds);
    data->removed = std::move(removedIds);
    steps_.reserve(cursor_ + 1);

    Step step;
    step.width = nextWidth;
    step.height = nextHeight;
    step.beforeWidth = reference.width();
    step.beforeHeight = reference.height();

    // From here on nothing throws: cels move between maps as nodes.
    for (const CelId id : data->removed)
        data->held.insert(store.pool.extract(id));
    store.pool.merge(added);
    store.layout = std::move(next);
    if (activeChanges)
        reference = std::move(newReference);

    step.bytes = structureBytes(*data);
    step.structure = std::move(data);
    discardRedo();
    bytes_ += step.bytes;
    steps_.push_back(std::move(step));
    cursor_ = steps_.size();
    evictOldest();
    return true;
}

IRect History::swapStructure(Step& step, bool undoing, CelStore& store, Image& reference) {
    StructureData& data = *step.structure;
    const Layout& target = undoing ? data.before : data.after;
    const std::vector<CelId>& leaving = undoing ? data.added : data.removed;   // go from the pool into the step
    const std::vector<CelId>& entering = undoing ? data.removed : data.added;  // come back out of the step

    for (const CelId id : leaving) {
        if (store.pool.find(id) == store.pool.end())
            return {};
    }
    for (const CelId id : entering) {
        if (data.held.find(id) == data.held.end())
            return {};
    }

    // A change that only touched metadata keeps the user's current position; one that moved cels around
    // goes back to where the user was at that time.
    Layout next = target;
    if (store.layout.sameGrid(target)) {
        next.activeLayer = store.layout.activeLayer;
        next.activeFrame = store.layout.activeFrame;
    }
    if (!next.valid(next.activeLayer, next.activeFrame))
        return {};
    const CelId nextActiveId = next.activeCel();
    const bool activeChanges = nextActiveId != store.layout.activeCel();

    Image newReference;
    if (activeChanges) {
        const auto inHeld = data.held.find(nextActiveId);
        const auto inPool = store.pool.find(nextActiveId);
        const Image* source = inHeld != data.held.end() ? &inHeld->second : inPool != store.pool.end() ? &inPool->second : nullptr;
        if (!source || source->empty())
            return {};
        newReference = *source;
    }

    // Nothing below throws.
    for (const CelId id : leaving)
        data.held.insert(store.pool.extract(id));
    for (const CelId id : entering)
        store.pool.insert(data.held.extract(id));
    store.layout = std::move(next);
    if (activeChanges)
        reference = std::move(newReference);

    const std::size_t bytes = structureBytes(data);
    bytes_ = bytes_ - step.bytes + bytes;
    step.bytes = bytes;
    return IRect{0, 0, reference.width(), reference.height()};
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
    if (!canUndo() || !sameSize(current, reference) || !fits(current) || steps_[cursor_ - 1].structure)
        return {};
    Step& step = steps_[cursor_ - 1];
    const IRect changed = step.whole ? swapWhole(step, current, reference) : swapWithImage(step, current, reference);
    --cursor_;
    return changed;
}

IRect History::redo(Image& current, Image& reference) {
    if (!canRedo() || !sameSize(current, reference) || !fits(current) || steps_[cursor_].structure)
        return {};
    Step& step = steps_[cursor_];
    const IRect changed = step.whole ? swapWhole(step, current, reference) : swapWithImage(step, current, reference);
    ++cursor_;
    return changed;
}

IRect History::applyStep(bool undoing, CelStore& store, Image& reference) {
    if (undoing ? !canUndo() : !canRedo())
        return {};
    Step& step = steps_[undoing ? cursor_ - 1 : cursor_];
    Image* active = store.activeImage();
    if (!active)
        return {};
    if (!step.structure) {
        if (step.cel != kNoCel && step.cel != store.layout.activeCel())
            return {}; // the caller has to activate the step's cel first
        return undoing ? undo(*active, reference) : redo(*active, reference);
    }
    if (!sameSize(*active, reference) || !fits(*active))
        return {};
    const IRect changed = swapStructure(step, undoing, store, reference);
    if (!changed.empty()) {
        if (undoing)
            --cursor_;
        else
            ++cursor_;
    }
    return changed;
}

IRect History::undo(CelStore& store, Image& reference) {
    return applyStep(true, store, reference);
}

IRect History::redo(CelStore& store, Image& reference) {
    return applyStep(false, store, reference);
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
