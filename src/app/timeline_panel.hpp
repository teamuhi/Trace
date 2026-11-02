#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "app/texture_mirror.hpp"
#include "core/timeline_edit.hpp"

struct SDL_Renderer;

namespace trace {

class Document;
class Toasts;

// The "Timeline" window and the animation clock behind it.
//
// The window is one cell per frame, left to right, scrolling sideways: the frame number, a thumbnail of the frame's
// composite, and its duration in milliseconds (a drag or typed number that applies on release, so one drag is one
// undo step). Click a cell to show that frame, drag one to reorder (a line shows where it lands). Above the cells:
// Play / Pause, Loop, "Frame 3 / 12", the speed of the shown frame in fps, and New, Duplicate and Delete frame.
//
// Playback moves the active frame on a clock (never an undo step, never a change to the saved state). It stops by
// itself on any change made by anything else: an edit, an undo, a layer or frame command, a new or opened file, a
// tool gesture, or the mode being left. The panel owns the clock, so Play works with the window closed.
//
// Thumbnails are small copies of each visible frame's composite, rebuilt only when that frame changed.
class TimelinePanel {
public:
    TimelinePanel() = default;
    ~TimelinePanel() { release(); }

    TimelinePanel(const TimelinePanel&) = delete;
    TimelinePanel& operator=(const TimelinePanel&) = delete;

    void init(SDL_Renderer* renderer) noexcept { renderer_ = renderer; }
    // Must run before the renderer is destroyed (thumbnail textures). Safe to call repeatedly.
    void release() noexcept;
    // The document was replaced (New, Open): stop playing, forget the thumbnails and any edit in progress.
    void reset() noexcept;

    // Once per ImGui frame while Pixel mode is shown, whether or not the window is: runs the playback clock.
    void update(Document& doc, bool gestureActive);
    // Call between ImGui::Begin and ImGui::End.
    void draw(Document& doc, bool gestureActive, Toasts& toasts);

    bool playing() const noexcept { return playback_.playing(); }
    bool loop() const noexcept { return loop_; }
    void setLoop(bool loop) noexcept { loop_ = loop; }
    // Play from the active frame, or pause. Does nothing for a one-frame document.
    void togglePlay(const Document& doc);
    void pause() noexcept { playback_.stop(); }
    // Pauses and shows the frame `delta` away from the active one (wrapping around while Loop is on).
    void stepFrame(Document& doc, int delta, Toasts& toasts);

private:
    struct Thumb {
        TextureMirror mirror;
        std::uint64_t revision = 0; // Document::revision() when it was last built
        std::uint64_t epoch = 0;    // staleEpoch_ when it was last built
        std::uint64_t used = 0;     // drawCount_ when it was last drawn
        bool valid = false;
        bool live = false;          // built while its frame was the active one (it may have changed since)
    };

    Thumb& thumbFor(const Document& doc, int frame, std::uint64_t signature, bool active, bool gestureActive);
    void pruneThumbs(std::size_t visible);
    void setActiveFrame(Document& doc, int frame, Toasts& toasts);

    SDL_Renderer* renderer_ = nullptr;
    std::unordered_map<std::uint64_t, Thumb> thumbs_; // by what the frame looks like, so a reorder rebuilds nothing
    std::uint64_t staleEpoch_ = 1;
    std::uint64_t drawCount_ = 0;
    int lastDrawFrame_ = -1;   // ImGui frame of the previous draw, to notice a gap (hidden panel)
    int lastUpdateFrame_ = -1; // the same for update(), to notice the mode having been left
    int lastShownFrame_ = -1;  // the active frame the strip was last scrolled to
    int scrollTries_ = 0;      // frames left to keep that frame in view

    // Playback
    Playback playback_;
    bool loop_ = true;
    bool fresh_ = false;                  // just started: the first tick counts as no time
    std::uint64_t expectedRevision_ = 0;  // Document::revision() after the last change playback made itself
    std::vector<int> durations_;

    // Duration field
    int editFrame_ = -1; // the frame whose duration is being dragged or typed, or -1
    int editValue_ = 0;
};

} // namespace trace
