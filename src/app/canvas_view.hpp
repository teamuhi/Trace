#pragma once

#include <optional>
#include <utility>

#include "core/guides.hpp"
#include "core/pattern.hpp"
#include "core/view_transform.hpp"

struct ImDrawList;
struct ImVec2;
struct SDL_Renderer;
struct SDL_Texture;

namespace trace {

class Document;
class Toolbox;

// Draws a Document inside the current ImGui window, handles zoom and pan, and turns mouse gestures into
// tool presses, drags and releases. Owns the GPU texture that mirrors the document image and keeps it
// current by uploading only the dirty rectangle.
class CanvasView {
public:
    CanvasView() = default;
    ~CanvasView() { release(); }

    CanvasView(const CanvasView&) = delete;
    CanvasView& operator=(const CanvasView&) = delete;

    void init(SDL_Renderer* renderer) noexcept { renderer_ = renderer; }

    // Must run before the renderer is destroyed. Safe to call repeatedly.
    void release() noexcept;

    // Fills the current window's content region with the canvas.
    void draw(Document& doc, Toolbox& tools);

    void zoomIn() noexcept;
    void zoomOut() noexcept;
    void fitToWindow() noexcept;
    void actualSize() noexcept;

    float zoomPercent() const noexcept { return view_.zoom() * 100.0f; }

    bool hasHoverPixel() const noexcept { return hoverValid_; }
    int hoverX() const noexcept { return hoverX_; }
    int hoverY() const noexcept { return hoverY_; }

    // The image pixel under the top-left corner of the viewport (may be negative or past the image).
    Vec2f visibleImageOrigin() const noexcept { return view_.toImage(Vec2f{0.0f, 0.0f}); }

    bool pixelGridEnabled() const noexcept { return showPixelGrid_; }
    void setPixelGridEnabled(bool enabled) noexcept { showPixelGrid_ = enabled; }

    // Patterns behind the image (backdrop) and behind transparent pixels, with their spacing in device pixels.
    void setPatterns(PatternStyle backdrop, PatternStyle transparency, int spacing) noexcept {
        backdropPattern_ = backdrop;
        transparencyPattern_ = transparency;
        patternSpacing_ = spacing;
    }

    // Rulers along the top and left edges, the guides drawn over the image, and whether shapes and
    // selections snap to guides. Dragging from a ruler makes a guide; dragging a guide back onto a ruler
    // (or off the image) deletes it.
    void setRulersEnabled(bool enabled) noexcept { showRulers_ = enabled; }
    void setGuidesVisible(bool visible) noexcept { showGuides_ = visible; }
    void setSnapToGuides(bool enabled) noexcept { snapToGuides_ = enabled; }
    bool draggingGuide() const noexcept { return guideDrag_.active; }

private:
    void syncTexture(Document& doc);
    void handleViewInput(float originX, float originY, bool hovered, bool active);
    void routeGesture(Document& doc, Toolbox& tools, Vec2f mouse, bool hovered, float snapRadius);
    void zoomBy(float factor) noexcept;
    void updateGuides(Document& doc, Vec2f mouse, const ImVec2& p0, const ImVec2& p1, bool hovered,
                      bool topRulerPressed, bool leftRulerPressed);
    void drawRulers(ImDrawList* dl, const ImVec2& origin, const ImVec2& p0, const ImVec2& p1, const ImVec2& imgMin,
                    float thickness, Vec2f mouse, bool showMarker) const;
    void drawGuides(ImDrawList* dl, const Document& doc, const ImVec2& p0, const ImVec2& p1, const ImVec2& imgMin) const;

    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int textureWidth_ = 0;
    int textureHeight_ = 0;
    bool textureNearest_ = true;

    ViewTransform view_;
    Vec2f viewSize_;
    bool fitMode_ = true; // refit on every resize until the user zooms or pans manually
    bool pendingActualSize_ = false;

    // Gesture state: the mouse button that started the current tool gesture (-1 when none).
    int gestureButton_ = -1;
    bool gestureCancelled_ = false;
    Vec2f lastPointer_; // last valid pointer position in view units, used if the pointer leaves the window

    bool showPixelGrid_ = true;
    PatternStyle backdropPattern_ = PatternStyle::Solid;
    PatternStyle transparencyPattern_ = PatternStyle::Checker;
    int patternSpacing_ = kDefaultPatternSpacing;
    bool showRulers_ = true;
    bool showGuides_ = true;
    bool snapToGuides_ = true;

    // A guide being dragged out of a ruler or picked up off the canvas. While it is held it is not in the
    // document; releasing it over the image puts it back, anywhere else deletes it.
    struct GuideDrag {
        bool active = false;
        GuideAxis axis = GuideAxis::Vertical;
        int position = 0;
        std::optional<int> original; // where it came from, when it was picked up
    };
    GuideDrag guideDrag_;
    std::optional<std::pair<GuideAxis, int>> hoverGuide_;
    bool hoverValid_ = false;
    int hoverX_ = 0;
    int hoverY_ = 0;
};

} // namespace trace
