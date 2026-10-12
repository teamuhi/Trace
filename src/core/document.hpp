#pragma once

#include <cstdint>

#include "core/dirty_rect.hpp"
#include "core/guides.hpp"
#include "core/history.hpp"
#include "core/image.hpp"
#include "core/selection.hpp"
#include "core/transform.hpp"

namespace trace {

// The image being edited, its undo history and the region the renderer still has to upload.
//
// Tools draw straight into image() and report what they touched with markDirty(). commit() then turns the
// difference from the last committed state into one undo step; Toolbox calls it at the end of each gesture.
class Document {
public:
    static constexpr Rgba8 kWhite{255, 255, 255, 255};

    Document() : Document(800, 600) {}
    Document(int width, int height, Rgba8 fill = kWhite);

    Image& image() noexcept { return image_; }
    const Image& image() const noexcept { return image_; }

    // Replace the image: history is cleared and the new image counts as unmodified. Strong guarantee: if
    // allocating the new image throws, the document is untouched.
    void reset(int width, int height, Rgba8 fill = kWhite);
    void load(Image image);

    // Bumped by every reset or load. In-flight strokes compare it to notice that their image was replaced.
    std::uint64_t generation() const noexcept { return generation_; }

    // Pixels in `r` changed: upload them and include them in the next commit.
    void markDirty(const IRect& r) noexcept;
    // Upload everything (for example after the GPU texture was recreated). Not an edit.
    void markAllDirty() noexcept;

    // Returns the pending upload bounds clipped to the image and clears them. Empty when nothing changed.
    IRect takeDirty() noexcept;

    // Turns the pending edits into an undo step. Returns whether a step was recorded (false when the edits
    // left the pixels as they were, as after a cancelled shape).
    bool commit();

    bool canUndo() const noexcept { return history_.canUndo(); }
    bool canRedo() const noexcept { return history_.canRedo(); }
    // Both flush pending edits first. They return whether the image changed (false as well when undoing a
    // resize ran out of memory; nothing changes then).
    bool undo();
    bool redo();

    // True when the image differs from the last markSaved() (or from the freshly created/loaded image).
    bool isModified() const noexcept { return !history_.atSavedState(); }
    void markSaved() noexcept { history_.markSaved(); }

    // --- selection ---------------------------------------------------------------------------------
    // A floating selection lives inside the image (see Selection), so undo, save and rendering need no special
    // cases. Undo, redo and load drop the selection. Callers commit() after a gesture as usual.
    const Selection& selection() const noexcept { return selection_; }
    void selectRect(const IRect& r);          // clipped to the image; ends a floating selection first
    void selectAll();
    void clearSelection();                    // commits, then forgets the selection (floating pixels stay)
    bool liftSelection(Rgba8 clearColor);     // false if there was nothing to lift or memory ran out
    void moveSelectionTo(int x, int y);       // top-left corner, floating selections only
    void deleteSelection(Rgba8 clearColor);
    Image copySelection() const { return selection_.copy(image_); }
    bool paste(Image pixels, int x, int y);   // becomes a floating selection; false on out-of-memory

    // --- whole-image changes ------------------------------------------------------------------------
    // Each one is a single undo step that costs one extra copy of the old image. They commit pending edits,
    // end any selection (floating pixels stay in the image) and move the guides along with the content. The
    // image is only replaced when the whole operation succeeded.
    enum class TransformResult {
        Applied,
        Nothing,  // the request would change nothing (same size, no selection) or is out of range
        NoMemory, // the new image could not be allocated; the document is untouched
    };
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
    IRect bounds() const noexcept { return IRect{0, 0, image_.width(), image_.height()}; }
    TransformResult replaceImage(Image next);

    Image image_;
    Selection selection_;
    Guides guides_;
    Image committed_; // the image as of the last commit; the baseline commit() diffs against
    DirtyRegion upload_;
    DirtyRegion edits_;
    History history_;
    std::uint64_t generation_ = 0;
};

} // namespace trace
