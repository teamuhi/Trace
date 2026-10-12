#include "core/selection.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace trace {

namespace {

IRect boundsOf(const Image& img) noexcept {
    return IRect{0, 0, img.width(), img.height()};
}

// Overwrites (does not blend) a rectangle, so a transparent clear color really clears.
void fillReplace(Image& img, const IRect& area, Rgba8 color) noexcept {
    const IRect r = intersectionOf(area, boundsOf(img));
    for (int y = r.y0; y < r.y1; ++y) {
        const std::span<Rgba8> row = img.row(y);
        std::fill(row.begin() + r.x0, row.begin() + r.x1, color);
    }
}

} // namespace

void Selection::clear() noexcept {
    rect_ = IRect{};
    floating_ = false;
    pixels_ = Image();
    base_ = Image();
}

void Selection::setRect(const IRect& rect) noexcept {
    if (floating_)
        return;
    rect_ = rect.empty() ? IRect{} : rect;
}

IRect Selection::lift(Image& image, Rgba8 clearColor) {
    if (floating_ || !active())
        return {};
    const IRect r = intersectionOf(rect_, boundsOf(image));
    if (r.empty())
        return {};

    // Everything that can throw happens before any member or the image is touched.
    Image pixels(r.width(), r.height());
    for (int y = r.y0; y < r.y1; ++y) {
        const std::span<const Rgba8> row = static_cast<const Image&>(image).row(y);
        std::copy(row.begin() + r.x0, row.begin() + r.x1, pixels.row(y - r.y0).begin());
    }
    Image base = image;
    fillReplace(base, r, clearColor);

    fillReplace(image, r, clearColor);
    pixels_ = std::move(pixels);
    base_ = std::move(base);
    rect_ = r;
    floating_ = true;
    return r;
}

void Selection::restoreFromBase(Image& image, const IRect& area) const {
    const IRect r = intersectionOf(area, boundsOf(image));
    for (int y = r.y0; y < r.y1; ++y) {
        const std::span<const Rgba8> from = base_.row(y);
        std::copy(from.begin() + r.x0, from.begin() + r.x1, image.row(y).begin() + r.x0);
    }
}

IRect Selection::blit(Image& image) const {
    const IRect r = intersectionOf(rect_, boundsOf(image));
    for (int y = r.y0; y < r.y1; ++y) {
        const std::span<const Rgba8> src = pixels_.row(y - rect_.y0);
        const std::span<Rgba8> dst = image.row(y);
        for (int x = r.x0; x < r.x1; ++x)
            dst[static_cast<std::size_t>(x)] = blendOver(dst[static_cast<std::size_t>(x)], src[static_cast<std::size_t>(x - rect_.x0)]);
    }
    return r;
}

IRect Selection::moveTo(Image& image, int x, int y) {
    if (!floating_ || base_.width() != image.width() || base_.height() != image.height())
        return {};
    const IRect old = rect_;
    restoreFromBase(image, old);
    rect_ = IRect{x, y, x + pixels_.width(), y + pixels_.height()};
    return unionOf(intersectionOf(old, boundsOf(image)), blit(image));
}

IRect Selection::erase(Image& image, Rgba8 clearColor) {
    if (!active())
        return {};
    if (floating_) {
        if (base_.width() != image.width() || base_.height() != image.height()) {
            clear();
            return {};
        }
        const IRect old = intersectionOf(rect_, boundsOf(image));
        restoreFromBase(image, old);
        clear();
        return old;
    }
    const IRect r = intersectionOf(rect_, boundsOf(image));
    fillReplace(image, r, clearColor);
    return r;
}

Image Selection::copy(const Image& image) const {
    if (!active())
        return {};
    if (floating_)
        return pixels_;

    const IRect r = intersectionOf(rect_, boundsOf(image));
    if (r.empty())
        return {};
    Image out(r.width(), r.height());
    for (int y = r.y0; y < r.y1; ++y) {
        const std::span<const Rgba8> row = image.row(y);
        std::copy(row.begin() + r.x0, row.begin() + r.x1, out.row(y - r.y0).begin());
    }
    return out;
}

IRect Selection::paste(Image& image, Image pixels, int x, int y) {
    clear();
    if (pixels.empty() || image.empty())
        return {};

    Image base = image; // may throw; nothing has changed yet
    pixels_ = std::move(pixels);
    base_ = std::move(base);
    rect_ = IRect{x, y, x + pixels_.width(), y + pixels_.height()};
    floating_ = true;
    return blit(image);
}

} // namespace trace
