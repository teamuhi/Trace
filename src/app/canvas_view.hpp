#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "app/texture_mirror.hpp"
#include "core/animation.hpp"
#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/guides.hpp"
#include "core/pattern.hpp"
#include "core/view_transform.hpp"

struct ImDrawList;
struct ImVec2;
struct SDL_Renderer;

namespace trace {

class Document;
class Toolbox;
struct ToolSettings;

// Draws a Document inside the current ImGui window, handles zoom and pan, and turns mouse gestures into
// tool presses, drags and releases. Keeps a TextureMirror of the document image current by uploading only the
// dirty rectangle.
class CanvasView {
public:
    CanvasView() = default;
    ~CanvasView() { release(); }

    CanvasView(const CanvasView&) = delete;
    CanvasView& operator=(const CanvasView&) = delete;

    void init(SDL_Renderer* renderer) noexcept {
        mirror_.init(renderer);
        for (OnionGhost& ghost : ghosts_)
            ghost.mirror.init(renderer);
    }

    // Must run before the renderer is destroyed. Safe to call repeatedly.
    void release() noexcept {
        mirror_.release();
        releaseOnion();
    }

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

    // The image rectangle in view (rounded outwards), and the image point under a screen position (nullopt when it
    // is outside the canvas viewport). Both use the view as of the last draw().
    IRect visibleImageRect() const noexcept;
    std::optional<Vec2f> screenToImage(float x, float y) const noexcept;

    // Endless whiteboard: no backdrop, checkerboard or border; transparent pixels show `paper`; the view is never
    // refit on its own, follows the board when it grows to the left or top (so the drawing keeps its place on screen)
    // and the rulers count from the board origin. Fit then frames what is drawn.
    void setBoard(bool endless, Rgba8 paper) noexcept {
        board_ = endless;
        paper_ = paper;
        if (!endless)
            boardTracked_ = false;
    }
    // A hook run on the press that starts a tool gesture, before the tool sees it, with the image point pressed. The
    // whiteboard grows here; the view is realigned afterwards so the press lands on the pixel under the pointer.
    void setBeforePress(std::function<void(Document&, Vec2f)> hook) { beforePress_ = std::move(hook); }

    bool pixelGridEnabled() const noexcept { return showPixelGrid_; }
    void setPixelGridEnabled(bool enabled) noexcept { showPixelGrid_ = enabled; }
    // The zoom (1.0 = 100%) from which the pixel grid and the hover outline show. Paint uses 8 (800%).
    void setGridMinZoom(float zoom) noexcept { gridMinZoom_ = zoom; }

    // Pixel art behaviour: Fit may enlarge a small picture (in whole steps) instead of stopping at 100%, and
    // zoom commands step through whole zoom levels from 100% up so pixels stay evenly sized.
    void setPixelArtView(bool enabled) noexcept { pixelArt_ = enabled; }

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
    // Pixel art only: an item of the symmetry list (see ToolSettings::symmetry) is being dragged.
    bool draggingSymmetryAxis() const noexcept { return symDrag_.active; }

    // Pixel art extras drawn around and behind the image. The tile preview shows the picture 3 x 3 with the
    // neighbours dimmed; onion skin ghosts the neighbouring frames behind the active one. Both only change what is
    // drawn, never the document.
    void setTilePreview(bool on) noexcept { tilePreview_ = on; }
    bool tilePreview() const noexcept { return tilePreview_; }
    void setOnionSkin(bool on) noexcept { onionSkin_ = on; }
    bool onionSkin() const noexcept { return onionSkin_; }
    // How far the tile preview's neighbouring copies fade toward the `tileDim` color, 0..100 percent.
    void setTileDimPercent(int percent) noexcept { tileDimPercent_ = std::clamp(percent, 0, 100); }
    // How many frames before and after the active one are ghosted, how visible the nearest ghost is (percent; farther
    // ones fade), and whether the first and last frames count as neighbours of each other.
    void setOnionOptions(int before, int after, int opacityPercent, bool loop) noexcept {
        onion_ = OnionOptions{std::clamp(before, 0, kMaxOnionGhostsPerSide), std::clamp(after, 0, kMaxOnionGhostsPerSide),
                              std::clamp(opacityPercent, 1, 100), loop};
    }

private:
    static constexpr int kMaxOnionGhostsPerSide = 4;

    struct OnionOptions {
        int before = 1;
        int after = 1;
        int opacityPercent = 40;
        bool loop = false;
        bool operator==(const OnionOptions&) const = default;
    };
    // One ghost frame: its picture on the GPU (made when first needed) and which frame it shows.
    struct OnionGhost {
        TextureMirror mirror;
        OnionFrame frame;
    };
    // What the ghosts were last built from; they are rebuilt only when it differs.
    struct OnionKey {
        const Document* doc = nullptr;
        std::uint64_t revision = 0;
        int activeFrame = -1;
        int frameCount = 0;
        OnionOptions options;
        bool operator==(const OnionKey&) const = default;
    };

