#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "core/cel_grid.hpp"
#include "core/geometry.hpp"
#include "core/image.hpp"

namespace trace {

// Undo/redo as a stack of tile deltas.
//
// The image is divided into kTileSize x kTileSize tiles. A step stores only the tiles that changed, so
// its size follows the edited area, not the image. Each stored tile holds the pixels of one side of the
// change; undoing or redoing swaps those pixels with the image's, so one copy serves both directions.
//
// History never sees the tools. The caller keeps a `reference` image equal to the last committed state and
// calls commit() with the region it touched: whatever differs inside that region becomes a step. A tool
// can therefore not forget to record a pixel.
//
// A document with several layers and frames adds two things. Tile and replace steps remember the CelId they
// belong to, so the caller can make that cel active before undoing or redoing one that happened elsewhere.
// And a *structure* step records a change to the layers and frames themselves (add, delete, duplicate,
// reorder, merge, a property change, a resize of every cel): the layout before and after, plus the cel
// images that the change removed (they live in the step until it is undone or evicted). Cels that a change
// creates or modifies always get fresh ids, so a step never needs a pixel diff of a cel it did not touch.
class History {
public:
    static constexpr int kTileSize = 64;
    static constexpr std::size_t kDefaultMaxBytes = 512u * 1024u * 1024u;

    explicit History(std::size_t maxBytes = kDefaultMaxBytes) noexcept : maxBytes_(maxBytes) {}

    // Forgets every step. The current state becomes the saved state.
    void clear() noexcept;

    // Oldest steps are dropped to stay under the limit, but the newest step is always kept.
    void setMaxBytes(std::size_t maxBytes) noexcept;
    std::size_t maxBytes() const noexcept { return maxBytes_; }
    std::size_t bytes() const noexcept { return bytes_; }

    bool canUndo() const noexcept { return cursor_ > 0; }
    bool canRedo() const noexcept { return cursor_ < steps_.size(); }
    std::size_t undoCount() const noexcept { return cursor_; }
    std::size_t redoCount() const noexcept { return steps_.size() - cursor_; }

    enum class StepKind { Pixels, Replace, Structure };
    struct StepInfo {
        StepKind kind = StepKind::Pixels;
        CelId cel = kNoCel; // the cel a Pixels or Replace step belongs to (kNoCel when the caller gave none)
    };
    // The step an undo or redo would apply next, if any.
    std::optional<StepInfo> peekUndo() const noexcept;
    std::optional<StepInfo> peekRedo() const noexcept;

    // Records what differs between `current` and `reference` inside `region` (whole tiles are compared)
    // as a new step and brings `reference` up to date. Discards any redo steps. Returns false, changing
    // nothing in the history, when nothing differs, the region is empty, the images' sizes disagree or they
    // differ from the size the history is at (a size change goes through commitReplace).
    bool commit(const Image& current, Image& reference, const IRect& region, CelId cel = kNoCel);

    // Records a change of the whole image, such as a resize or a crop: `current` and `reference` (which must
    // already be equal, so commit any pending edit first) become `next`, and the step keeps the old image so
    // undo can bring it back. Costs one full image per step. Discards any redo steps. Returns false and
    // changes nothing when the images disagree or `next` is empty. May throw std::bad_alloc, again without
    // changing anything.
    bool commitReplace(Image& current, Image& reference, Image next, CelId cel = kNoCel);

    // Records a change to the layers and frames. `store` is the document's layout and cel pool; `reference`
    // mirrors its active cel. `next` is the layout after the change, `added` the new cel images it
    // introduces (exactly the ids in `next` that `store` does not have). Cels that `next` no longer lists
    // move from the pool into the step. On success `store` is in the new state and `reference` is a copy of
    // the new active cel. Returns false, changing nothing, when the arguments disagree (unknown or missing
    // ids, an invalid active position, sizes the history is not at). May throw std::bad_alloc, again without
    // changing anything. Pending pixel edits must be committed first. Discards any redo steps.
    bool commitStructure(CelStore& store, Image& reference, Layout next, CelPool added);

    // Move `current` one step back or forward and keep `reference` in sync. Return the changed bounds,
    // empty when there was nothing to do or the images are not the size the steps belong to. Undoing or
    // redoing a commitReplace step may throw std::bad_alloc, before anything is changed.
    IRect undo(Image& current, Image& reference);
    IRect redo(Image& current, Image& reference);

    // The same for any kind of step, against a document's whole cel store. A Pixels or Replace step is
    // applied only when its cel is the active one (see peekUndo); a structure step restores the layout and
    // moves cels in or out of the pool, so references into the pool may be gone afterwards. Return the bounds
    // of the new active cel, empty when nothing happened. May throw std::bad_alloc before anything changes.
    IRect undo(CelStore& store, Image& reference);
    IRect redo(CelStore& store, Image& reference);

    // The saved state is the one a file on disk matches. It stops being reachable once a new commit
    // discards it or eviction drops the step that leads back to it.
    void markSaved() noexcept { saved_ = cursor_; }
    bool atSavedState() const noexcept { return saved_ && *saved_ == cursor_; }

private:
    struct Tile {
        int x = 0; // pixel position of the tile's top-left corner
        int y = 0;
        int width = 0;
        int height = 0;
        std::vector<Rgba8> pixels; // width * height, row-major
    };
    struct StructureData {
        Layout before;
        Layout after;
        CelPool held;                // the cels that are not in the document right now
        std::vector<CelId> added;    // in `after` but not in `before` (sorted)
        std::vector<CelId> removed;  // in `before` but not in `after` (sorted)
    };
    struct Step {
        std::vector<Tile> tiles;       // a pixel step: the pixels of one side of the change
        std::optional<Image> whole;    // a replace step: the whole image on the other side of the change
        std::unique_ptr<StructureData> structure; // a structure step
        CelId cel = kNoCel;            // the cel a pixel or replace step belongs to
        int width = 0;                 // size of the image while this step is applied
        int height = 0;
        int beforeWidth = 0;           // and while it is undone (equal for pixel steps)
        int beforeHeight = 0;
        std::size_t bytes = 0;
    };

    static IRect swapWithImage(Step& step, Image& current, Image& reference);
    IRect swapWhole(Step& step, Image& current, Image& reference);
    IRect swapStructure(Step& step, bool undoing, CelStore& store, Image& reference);
    IRect applyStep(bool undoing, CelStore& store, Image& reference);
    static std::size_t structureBytes(const StructureData& data) noexcept;
    void discardRedo();
    void evictOldest();

    // True when `img` is the size the image has at the current position in the history (always true while
    // there are no steps).
    bool fits(const Image& img) const noexcept;

    std::vector<Step> steps_;
    std::size_t cursor_ = 0; // steps_[0, cursor_) are applied; the rest can be redone
    std::size_t bytes_ = 0;
    std::size_t maxBytes_;
    std::optional<std::size_t> saved_ = 0; // cursor_ value of the saved state, if still reachable
};

} // namespace trace
