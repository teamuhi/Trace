#include "core/view_transform.hpp"

#include <algorithm>

namespace trace {

namespace {

float clampZoom(float zoom) noexcept {
    return std::clamp(zoom, ViewTransform::kMinZoom, ViewTransform::kMaxZoom);
}

} // namespace

void ViewTransform::setDevicePixelRatio(float dpr) noexcept {
    dpr_ = dpr > 0.0f ? dpr : 1.0f;
}

Vec2f ViewTransform::toView(Vec2f image) const noexcept {
    const float s = scale();
    return Vec2f{origin_.x + image.x * s, origin_.y + image.y * s};
}

Vec2f ViewTransform::toImage(Vec2f view) const noexcept {
    const float s = scale();
    return Vec2f{(view.x - origin_.x) / s, (view.y - origin_.y) / s};
}

void ViewTransform::zoomAt(Vec2f anchor, float newZoom) noexcept {
    const Vec2f imagePoint = toImage(anchor);
    zoom_ = clampZoom(newZoom);
    const float s = scale();
    origin_.x = anchor.x - imagePoint.x * s;
    origin_.y = anchor.y - imagePoint.y * s;
}

void ViewTransform::panBy(float dx, float dy) noexcept {
    origin_.x += dx;
    origin_.y += dy;
}

void ViewTransform::fit(Vec2f viewSize, int imageWidth, int imageHeight, float margin) noexcept {
    if (imageWidth <= 0 || imageHeight <= 0)
        return;
    const float availX = std::max(viewSize.x - 2.0f * margin, 1.0f);
    const float availY = std::max(viewSize.y - 2.0f * margin, 1.0f);
    const float unitsPerPixel = std::min(availX / static_cast<float>(imageWidth),
                                         availY / static_cast<float>(imageHeight));
    zoom_ = clampZoom(std::min(unitsPerPixel * dpr_, 1.0f));
    center(viewSize, imageWidth, imageHeight);
}

void ViewTransform::actualSize(Vec2f viewSize, int imageWidth, int imageHeight) noexcept {
    zoom_ = 1.0f;
    center(viewSize, imageWidth, imageHeight);
}

void ViewTransform::center(Vec2f viewSize, int imageWidth, int imageHeight) noexcept {
    const float s = scale();
    origin_.x = (viewSize.x - static_cast<float>(imageWidth) * s) * 0.5f;
    origin_.y = (viewSize.y - static_cast<float>(imageHeight) * s) * 0.5f;
}

} // namespace trace
