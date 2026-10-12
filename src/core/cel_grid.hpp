#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <string>
#include <vector>

#include "core/image.hpp"

namespace trace {

// A cel is the picture one layer shows in one frame. Every cel has an id that never changes while the cel
// exists and is never reused, so history steps can name the cel they belong to.
using CelId = std::uint32_t;
inline constexpr CelId kNoCel = 0;

inline constexpr int kMaxLayers = 256;
inline constexpr int kMaxFrames = 999;
inline constexpr int kMinFrameMs = 1;
inline constexpr int kMaxFrameMs = 60000;
inline constexpr int kDefaultFrameMs = 100;
inline constexpr std::size_t kMaxLayerNameBytes = 64;

// Pixel Art canvases are small by design; the pixel workspace enforces this (Paint images go to 16384).
inline constexpr int kMaxPixelCanvasSide = 1024;
// All cels of one document together may hold at most this many pixels (4 bytes each, so 512 MiB). 256 layers
// by 999 frames of a 1024 x 1024 canvas would otherwise be about a terabyte.
inline constexpr std::size_t kMaxDocumentPixels = 128u * 1024u * 1024u;

// Whether a width x height canvas with that many layers and frames stays inside kMaxDocumentPixels. Any
// non-positive argument gives false. Computed in 64 bits, so it cannot overflow.
inline bool fitsDocumentBudget(long long width, long long height, long long layers, long long frames) noexcept {
    if (width < 1 || height < 1 || layers < 1 || frames < 1)
        return false;
    if (width > kMaxPixelCanvasSide * 16LL || height > kMaxPixelCanvasSide * 16LL || layers > kMaxLayers ||
        frames > kMaxFrames)
        return false; // beyond any real document; also keeps the product below 2^63
    return width * height * layers * frames <= static_cast<long long>(kMaxDocumentPixels);
}

struct LayerInfo {
    std::uint32_t id = 0; // stable for the layer's lifetime (reordering keeps it)
    std::string name;
    bool visible = true;
    std::uint8_t opacity = 255; // 0..255; used as the coverage when the layer is composited
    bool locked = false;

    bool operator==(const LayerInfo&) const = default;
};

struct FrameInfo {
    int durationMs = kDefaultFrameMs;

    bool operator==(const FrameInfo&) const = default;
};

// Everything about a document's layers and frames except the pixels: metadata plus which cel sits at every
// (layer, frame) position. Layer 0 is the bottom of the stack. The grid is frame-major.
struct Layout {
    std::vector<LayerInfo> layers;
    std::vector<FrameInfo> frames;
    std::vector<CelId> grid; // frames.size() * layers.size() ids; grid[frame * layers.size() + layer]
    int activeLayer = 0;
    int activeFrame = 0;

    int layerCount() const noexcept { return static_cast<int>(layers.size()); }
    int frameCount() const noexcept { return static_cast<int>(frames.size()); }

    bool valid(int layer, int frame) const noexcept {
        return layer >= 0 && layer < layerCount() && frame >= 0 && frame < frameCount();
    }
    CelId cel(int layer, int frame) const noexcept {
        return valid(layer, frame) ? grid[static_cast<std::size_t>(frame) * layers.size() + static_cast<std::size_t>(layer)] : kNoCel;
    }
    CelId activeCel() const noexcept { return cel(activeLayer, activeFrame); }

    // Position of a cel, or {-1, -1}.
    std::pair<int, int> find(CelId id) const noexcept {
        if (id == kNoCel || layers.empty())
            return {-1, -1};
        const auto it = std::find(grid.begin(), grid.end(), id);
        if (it == grid.end())
            return {-1, -1};
        const auto index = static_cast<std::size_t>(it - grid.begin());
        return {static_cast<int>(index % layers.size()), static_cast<int>(index / layers.size())};
    }

    // Same layers, frames and cels (the metadata may differ).
    bool sameGrid(const Layout& other) const noexcept {
        return layers.size() == other.layers.size() && frames.size() == other.frames.size() && grid == other.grid;
    }

    std::size_t approxBytes() const noexcept {
        std::size_t bytes = sizeof(Layout) + grid.size() * sizeof(CelId) + frames.size() * sizeof(FrameInfo);
        for (const LayerInfo& l : layers)
            bytes += sizeof(LayerInfo) + l.name.size();
        return bytes;
    }

    bool operator==(const Layout&) const = default;
};

// The pixels of every cel, by id. A std::map keeps references to the images stable while cels come and go,
// and moves a cel between maps without allocating.
using CelPool = std::map<CelId, Image>;

// The part of a document that history can restore as a whole.
struct CelStore {
    Layout layout;
    CelPool pool;

    Image* activeImage() noexcept {
        const auto it = pool.find(layout.activeCel());
        return it == pool.end() ? nullptr : &it->second;
    }
};

} // namespace trace
