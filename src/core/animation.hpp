#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "core/geometry.hpp"
#include "core/image.hpp"

namespace trace {

// Pure helpers for Pixel Art animation: onion skin frame choice, playback timing and sprite sheet layout. No ImGui,
// no files.

// --- onion skin ------------------------------------------------------------------------------------

struct OnionFrame {
    int frame = 0;     // index of the frame to draw
    int distance = 1;  // 1 = the frame next to the active one, 2 = two away ... (fade by this)
    bool previous = true; // a frame before the active one (tinted "previous"), else one after it
};

// The frames to ghost behind frame `active` of `frameCount`: up to `before` previous and `after` next ones, nearest
// first, previous before next at equal distance. Without `loop` there is no previous frame for frame 0 and no next one
// for the last frame; with `loop` the neighbours wrap around. A frame appears once (never the active frame, never
// twice when the animation is short and wraps onto itself). Bad input (no frames, `active` out of range, negative
// counts) gives an empty list; `before` and `after` are capped at 8.
std::vector<OnionFrame> onionFrames(int active, int frameCount, int before, int after, bool loop);

// --- playback --------------------------------------------------------------------------------------

struct PlaybackState {
    int frame = 0;          // the frame being shown
    double elapsedMs = 0.0; // how long it has been shown
};

// Advances playback by `dtMs` over frames whose durations are `durationsMs` (each clamped to 1..60000; an empty
// span means nothing to play). Handles any `dtMs`, including one that skips several frames at once. With `loop` the
// animation wraps from the last frame to the first; without it playback stops on the last frame once its duration
// has passed and `finished` is set. A negative or non-finite `dtMs` changes nothing; an out-of-range `state.frame`
// is clamped first.
PlaybackState advancePlayback(PlaybackState state, double dtMs, std::span<const int> durationsMs, bool loop,
                              bool& finished);

// --- sprite sheet ----------------------------------------------------------------------------------

enum class SheetMode { Row, Column, Grid };

struct SheetOptions {
    SheetMode mode = SheetMode::Row;
    int columns = 4;  // Grid only: frames per row (clamped to 1..frames)
    int padding = 0;  // empty pixels between cells and around the sheet's edge, in output pixels (0..64)
    int scale = 1;    // integer scale of every frame, 1..32
};

struct SheetLayout {
    bool ok = false;
    std::string error;        // when !ok: a message fit to show the user
    int width = 0;            // the whole sheet in output pixels
    int height = 0;
    int cellWidth = 0;        // one frame in output pixels (frame size * scale)
    int cellHeight = 0;
    std::vector<IRect> cells; // where each frame goes, in order
};

// How `frameCount` frames of `frameWidth` x `frameHeight` pixels are laid out. Refused (ok = false, with a message)
// when there is no frame, the frame is empty, the options are out of range after clamping cannot fix them, or the
// sheet would pass kMaxExportSide (16384) on either side; the size is computed in 64 bits so it cannot overflow.
inline constexpr int kMaxExportSide = 16384;
SheetLayout sheetLayout(int frameCount, int frameWidth, int frameHeight, const SheetOptions& options);

// The sheet picture: transparent background, every frame scaled (nearest) by options.scale and placed in its cell.
// `frames` must all have the same size as the one the layout was made for and as many entries as layout.cells; any
// mismatch or an out-of-memory returns an empty image. Never throws.
Image buildSpriteSheet(std::span<const Image> frames, const SheetLayout& layout, int scale);

// `image` enlarged by an integer factor with nearest sampling (1 returns a copy). Empty on a bad factor, an empty
// source, a result past kMaxExportSide or out of memory.
Image scaledNearest(const Image& image, int scale);

} // namespace trace
