#include "core/animation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>
#include <string>

namespace trace {

namespace {

constexpr int kMaxOnionCount = 8;
constexpr int kMaxSheetPadding = 64;
constexpr int kMaxSheetScale = 32;
constexpr int kMaxSheetFrames = 1 << 20; // far above any document; keeps the cell list small
constexpr double kMaxStepMs = 1e15; // a longer pause than this (about 30000 years) is the same as this

int clampedDuration(int ms) noexcept {
    return std::clamp(ms, 1, 60000);
}

// `index` brought into 0..count-1 by wrapping around, for any integer (count >= 1).
int wrapped(long long index, int count) noexcept {
    const long long n = count;
    return static_cast<int>(((index % n) + n) % n);
}

} // namespace

// --- onion skin ------------------------------------------------------------------------------------

std::vector<OnionFrame> onionFrames(int active, int frameCount, int before, int after, bool loop) {
    std::vector<OnionFrame> result;
    if (frameCount < 1 || active < 0 || active >= frameCount || before < 0 || after < 0)
        return result;
    before = std::min(before, kMaxOnionCount);
    after = std::min(after, kMaxOnionCount);

    const auto taken = [&](long long index) {
        return index == active || std::any_of(result.begin(), result.end(), [&](const OnionFrame& f) { return f.frame == index; });
    };
    const auto take = [&](long long index, int distance, bool previous) {
        if (index < 0 || index >= frameCount) {
            if (!loop)
                return;
            index = wrapped(index, frameCount);
        }
        if (taken(index))
            return; // the active frame, or a short animation wrapping onto a frame already listed
        result.push_back(OnionFrame{static_cast<int>(index), distance, previous});
    };

    for (int d = 1; d <= std::max(before, after); ++d) {
        if (d <= before)
            take(static_cast<long long>(active) - d, d, true);
        if (d <= after)
            take(static_cast<long long>(active) + d, d, false);
    }
    return result;
}

// --- playback --------------------------------------------------------------------------------------

PlaybackState advancePlayback(PlaybackState state, double dtMs, std::span<const int> durationsMs, bool loop,
                              bool& finished) {
    finished = false;
    if (durationsMs.empty())
        return PlaybackState{};
    const int count = static_cast<int>(std::min<std::size_t>(durationsMs.size(), 1u << 30));
    const auto duration = [&](int i) { return static_cast<double>(clampedDuration(durationsMs[static_cast<std::size_t>(i)])); };

    state.frame = std::clamp(state.frame, 0, count - 1);
    if (!std::isfinite(state.elapsedMs) || state.elapsedMs < 0.0)
        state.elapsedMs = 0.0;
    if (!std::isfinite(dtMs) || dtMs < 0.0)
        return state;
    double elapsed = state.elapsedMs + std::min(dtMs, kMaxStepMs);
    int frame = state.frame;

    if (loop) {
        // Work on the position inside one whole cycle so a huge step costs one pass, not millions.
        double cycle = 0.0;
        double start = 0.0; // where the current frame begins in the cycle
        for (int i = 0; i < count; ++i) {
            if (i == frame)
                start = cycle;
            cycle += duration(i);
        }
        double position = std::fmod(start + elapsed, cycle);
        for (int i = 0; i < count; ++i) {
            const double length = duration(i);
            if (position < length)
                return PlaybackState{i, position};
            position -= length;
        }
        return PlaybackState{count - 1, duration(count - 1)}; // rounding left a sliver past the end
    }

    for (; frame < count - 1 && elapsed >= duration(frame); ++frame)
        elapsed -= duration(frame);
    if (frame == count - 1 && elapsed >= duration(frame)) {
        elapsed = duration(frame);
        finished = true;
    }
    return PlaybackState{frame, elapsed};
}

// --- sprite sheet ----------------------------------------------------------------------------------

SheetLayout sheetLayout(int frameCount, int frameWidth, int frameHeight, const SheetOptions& options) {
    SheetLayout layout;
    if (frameCount < 1) {
        layout.error = "There are no frames to put on the sheet.";
        return layout;
    }
    if (frameCount > kMaxSheetFrames) {
        layout.error = "There are too many frames for one sheet.";
        return layout;
    }
    if (frameWidth < 1 || frameHeight < 1) {
        layout.error = "The frames are empty.";
        return layout;
    }
    if (options.scale < 1 || options.scale > kMaxSheetScale) {
        layout.error = "The scale must be between 1 and " + std::to_string(kMaxSheetScale) + ".";
        return layout;
    }
    if (options.mode != SheetMode::Row && options.mode != SheetMode::Column && options.mode != SheetMode::Grid) {
        layout.error = "The sheet arrangement is not valid.";
        return layout;
    }

    const long long count = frameCount;
    const long long padding = std::clamp(options.padding, 0, kMaxSheetPadding);
    const long long cellW = static_cast<long long>(frameWidth) * options.scale;
    const long long cellH = static_cast<long long>(frameHeight) * options.scale;
    long long columns = count;
    switch (options.mode) {
    case SheetMode::Row: columns = count; break;
    case SheetMode::Column: columns = 1; break;
    case SheetMode::Grid: columns = std::clamp<long long>(options.columns, 1, count); break;
    }
    const long long rows = (count + columns - 1) / columns;
    const long long width = padding + columns * (cellW + padding);
    const long long height = padding + rows * (cellH + padding);
    if (width > kMaxExportSide || height > kMaxExportSide) {
        layout.error = "The sheet would be " + std::to_string(width) + " x " + std::to_string(height) +
                       " pixels, over the limit of " + std::to_string(kMaxExportSide) +
                       " pixels on a side. Use a smaller scale, less padding or another arrangement.";
        return layout;
    }

    try {
        layout.cells.reserve(static_cast<std::size_t>(count));
        for (long long i = 0; i < count; ++i) {
            const long long x = padding + (i % columns) * (cellW + padding);
            const long long y = padding + (i / columns) * (cellH + padding);
            layout.cells.push_back(IRect{static_cast<int>(x), static_cast<int>(y), static_cast<int>(x + cellW),
                                         static_cast<int>(y + cellH)});
        }
    } catch (const std::bad_alloc&) {
        layout.cells.clear();
        layout.error = "There is not enough memory for the sheet.";
        return layout;
    }
    layout.width = static_cast<int>(width);
    layout.height = static_cast<int>(height);
    layout.cellWidth = static_cast<int>(cellW);
    layout.cellHeight = static_cast<int>(cellH);
    layout.ok = true;
    return layout;
}

Image buildSpriteSheet(std::span<const Image> frames, const SheetLayout& layout, int scale) {
    if (!layout.ok || frames.empty() || frames.size() != layout.cells.size() || scale < 1 || scale > kMaxSheetScale ||
        layout.width < 1 || layout.height < 1 || layout.width > kMaxExportSide || layout.height > kMaxExportSide)
        return {};
    const int frameW = frames.front().width();
    const int frameH = frames.front().height();
    if (frameW < 1 || frameH < 1 || layout.cellWidth != frameW * scale || layout.cellHeight != frameH * scale)
        return {};
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const IRect& cell = layout.cells[i];
        if (frames[i].width() != frameW || frames[i].height() != frameH || cell.x0 < 0 || cell.y0 < 0 ||
            cell.width() != layout.cellWidth || cell.height() != layout.cellHeight || cell.x1 > layout.width ||
            cell.y1 > layout.height)
            return {};
    }

