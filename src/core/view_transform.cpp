#include "core/view_transform.hpp"

#include <algorithm>
#include <cmath>

namespace trace {

float ViewTransform::clampZoom(float zoom) const noexcept {
    return std::clamp(zoom, minZoom_, maxZoom_);
}

bool ViewTransform::setZoomLimits(float minZoom, float maxZoom) noexcept {
    if (!std::isfinite(minZoom) || !std::isfinite(maxZoom) || !(minZoom > 0.0f) || minZoom > maxZoom)
        return false;
    minZoom_ = minZoom;
    maxZoom_ = maxZoom;
    zoom_ = clampZoom(zoom_);
    return true;
}

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

void ViewTransform::fit(Vec2f viewSize, int imageWidth, int imageHeight, float margin, float maxFitZoom,
                        bool wholeSteps) noexcept {
    if (imageWidth <= 0 || imageHeight <= 0)
        return;
    const float availX = std::max(viewSize.x - 2.0f * margin, 1.0f);
    const float availY = std::max(viewSize.y - 2.0f * margin, 1.0f);
    const float unitsPerPixel = std::min(availX / static_cast<float>(imageWidth),
                                         availY / static_cast<float>(imageHeight));
    float zoom = std::min(unitsPerPixel * dpr_, std::isfinite(maxFitZoom) && maxFitZoom > 0.0f ? maxFitZoom : 1.0f);
    if (wholeSteps && zoom >= 1.0f)
        zoom = std::floor(zoom);
    zoom_ = clampZoom(zoom);
    center(viewSize, imageWidth, imageHeight);
}

void ViewTransform::fitRect(Vec2f viewSize, float x0, float y0, float x1, float y1, float margin) noexcept {
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1))
        return;
    const float centerX = (x0 + x1) * 0.5f;
    const float centerY = (y0 + y1) * 0.5f;
    const float w = x1 - x0;
    const float h = y1 - y0;
    if (w > 0.0f || h > 0.0f) {
        const float availX = std::max(viewSize.x - 2.0f * margin, 1.0f);
        const float availY = std::max(viewSize.y - 2.0f * margin, 1.0f);
        // A zero extent on one axis imposes no limit on that axis.
        const float unitsX = w > 0.0f ? availX / w : availX;
        const float unitsY = h > 0.0f ? availY / h : availY;
        zoom_ = clampZoom(std::min(std::min(unitsX, unitsY) * dpr_, 1.0f));
    } else {
        zoom_ = clampZoom(1.0f);
    }
    const float s = scale();
    origin_.x = viewSize.x * 0.5f - centerX * s;
    origin_.y = viewSize.y * 0.5f - centerY * s;
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
