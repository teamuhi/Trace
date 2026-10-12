#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/color.hpp"

namespace trace {

class Image {
public:
    Image() = default;
    Image(int width, int height, Rgba8 fill = Rgba8{});

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    bool empty() const noexcept { return pixels_.empty(); }

    bool contains(int x, int y) const noexcept { return x >= 0 && y >= 0 && x < width_ && y < height_; }

    // Out-of-bounds reads return transparent black; out-of-bounds writes are ignored and return false.
    Rgba8 get(int x, int y) const noexcept;
    bool set(int x, int y, Rgba8 color) noexcept;
    void clear(Rgba8 color = Rgba8{}) noexcept;

    std::span<Rgba8> pixels() noexcept { return pixels_; }
    std::span<const Rgba8> pixels() const noexcept { return pixels_; }

    // Empty span when y is out of range.
    std::span<Rgba8> row(int y) noexcept;
    std::span<const Rgba8> row(int y) const noexcept;

private:
    std::size_t index(int x, int y) const noexcept {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<Rgba8> pixels_;
};

} // namespace trace
