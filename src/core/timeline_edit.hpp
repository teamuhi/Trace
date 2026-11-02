#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

#include "core/animation.hpp"
#include "core/cel_grid.hpp"

namespace trace {

// Pure helpers of the Timeline window: where a dragged frame lands, which cells are on screen, the speed text, and
// the playback clock. No ImGui, no Document, so all of it is unit-tested.

// The index moveFrame() takes when frame `from` is dropped on the left or right half of cell `target` (cells run
// left to right, so the left half means "before the target"). Same convention as the Layers panel: the index is the
// position after the dragged frame has been taken out of the row.
inline int frameDropIndex(int from, int target, bool rightHalf) {
    if (from == target)
        return from; // either half of its own cell: it stays (the right half would otherwise be one past the end)
    if (rightHalf)
        return from < target ? target : target + 1;
    return from < target ? target - 1 : target;
}

// The frame after stepping `delta` frames from `active` among `count`, wrapping around when `wrap` is set and
// stopping at the ends otherwise. A bad `count` or `active` gives 0.
inline int steppedFrame(int active, int count, int delta, bool wrap) {
    if (count < 1)
        return 0;
    active = std::clamp(active, 0, count - 1);
    const long long target = static_cast<long long>(active) + delta;
    if (wrap) {
        const long long m = target % count;
        return static_cast<int>(m < 0 ? m + count : m);
    }
    return static_cast<int>(std::clamp<long long>(target, 0, count - 1));
}

struct CellRange {
    int first = 0; // first cell to draw
    int end = 0;   // one past the last (first == end: nothing to draw)
};

// The cells of a row that is `viewWidth` wide and scrolled by `scrollX`, when each cell takes `stride` pixels. One
// cell of margin on both sides keeps a fast scroll from showing a gap. Bad input gives an empty range.
inline CellRange visibleCells(float scrollX, float viewWidth, float stride, int count) {
    if (count < 1 || !(stride > 0.0f) || !std::isfinite(scrollX) || !std::isfinite(viewWidth) || viewWidth <= 0.0f)
        return {};
    const float left = std::max(scrollX, 0.0f);
    const int first = std::clamp(static_cast<int>(std::floor(left / stride)) - 1, 0, count);
    const int end = std::clamp(static_cast<int>(std::ceil((left + viewWidth) / stride)) + 1, first, count);
    return {first, end};
}

// The speed a frame duration stands for as a number for "{0} fps": 1000 / ms with up to two decimals and no
// trailing zeros ("12.5", "10", "0.02"). The duration is clamped to the document's range first.
inline std::string fpsNumber(int durationMs) {
    const double fps = 1000.0 / static_cast<double>(std::clamp(durationMs, kMinFrameMs, kMaxFrameMs));
    char text[32];
    if (fps >= 100.0)
        std::snprintf(text, sizeof(text), "%.0f", fps);
    else if (fps >= 1.0)
        std::snprintf(text, sizeof(text), "%.1f", fps);
    else
        std::snprintf(text, sizeof(text), "%.2f", fps);
    std::string out = text;
    if (out.find('.') != std::string::npos) {
        while (out.back() == '0')
            out.pop_back();
        if (out.back() == '.')
            out.pop_back();
    }
    return out;
}

// One step of a hash that identifies a frame's look (cel ids, layer flags) so a thumbnail is rebuilt exactly when
// it changed. FNV-1a over the eight bytes of `value`.
inline std::uint64_t mixHash(std::uint64_t hash, std::uint64_t value) noexcept {
    for (int i = 0; i < 8; ++i) {
        hash ^= (value >> (8 * i)) & 0xFFu;
        hash *= 1099511628211ull;
    }
    return hash;
}
inline constexpr std::uint64_t kHashSeed = 14695981039346656037ull;

// A frame time in milliseconds from ImGui's last frame time in seconds. A long gap (the window was dragged, the
// machine stalled) counts as at most a quarter second, so playback never skips a long way at once; a negative or
// non-finite value counts as nothing.
inline double clampedFrameMs(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0)
        return 0.0;
    return std::min(seconds * 1000.0, 250.0);
}

// Where Play starts from: the active frame, except that without a loop Play on the last frame starts over from the
// first (otherwise it would stop at once).
inline int playbackStartFrame(int active, int count, bool loop) {
    if (count < 1)
        return 0;
    active = std::clamp(active, 0, count - 1);
    return (!loop && active == count - 1) ? 0 : active;
}

// The playback clock: whether it runs, and the frame and time within it. advancePlayback() does the arithmetic.
class Playback {
public:
    bool playing() const noexcept { return playing_; }
    int frame() const noexcept { return state_.frame; }

    void start(int frame) noexcept {
        state_ = PlaybackState{frame, 0.0};
        playing_ = true;
    }
    void stop() noexcept { playing_ = false; }

    // Moves the clock on by `dtMs` and returns the frame to show. Without `loop` it stops by itself after the last
    // frame's duration. Does nothing (returns the current frame) when not playing.
    int tick(double dtMs, std::span<const int> durationsMs, bool loop) {
        if (!playing_)
            return state_.frame;
        bool finished = false;
        state_ = advancePlayback(state_, dtMs, durationsMs, loop, finished);
        if (finished)
            playing_ = false;
        return state_.frame;
    }

private:
    PlaybackState state_;
    bool playing_ = false;
};

} // namespace trace
