#include "core/image.hpp"

#include <algorithm>

namespace trace {

Image::Image(int width, int height, Rgba8 fill) {
    if (width <= 0 || height <= 0)
        return;
    width_ = width;
    height_ = height;
    pixels_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), fill);
}

Rgba8 Image::get(int x, int y) const noexcept {
    return contains(x, y) ? pixels_[index(x, y)] : Rgba8{};
}

bool Image::set(int x, int y, Rgba8 color) noexcept {
    if (!contains(x, y))
        return false;
    pixels_[index(x, y)] = color;
    return true;
}

void Image::clear(Rgba8 color) noexcept {
    std::fill(pixels_.begin(), pixels_.end(), color);
}

std::span<Rgba8> Image::row(int y) noexcept {
    if (y < 0 || y >= height_)
        return {};
    return std::span<Rgba8>(pixels_).subspan(index(0, y), static_cast<std::size_t>(width_));
}

std::span<const Rgba8> Image::row(int y) const noexcept {
    if (y < 0 || y >= height_)
        return {};
    return std::span<const Rgba8>(pixels_).subspan(index(0, y), static_cast<std::size_t>(width_));
}

} // namespace trace
