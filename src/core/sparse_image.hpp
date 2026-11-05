#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "core/color.hpp"
#include "core/image.hpp"

namespace trace {

// A copy of an Image that stores only the 64 x 64 tiles holding something. A tile whose pixels are all zero (fully
// transparent black, which is what a new whiteboard is made of) is not stored and reads as zero. The history keeps its
// baseline and the cels a step has taken out of the document in this form: an endless whiteboard is mostly empty, and
// a dense copy of it cost as much as the board itself, twice over.
//
// Exact: every stored pixel reads back as it was written, including the color of transparent pixels.
class SparseImage {
public:
    static constexpr int kTile = 64;

    SparseImage() = default;
    // A copy of `image`. May throw std::bad_alloc.
    explicit SparseImage(const Image& image);
    // Copies are deep (and may throw std::bad_alloc); moves are cheap.
    SparseImage(const SparseImage& other);
    SparseImage& operator=(const SparseImage& other);
    SparseImage(SparseImage&&) noexcept = default;
    SparseImage& operator=(SparseImage&&) noexcept = default;
    ~SparseImage() = default;

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    bool empty() const noexcept { return width_ <= 0 || height_ <= 0; }

    // Out-of-bounds reads return transparent black, as Image does.
    Rgba8 get(int x, int y) const noexcept;

    // Whether `image` (the same size) has exactly the stored pixels in x0, y0, w x h.
    bool equalsRect(const Image& image, int x0, int y0, int w, int h) const noexcept;
    // Copies the stored pixels of x0, y0, w x h, row by row, into `out` (w * h entries).
    void copyRect(int x0, int y0, int w, int h, Rgba8* out) const noexcept;
    // Makes room for assignRect on x0, y0, w x h: every tile it overlaps is allocated now, so the assignRect that
    // follows (and any other on that rectangle) cannot run out of memory. Tiles that stay empty are given back by
    // assignRect. May throw std::bad_alloc, which changes no pixel.
    void reserveRect(int x0, int y0, int w, int h);
    // Makes the stored pixels of x0, y0, w x h equal to `image`'s (the same size). Does not throw after reserveRect
    // on the same rectangle; without it, it may throw std::bad_alloc with the rectangle partly updated.
    void assignRect(const Image& image, int x0, int y0, int w, int h);

    // The whole image again. May throw std::bad_alloc.
    Image toImage() const;

    // Memory the stored tiles take, which is what the history counts.
    std::size_t bytes() const noexcept { return stored_ * sizeof(Tile); }

private:
    using Tile = std::array<Rgba8, static_cast<std::size_t>(kTile) * kTile>;

    std::size_t tileIndex(int tx, int ty) const noexcept {
        return static_cast<std::size_t>(ty) * static_cast<std::size_t>(across_) + static_cast<std::size_t>(tx);
    }

    int width_ = 0;
    int height_ = 0;
    int across_ = 0;
    std::size_t stored_ = 0;
    std::vector<std::unique_ptr<Tile>> tiles_; // null: the tile is all zero
};

} // namespace trace
