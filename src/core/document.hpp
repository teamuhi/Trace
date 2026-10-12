#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "core/cel_grid.hpp"
#include "core/dirty_rect.hpp"
#include "core/guides.hpp"
#include "core/history.hpp"
#include "core/image.hpp"
#include "core/selection.hpp"
#include "core/transform.hpp"

namespace trace {

// The picture being edited, its undo history and the region the renderer still has to upload.
//
// A document is a grid of cels: `layerCount()` layers (bottom to top) times `frameCount()` frames, every cel
// the same size. Paint mode never goes beyond 1 x 1, where this costs nothing: the one cel is the image, the
// display image is that very cel, and history behaves exactly as it always did.
//
// Tools draw straight into image(), the *active* cel, and report what they touched with markDirty(). commit()
// then turns the difference from the last committed state into one undo step; Toolbox calls it at the end
// of each gesture. Pending edits, the committed baseline and the floating selection only ever concern the
// active cel; changing the active cel commits them first.
//
// What the user sees is displayImage(): the active cel itself when exactly one layer is visible at full
// opacity, otherwise a composite that is re-blended only where something changed. Rendering and saving use
// displayImage(), never image().
class Document {
public:
    static constexpr Rgba8 kWhite{255, 255, 255, 255};

    // What an operation did. Nothing: the request would change nothing or is out of range (the document is
    // untouched). NoMemory: an allocation failed; the document is untouched.
    enum class OpResult { Applied, Nothing, NoMemory };
    using TransformResult = OpResult;

    Document() : Document(800, 600) {}
    Document(int width, int height, Rgba8 fill = kWhite);

    // The index of a cel's pixels is part of the document's identity, so a document is moved, not copied.
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) = default;
    Document& operator=(Document&&) = default;

    // The active cel: where tools draw. Never changes identity while the active cel stays the same.
    Image& image() noexcept { return *active_; }
    const Image& image() const noexcept { return *active_; }

    // What is shown and saved. The active cel's own image when a single layer is visible at 255 opacity
    // (the cel of that layer in the active frame), else the composite of the active frame. The reference
    // stays valid until the next change to the document; the composite is brought up to date first.
    const Image& displayImage();
    // The composite of one frame, as a fresh image (for onion skins and export); empty for a bad index.
    // Hidden layers are skipped; layer opacity is the blend coverage. May throw std::bad_alloc.
    Image compositeOfFrame(int frame) const;

    // Replace everything with a single-cel (1 layer x 1 frame) document: history is
    // cleared and the new image counts as unmodified. Strong guarantee: if allocating throws, the document
    // is untouched.
    void reset(int width, int height, Rgba8 fill = kWhite);
    void load(Image image);
    // Replaces everything with a whole layered, animated document (a project file). History is cleared and
    // the result counts as unmodified. Returns false, leaving the document untouched, when `store` is not a
    // valid cel grid (no layers or frames, more than kMaxLayers / kMaxFrames, a grid that does not match the
    // pool, cels of different sizes or empty, or ids that are zero or repeated) or when memory runs out. Layer
    // names are cleaned and frame durations clamped, so a hostile file cannot put anything odd in the document.
    bool loadStore(CelStore store);

    // Counts every change to what is on screen (an edit, an undo or redo, a layer or frame change): thumbnails
    // and other caches compare it to know when to redraw. Not bumped by reading.
    std::uint64_t revision() const noexcept { return revision_; }

    // Bumped whenever the active cel's image object is replaced: load, reset, a resize or crop, changing the
    // active cel, undoing or redoing a step that does. In-flight strokes compare it to notice that their
    // image went away.
    std::uint64_t generation() const noexcept { return generation_; }

    // Pixels in `r` changed: upload them and include them in the next commit.
    void markDirty(const IRect& r) noexcept;
    // Upload everything (for example after the GPU texture was recreated). Not an edit.
    void markAllDirty() noexcept;

    // Returns the pending upload bounds of displayImage() clipped to the image and clears them. Empty when
    // nothing changed. The composite is current inside the returned rectangle.
    IRect takeDirty() noexcept;

    // Turns the pending edits into an undo step. Returns whether a step was recorded (false when the edits
    // left the pixels as they were, as after a cancelled shape).
    bool commit();

    bool canUndo() const noexcept { return history_.canUndo(); }
    bool canRedo() const noexcept { return history_.canRedo(); }
    // Both flush pending edits first. A step that belongs to another cel makes that cel active before it is
    // applied. They return whether the document changed (false as well when undoing a resize ran out of
    // memory; nothing changes then).
    bool undo();
    bool redo();

    // True when the document differs from the last markSaved() (or from the freshly created/loaded one).
    bool isModified() const noexcept { return !history_.atSavedState(); }
    void markSaved() noexcept { history_.markSaved(); }

    // --- layers and frames ---------------------------------------------------------------------------
    // Every call below that returns an OpResult is exactly one undo step when it returns Applied. They
    // commit pending edits and flatten a floating selection first (the pixels stay), and they leave the
    // document untouched on Nothing and NoMemory. Locking and visibility are not enforced here (the UI and
    // Toolbox decide what a locked layer allows); activeEditable() tells whether tools may draw.
    const Layout& layout() const noexcept { return store_.layout; }
    int layerCount() const noexcept { return store_.layout.layerCount(); }
    int frameCount() const noexcept { return store_.layout.frameCount(); }
    int activeLayer() const noexcept { return store_.layout.activeLayer; }
    int activeFrame() const noexcept { return store_.layout.activeFrame; }
    CelId activeCel() const noexcept { return activeCel_; }
    // Out-of-range indices give a default LayerInfo / FrameInfo / empty image / kNoCel.
    const LayerInfo& layer(int index) const noexcept;
    const FrameInfo& frame(int index) const noexcept;
    const Image& cel(int layer, int frame) const noexcept;
    CelId celId(int layer, int frame) const noexcept { return store_.layout.cel(layer, frame); }
    // The active layer is visible and not locked.
    bool activeEditable() const noexcept;