    void releaseOnion() noexcept;
    // Brings the ghosts up to date with the document (never while a gesture is drawing: they cannot change then).
    void updateOnion(const Document& doc, bool gestureActive);
    void drawOnion(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, const ImVec2& imgMin, int imgW, int imgH);
    void drawTilePreview(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, const ImVec2& imgMin, int imgW, int imgH,
                         float patternTile);

    void syncTexture(Document& doc);
    // Keeps the drawing still on screen when the board origin moved (it grew, or an undo or redo changed it).
    void followBoardOrigin(const Document& doc) noexcept;
    void fitBoard(Document& doc);
    void handleViewInput(float originX, float originY, bool hovered, bool active);
    void routeGesture(Document& doc, Toolbox& tools, Vec2f mouse, bool hovered, float snapRadius);
    void zoomBy(float factor) noexcept;
    // One zoom step toward (or away from) `anchor`: a factor normally, the next whole level in pixel art.
    void zoomStep(Vec2f anchor, bool in, float factor) noexcept;
    void updateGuides(Document& doc, Vec2f mouse, const ImVec2& p0, const ImVec2& p1, bool hovered,
                      bool topRulerPressed, bool leftRulerPressed);
    void drawRulers(ImDrawList* dl, const ImVec2& origin, const ImVec2& p0, const ImVec2& p1, const ImVec2& imgMin,
                    float thickness, Vec2f mouse, bool showMarker) const;
    void drawGuides(ImDrawList* dl, const Document& doc, const ImVec2& p0, const ImVec2& p1, const ImVec2& imgMin) const;
    // The Text tool's open box: applies or drops it when it can no longer stay open (another tool, a replaced
    // document or layer), draws its preview, and shows the floating text field that takes the typing.
    void keepTextSession(Document& doc, Toolbox& tools);
    void drawTextPreview(ImDrawList* dl, Toolbox& tools, const ImVec2& p0, const ImVec2& p1, const ImVec2& imgMin) const;
    void drawTextEntry(Document& doc, Toolbox& tools, const ImVec2& p0, const ImVec2& p1, const ImVec2& imgMin);
    void updateSymmetryAxes(const Document& doc, Toolbox& tools, Vec2f mouse, bool hovered);
    void drawSymmetryAxes(ImDrawList* dl, const Document& doc, const ToolSettings& settings, const ImVec2& p0,
                          const ImVec2& p1, const ImVec2& imgMin) const;

    TextureMirror mirror_;

    ViewTransform view_;
    Vec2f viewSize_;
    bool fitMode_ = true; // refit on every resize until the user zooms or pans manually
    bool pendingActualSize_ = false;

    // Gesture state: the mouse button that started the current tool gesture (-1 when none).
    int gestureButton_ = -1;
    bool gestureCancelled_ = false;
    Vec2f lastPointer_; // last valid pointer position in view units, used if the pointer leaves the window

    bool showPixelGrid_ = true;
    float gridMinZoom_ = 8.0f;
    bool pixelArt_ = false;
    bool tilePreview_ = false;
    bool onionSkin_ = false;
    int tileDimPercent_ = 50;
    OnionOptions onion_;
    OnionKey onionKey_;
    std::array<OnionGhost, 2 * kMaxOnionGhostsPerSide> ghosts_; // the first onionShown_ entries are drawn
    int onionShown_ = 0;
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

    // Pixel art: the symmetry item under the pointer (-1 for none) and whether it is its mirror line rather than its
    // grip or marker, and the item being dragged. A drag keeps the item's point where it was at the press (`startX`,
    // `startY`, in half pixels) and the pointer's image position then, and moves the point by the pointer's
    // movement: freely for a grip or marker, along the mirror's normal only (`perpendicular`) for the line. `originalX`
    // and `originalY` are the stored values to put back on Escape.
    struct SymmetryDrag {
        bool active = false;
        int index = -1;
        bool perpendicular = false;
        Vec2f start;
        int startX = 0;
        int startY = 0;
        int originalX = 0;
        int originalY = 0;
    };
    SymmetryDrag symDrag_;
    int hoverSymItem_ = -1;
    bool hoverSymLine_ = false;
    bool symmetryHandleHot() const noexcept { return hoverSymItem_ >= 0 || symDrag_.active; }
    // Text tool: the text field's contents (a copy of the tool's text, edited in place by ImGui), the box it was
    // filled for, whether it still has to take the keyboard focus, and its size last frame (to place it).
    std::vector<char> textBuffer_;
    std::uint64_t textSerial_ = 0;
    bool textFocusPending_ = false;
    Vec2f textEntrySize_;
    static constexpr float kBoardMinZoom = 0.02f;
    bool board_ = false;
    Rgba8 paper_{255, 255, 255, 255};
    bool boardTracked_ = false;
    int trackedOriginX_ = 0;
    int trackedOriginY_ = 0;
    bool pendingFitBoard_ = false;
    std::function<void(Document&, Vec2f)> beforePress_;
    Vec2f viewOrigin_;     // screen position of the viewport top-left corner, as of the last draw()
    int rulerOriginX_ = 0; // world position of image pixel (0, 0), for the rulers
    int rulerOriginY_ = 0;
    bool hoverValid_ = false;
    int hoverX_ = 0;
    int hoverY_ = 0;
};

} // namespace trace
