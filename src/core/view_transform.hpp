#pragma once

#include "core/geometry.hpp"

namespace trace {

// Maps between image pixels and view units (the coordinate space of the canvas viewport).
//
// Zoom is expressed in device pixels per image pixel, so 1.0 is a true 100% regardless of display
// scaling; `setDevicePixelRatio` converts that to view units.
class ViewTransform {
public:
    static constexpr float kMinZoom = 0.1f;
    static constexpr float kMaxZoom = 32.0f;

    float zoom() const noexcept { return zoom_; }
    float minZoom() const noexcept { return minZoom_; }
    float maxZoom() const noexcept { return maxZoom_; }

    // Narrows or widens the zoom range (the flowchart board uses 0.05 to 8). Non-finite or non-positive
    // limits, and a minimum above the maximum, are refused and change nothing. The current zoom is clamped
    // into the new range, keeping the origin where it is.
    bool setZoomLimits(float minZoom, float maxZoom) noexcept;
    float devicePixelRatio() const noexcept { return dpr_; }
    void setDevicePixelRatio(float dpr) noexcept;

    // View units per image pixel.
    float scale() const noexcept { return zoom_ / dpr_; }

    // Position of image pixel (0, 0) in view units.
    Vec2f origin() const noexcept { return origin_; }

    Vec2f toView(Vec2f image) const noexcept;
    Vec2f toImage(Vec2f view) const noexcept;

    // Sets the zoom (clamped) while keeping the image point under `anchor` fixed.
    void zoomAt(Vec2f anchor, float newZoom) noexcept;
    void zoomByFactorAt(Vec2f anchor, float factor) noexcept { zoomAt(anchor, zoom_ * factor); }

    void panBy(float dx, float dy) noexcept;

    // Largest zoom (clamped, by default never above 100%) at which the whole image fits with `margin` view
    // units around it, centered in a viewport of `viewSize`. `maxFitZoom` raises that ceiling, so a small
    // picture can be enlarged to fill the view (pixel art); `wholeSteps` then rounds a zoom of 100% or more
    // down to a whole number, so every image pixel is the same number of device pixels wide.
    void fit(Vec2f viewSize, int imageWidth, int imageHeight, float margin = 16.0f, float maxFitZoom = 1.0f,
             bool wholeSteps = false) noexcept;

    // Like fit(), for a content box given as floats in image units: largest zoom (clamped, never above 100%)
    // at which the box [x0, x1] x [y0, y1] fits with `margin` view units around it, with its center at the
    // center of the viewport. A box with a non-finite corner is ignored; an empty or inverted box is treated
    // as the point at its center (100% zoom, centered).
    void fitRect(Vec2f viewSize, float x0, float y0, float x1, float y1, float margin = 16.0f) noexcept;

    // 100% zoom, image centered.
    void actualSize(Vec2f viewSize, int imageWidth, int imageHeight) noexcept;

    void center(Vec2f viewSize, int imageWidth, int imageHeight) noexcept;

private:
    float clampZoom(float zoom) const noexcept;

    float minZoom_ = kMinZoom;
    float maxZoom_ = kMaxZoom;
    float zoom_ = 1.0f;
    float dpr_ = 1.0f;
    Vec2f origin_;
};

} // namespace trace
