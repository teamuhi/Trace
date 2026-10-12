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

    // Largest zoom (clamped, never above 100%) at which the whole image fits with `margin` view units
    // around it, centered in a viewport of `viewSize`.
    void fit(Vec2f viewSize, int imageWidth, int imageHeight, float margin = 16.0f) noexcept;

    // 100% zoom, image centered.
    void actualSize(Vec2f viewSize, int imageWidth, int imageHeight) noexcept;

    void center(Vec2f viewSize, int imageWidth, int imageHeight) noexcept;

private:
    float zoom_ = 1.0f;
    float dpr_ = 1.0f;
    Vec2f origin_;
};

} // namespace trace