    // Makes (layer, frame) the active cel. Not an undo step (the selection of what to edit is not an edit).
    // Drops a floating selection (its pixels stay where they are) and bumps generation(). Nothing for an
    // out-of-range or already active position.
    OpResult setActive(int layer, int frame);

    // New transparent layer above the active one, which becomes active. An empty name gets "Layer N".
    OpResult addLayer(std::string name = {});
    // Never the last layer.
    OpResult deleteLayer(int layer);
    // A copy of the layer (all its cels) just above it, which becomes active. An empty name is "<name> copy".
    OpResult duplicateLayer(int layer, std::string name = {});
    // Moves the layer to index `to` (0 = bottom). The active layer stays the same layer.
    OpResult moveLayer(int from, int to);
    // Blends `layer` into the one below it in every frame (hidden layers add nothing, the layer's opacity is
    // the coverage); the lower layer keeps its name and settings. Nothing for the bottom layer.
    OpResult mergeDown(int layer);
    // One layer holding the composite of every frame. Nothing when there is only one layer.
    OpResult flatten();
    // Name, visibility, opacity and lock. `props.id` is ignored; a name that is empty after cleaning keeps
    // the old name; names are cut to kMaxLayerNameBytes. Nothing when nothing changes.
    OpResult setLayerProps(int layer, const LayerInfo& props);

    enum class FrameFill { Empty, Duplicate };
    // New frame after the active one, which becomes active; it takes the active frame's duration.
    OpResult addFrame(FrameFill fill = FrameFill::Empty);
    // Never the last frame.
    OpResult deleteFrame(int frame);
    OpResult moveFrame(int from, int to);
    // Clamped to kMinFrameMs..kMaxFrameMs.
    OpResult setFrameDuration(int frame, int durationMs);

    // --- selection ---------------------------------------------------------------------------------
    // A floating selection lives inside the active cel (see Selection), so undo, save and rendering need no
    // special cases. Undo, redo and load drop the selection. Callers commit() after a gesture as usual.
    const Selection& selection() const noexcept { return selection_; }
    void selectRect(const IRect& r);          // clipped to the image; ends a floating selection first
    void selectAll();
    void clearSelection();                    // commits, then forgets the selection (floating pixels stay)
    bool liftSelection(Rgba8 clearColor);     // false if there was nothing to lift or memory ran out
    void moveSelectionTo(int x, int y);       // top-left corner, floating selections only
    void deleteSelection(Rgba8 clearColor);
    Image copySelection() const { return selection_.copy(image()); }
    bool paste(Image pixels, int x, int y);   // becomes a floating selection; false on out-of-memory

    // --- whole-image changes ------------------------------------------------------------------------
    // Each one is a single undo step that applies to every cel and costs one extra copy of the old cels. They
    // commit pending edits, end any selection (floating pixels stay in the image) and move the guides along
    // with the content. Cels are only replaced when the whole operation succeeded.
    TransformResult resizeImage(int width, int height, ResizeFilter filter);
    // Changes the canvas size; the old image sits at `anchor` and new area is filled with `fill`.
    TransformResult resizeCanvas(int width, int height, Anchor anchor, Rgba8 fill);
    TransformResult cropToSelection();

    Guides& guides() noexcept { return guides_; }
    const Guides& guides() const noexcept { return guides_; }

    const History& history() const noexcept { return history_; }
    void setHistoryLimit(std::size_t maxBytes) noexcept { history_.setMaxBytes(maxBytes); }

    // Test helper (no menu item any more): gradient, grid lines, edge markers and a transparent window.
    void fillTestPattern();

private:
    IRect bounds() const noexcept { return IRect{0, 0, active_->width(), active_->height()}; }

    // Re-derives the pointers into the pool and the display mode after the pool or layout changed.
    void refreshDerived() noexcept;
    // Everything on screen may have changed: upload and re-blend all of it.
    void invalidateDisplay() noexcept;
    // Pixels changed on screen (not necessarily an edit).
    void touch(const IRect& r) noexcept;
    bool flushComposite();

    // Commits pending edits and flattens a floating selection; false when edits are still pending (the
    // commit ran out of memory), in which case the caller must not go on.
    bool beginOp();
    // Records `next` (plus the cels it adds) as a structure step and brings the document up to date.
    OpResult applyLayout(Layout next, CelPool added, CelId nextCelId, std::uint32_t nextLayerId);
    OpResult replaceAll(const std::function<Image(const Image&)>& make);
    bool activateCel(CelId id);
    bool stepHistory(bool undoing);

    CelStore store_;
    Image* active_ = nullptr;        // the active cel's image, inside store_.pool
    CelId activeCel_ = kNoCel;
    Image* displayFast_ = nullptr;   // the cel shown as is, or null when a composite is needed
    Image composite_;                // allocated only while a composite is shown
    DirtyRegion compositeDirty_;     // part of composite_ that must be re-blended

    Selection selection_;
    Guides guides_;
    Image committed_; // the active cel as of the last commit; the baseline commit() diffs against
    DirtyRegion upload_;
    DirtyRegion edits_;
    History history_;
    std::uint64_t generation_ = 0;
    std::uint64_t revision_ = 0;
    CelId nextCelId_ = 1;
    std::uint32_t nextLayerId_ = 1;
};

} // namespace trace