    try {
        Image sheet(layout.width, layout.height);
        if (sheet.empty())
            return {};
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const IRect& cell = layout.cells[i];
            for (int y = 0; y < cell.height(); ++y) {
                const std::span<const Rgba8> source = frames[i].row(y / scale);
                const std::span<Rgba8> target = sheet.row(cell.y0 + y);
                for (int x = 0; x < cell.width(); ++x)
                    target[static_cast<std::size_t>(cell.x0 + x)] = source[static_cast<std::size_t>(x / scale)];
            }
        }
        return sheet;
    } catch (...) {
        return {};
    }
}

Image scaledNearest(const Image& image, int scale) {
    if (image.empty() || scale < 1 || scale > kMaxSheetScale ||
        static_cast<long long>(image.width()) * scale > kMaxExportSide ||
        static_cast<long long>(image.height()) * scale > kMaxExportSide)
        return {};
    try {
        if (scale == 1)
            return image;
        Image out(image.width() * scale, image.height() * scale);
        if (out.empty())
            return {};
        for (int y = 0; y < out.height(); ++y) {
            const std::span<const Rgba8> source = image.row(y / scale);
            const std::span<Rgba8> target = out.row(y);
            for (int x = 0; x < out.width(); ++x)
                target[static_cast<std::size_t>(x)] = source[static_cast<std::size_t>(x / scale)];
        }
        return out;
    } catch (...) {
        return {};
    }
}

} // namespace trace
