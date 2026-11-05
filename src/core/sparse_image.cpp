#include "core/sparse_image.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace trace {

namespace {

bool allZero(const Rgba8* p, std::size_t count) noexcept {
    const auto* bytes = reinterpret_cast<const unsigned char*>(p);
    const std::size_t n = count * sizeof(Rgba8);
    // The first byte or two usually decide it; memcmp against the same buffer shifted by one covers the rest.
    return n == 0 || (bytes[0] == 0 && std::memcmp(bytes, bytes + 1, n - 1) == 0);
}

} // namespace

SparseImage::SparseImage(const Image& image) : width_(image.width()), height_(image.height()) {
    if (empty()) {
        width_ = 0;
        height_ = 0;
        return;
    }
    across_ = (width_ + kTile - 1) / kTile;
    const int down = (height_ + kTile - 1) / kTile;
    tiles_.resize(static_cast<std::size_t>(across_) * static_cast<std::size_t>(down));
    for (int ty = 0; ty < down; ++ty) {
        for (int tx = 0; tx < across_; ++tx) {
            const int x0 = tx * kTile;
            const int y0 = ty * kTile;
            assignRect(image, x0, y0, std::min(kTile, width_ - x0), std::min(kTile, height_ - y0));
        }
    }
}

SparseImage::SparseImage(const SparseImage& other)
    : width_(other.width_), height_(other.height_), across_(other.across_), stored_(other.stored_) {
    tiles_.resize(other.tiles_.size());
    for (std::size_t i = 0; i < tiles_.size(); ++i)
        if (other.tiles_[i])
            tiles_[i] = std::make_unique<Tile>(*other.tiles_[i]);
}

SparseImage& SparseImage::operator=(const SparseImage& other) {
    if (this != &other) {
        SparseImage copy(other); // the copy can throw; this stays as it was until it is done
        *this = std::move(copy);
    }
    return *this;
}

Rgba8 SparseImage::get(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
        return Rgba8{};
    const Tile* tile = tiles_[tileIndex(x / kTile, y / kTile)].get();
    return tile ? (*tile)[(y % kTile) * kTile + (x % kTile)] : Rgba8{};
}

bool SparseImage::equalsRect(const Image& image, int x0, int y0, int w, int h) const noexcept {
    if (empty() || image.width() != width_ || image.height() != height_)
        return false;
    for (int y = y0; y < y0 + h; ++y) {
        const Rgba8* line = image.row(y).data();
        for (int x = x0; x < x0 + w;) {
            const int tx = x / kTile;
            const int run = std::min(x0 + w, (tx + 1) * kTile) - x;
            const Tile* tile = tiles_[tileIndex(tx, y / kTile)].get();
            if (tile) {
                if (std::memcmp(line + x, &(*tile)[(y % kTile) * kTile + (x % kTile)],
                                static_cast<std::size_t>(run) * sizeof(Rgba8)) != 0)
                    return false;
            } else if (!allZero(line + x, static_cast<std::size_t>(run))) {
                return false;
            }
            x += run;
        }
    }
    return true;
}

void SparseImage::copyRect(int x0, int y0, int w, int h, Rgba8* out) const noexcept {
    for (int y = y0; y < y0 + h; ++y) {
        Rgba8* dst = out + static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(w);
        for (int x = x0; x < x0 + w;) {
            const int tx = x / kTile;
            const int run = std::min(x0 + w, (tx + 1) * kTile) - x;
            const Tile* tile = tiles_[tileIndex(tx, y / kTile)].get();
            if (tile)
                std::memcpy(dst + (x - x0), &(*tile)[(y % kTile) * kTile + (x % kTile)],
                            static_cast<std::size_t>(run) * sizeof(Rgba8));
            else
                std::fill_n(dst + (x - x0), run, Rgba8{});
            x += run;
        }
    }
}

void SparseImage::reserveRect(int x0, int y0, int w, int h) {
    if (empty() || w <= 0 || h <= 0)
        return;
    for (int ty = std::max(y0, 0) / kTile; ty <= std::min(y0 + h - 1, height_ - 1) / kTile; ++ty) {
        for (int tx = std::max(x0, 0) / kTile; tx <= std::min(x0 + w - 1, width_ - 1) / kTile; ++tx) {
            std::unique_ptr<Tile>& slot = tiles_[tileIndex(tx, ty)];
            if (!slot) {
                slot = std::make_unique<Tile>();
                ++stored_;
            }
        }
    }
}

void SparseImage::assignRect(const Image& image, int x0, int y0, int w, int h) {
    if (empty() || image.width() != width_ || image.height() != height_ || w <= 0 || h <= 0)
        return;
    for (int ty = y0 / kTile; ty <= (y0 + h - 1) / kTile; ++ty) {
        for (int tx = x0 / kTile; tx <= (x0 + w - 1) / kTile; ++tx) {
            // The part of this tile inside the rectangle, in image coordinates.
            const int ax = std::max(x0, tx * kTile);
            const int bx = std::min(x0 + w, (tx + 1) * kTile);
            const int ay = std::max(y0, ty * kTile);
            const int by = std::min(y0 + h, (ty + 1) * kTile);
            std::unique_ptr<Tile>& slot = tiles_[tileIndex(tx, ty)];
            if (!slot) {
                // Nothing stored means zero: only a part that has something in it needs a tile.
                bool blank = true;
                for (int y = ay; y < by && blank; ++y)
                    blank = allZero(image.row(y).data() + ax, static_cast<std::size_t>(bx - ax));
                if (blank)
                    continue;
                slot = std::make_unique<Tile>(); // zeroed: the rest of the tile is outside the rectangle
                ++stored_;
            }
            for (int y = ay; y < by; ++y)
                std::memcpy(&(*slot)[(y % kTile) * kTile + (ax % kTile)], image.row(y).data() + ax,
                            static_cast<std::size_t>(bx - ax) * sizeof(Rgba8));
            // A tile that became all zero is given back.
            if (allZero(&(*slot)[0], static_cast<std::size_t>(kTile) * kTile)) {
                slot.reset();
                --stored_;
            }
        }
    }
}

Image SparseImage::toImage() const {
    Image out(width_, height_);
    for (int y = 0; y < height_; ++y) {
        Rgba8* dst = out.row(y).data();
        for (int x = 0; x < width_;) {
            const int tx = x / kTile;
            const int run = std::min(width_, (tx + 1) * kTile) - x;
            if (const Tile* tile = tiles_[tileIndex(tx, y / kTile)].get())
                std::memcpy(dst + x, &(*tile)[(y % kTile) * kTile + (x % kTile)], static_cast<std::size_t>(run) * sizeof(Rgba8));
            x += run;
        }
    }
    return out;
}

} // namespace trace
