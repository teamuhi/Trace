#include "app/canvas_view.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <utility>

#include "app/pattern_draw.hpp"
#include "app/sampled_image.hpp"
#include "app/symmetry_panel.hpp"
#include "app/theme.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "core/raster.hpp"
#include "core/text_raster.hpp"
#include "tools/text_tool.hpp"
#include "tools/toolbox.hpp"

namespace trace {

namespace {

constexpr float kZoomStep = 1.25f;          // menu and keyboard zoom
constexpr float kZoomPerWheelNotch = 1.15f; // Ctrl + wheel
constexpr float kFitMargin = 24.0f;
constexpr float kMinRingRadius = 3.0f; // smaller rings are just a dot; the pointer itself is enough
constexpr float kRulerThicknessEm = 1.5f;  // ruler width as a multiple of the font size
constexpr float kRulerLabelEm = 5.5f;      // least distance between two ruler labels
constexpr float kRulerMinMinorTick = 4.0f; // minor ticks closer than this (view units) are left out
constexpr float kGuideGrab = 5.0f;         // how close the pointer must be to pick up a guide (view units)
constexpr float kGuideSnap = 6.0f;         // how close a shape or selection edge must be to snap (view units)
constexpr float kAxisGrab = 4.0f;          // how close the pointer must be to take a mirror axis (view units)
constexpr int kMaxTextPreviewRects = 40000; // the typed text's preview is cut off past this many rectangles
constexpr int kRulerSteps[] = {1,   2,   5,    10,   20,   50,   100,  200,
                               500, 1000, 2000, 5000, 10000, 20000};

ImU32 color(const ImVec4& c) {
    return ImGui::ColorConvertFloat4ToU32(c);
}

// A solid outline with dashes of a second color over it, so it reads on any pixels. Static (not animated)
// so the app can still sleep when idle. The dash count per edge is capped for very long edges.
void dashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 base, ImU32 dash, float dashLength) {
    dl->AddRect(a, b, base);
    const auto edge = [&](ImVec2 p, ImVec2 q) {
        const float len = std::hypot(q.x - p.x, q.y - p.y);
        if (!(len > 0.0f))
            return;
        const float step = std::max(2.0f * dashLength, len / 1000.0f);
        const float dx = (q.x - p.x) / len;
        const float dy = (q.y - p.y) / len;
        for (float t = 0.0f; t < len; t += step) {
            const float e = std::min(t + step * 0.5f, len);
            dl->AddLine(ImVec2(p.x + dx * t, p.y + dy * t), ImVec2(p.x + dx * e, p.y + dy * e), dash);
        }
    };
    edge(a, ImVec2(b.x, a.y));
    edge(ImVec2(b.x, a.y), b);
    edge(b, ImVec2(a.x, b.y));
    edge(ImVec2(a.x, b.y), a);
}

// Image coordinate (a guide position) nearest to a view-space value, or nothing when it is not a number.
std::optional<int> toBoundary(float v) {
    if (!std::isfinite(v))
        return std::nullopt;
    constexpr float limit = 1.0e6f;
    return static_cast<int>(std::lround(std::clamp(v, -limit, limit)));
}

// A straight dashed line (horizontal or vertical). The dash count is capped for very long lines.
void dashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thickness, float dash, float gap) {
    const float len = std::hypot(b.x - a.x, b.y - a.y);
    if (!(len > 0.0f))
        return;
    const float step = std::max(dash + gap, len / 500.0f);
    const float dx = (b.x - a.x) / len;
    const float dy = (b.y - a.y) / len;
    for (float t = 0.0f; t < len; t += step) {
        const float e = std::min(t + std::max(step - gap, 1.0f), len);
        dl->AddLine(ImVec2(a.x + dx * t, a.y + dy * t), ImVec2(a.x + dx * e, a.y + dy * e), col, thickness);
    }
}

// "32" or "31.5": a mirror axis position in half pixels, as image pixels.
std::string axisText(int halfPixels) {
    return std::to_string(halfPixels / 2) + (halfPixels % 2 != 0 ? ".5" : "");
}


// Where the parts of one symmetry item are on screen. The space is the caller's (view units, or the window's screen
// space), given by where the image's top-left corner `imgMin` is in it.
struct SymmetryGeometry {
    bool shown = false; // a mirror: some of its line is in view; a rotation: always
    Vec2f p;            // the item's point (the rotation center)
    Vec2f a;            // a mirror: the visible part of its line, from a to b
    Vec2f b;
    Vec2f grip;         // where the item is grabbed: the rotation center, or the grip on the mirror line
};

// The range [t0, t1] of the parameter for which p + t * d lies inside the rectangle lo..hi, or nothing.
std::optional<std::pair<float, float>> clipLine(Vec2f p, Vec2f d, Vec2f lo, Vec2f hi) {
    if (!(hi.x > lo.x && hi.y > lo.y))
        return std::nullopt;
    float t0 = -1.0e30f;
    float t1 = 1.0e30f;
    const float ps[2] = {p.x, p.y};
    const float ds[2] = {d.x, d.y};
    const float los[2] = {lo.x, lo.y};
    const float his[2] = {hi.x, hi.y};
    for (int i = 0; i < 2; ++i) {
        if (std::fabs(ds[i]) < 1.0e-6f) {
            if (ps[i] < los[i] || ps[i] > his[i])
                return std::nullopt;
            continue;
        }
        float a = (los[i] - ps[i]) / ds[i];
        float b = (his[i] - ps[i]) / ds[i];
        if (a > b)
            std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
    }
    if (!(t1 >= t0) || t1 - t0 > 1.0e29f)
        return std::nullopt;
    return std::make_pair(t0, t1);
}

float distanceToSegment(Vec2f p, Vec2f a, Vec2f b) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float len2 = abx * abx + aby * aby;
    const float t = len2 > 0.0f ? std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / len2, 0.0f, 1.0f) : 0.0f;
    return std::hypot(p.x - (a.x + abx * t), p.y - (a.y + aby * t));
}

// A mirror's line is perpendicular to its flip direction (cos angle, sin angle): angle 0 is a vertical line. The line
// is clipped to the visible part of the image (`lo`..`hi`) and its grip sits at the item's point, or at the nearest
// point of the visible line when that point is out of view.
SymmetryGeometry symmetryGeometry(const SymmetryItem& it, int imgW, int imgH, float scale, Vec2f imgMin, Vec2f lo,
                                  Vec2f hi) {
    SymmetryGeometry g;
    g.p = Vec2f{imgMin.x + static_cast<float>(resolveAxis(it.posX, imgW)) * 0.5f * scale,
                imgMin.y + static_cast<float>(resolveAxis(it.posY, imgH)) * 0.5f * scale};
    if (it.kind == SymmetryKind::Rotate) {
        g.p = Vec2f{std::round(g.p.x), std::round(g.p.y)};
        g.shown = true;
        g.grip = g.p;
        return g;
    }
    const double angle = static_cast<double>(normalizeMirrorAngle(it.angle)) * 3.14159265358979323846 / 180.0;
    Vec2f d{-static_cast<float>(std::sin(angle)), static_cast<float>(std::cos(angle))};
    if (std::fabs(d.x) < 1.0e-6f)
        d.x = 0.0f;
    if (std::fabs(d.y) < 1.0e-6f)
        d.y = 0.0f;
    if (d.x == 0.0f) // axis-aligned lines sit on whole pixels so they stay crisp
        g.p.x = std::round(g.p.x);
    if (d.y == 0.0f)
        g.p.y = std::round(g.p.y);
    const auto range = clipLine(g.p, d, lo, hi);
    if (!range)
        return g;
    g.shown = true;
    g.a = Vec2f{g.p.x + d.x * range->first, g.p.y + d.y * range->first};
    g.b = Vec2f{g.p.x + d.x * range->second, g.p.y + d.y * range->second};
    const float along = std::clamp(0.0f, range->first, range->second);
    g.grip = Vec2f{g.p.x + d.x * along, g.p.y + d.y * along};
    return g;
}

// Draws the part of an image placed at `cellMin`..`cellMax` (screen space) that lies inside `clipMin`..`clipMax`. Only
// the visible slice is drawn; huge off-screen quads lose float precision at high zoom.
void drawTextureSlice(ImDrawList* dl, SDL_Texture* texture, const ImVec2& cellMin, const ImVec2& cellMax,
                      const ImVec2& clipMin, const ImVec2& clipMax, bool nearest, ImU32 tint) {
    const float w = cellMax.x - cellMin.x;
    const float h = cellMax.y - cellMin.y;
    if (!(w > 0.0f && h > 0.0f))
        return;
    const ImVec2 a(std::max(cellMin.x, clipMin.x), std::max(cellMin.y, clipMin.y));
    const ImVec2 b(std::min(cellMax.x, clipMax.x), std::min(cellMax.y, clipMax.y));
    if (!(b.x > a.x && b.y > a.y))
        return;
    const ImVec2 uv0((a.x - cellMin.x) / w, (a.y - cellMin.y) / h);
    const ImVec2 uv1((b.x - cellMin.x) / w, (b.y - cellMin.y) / h);
    addImageSampled(dl, (ImTextureID)(std::intptr_t)texture, a, b, uv0, uv1, nearest, tint);
}

} // namespace

void CanvasView::zoomStep(Vec2f anchor, bool in, float factor) noexcept {
    if (!pixelArt_) {
        view_.zoomByFactorAt(anchor, in ? factor : 1.0f / factor);
        return;
    }
    // Pixel art: below 100% the usual factor, from 100% up the next whole level (so 300% goes to 400%, never
    // to 375%), and never past 100% on the way in from below.
    const float z = view_.zoom();
    float next;
    if (in)
        next = z < 1.0f ? std::min(z * factor, 1.0f) : std::floor(z + 0.001f) + 1.0f;
    else
        next = z > 1.0f ? std::max(std::ceil(z - 0.001f) - 1.0f, 1.0f) : z / factor;
    view_.zoomAt(anchor, next);
}

void CanvasView::zoomIn() noexcept {
    zoomStep(Vec2f{viewSize_.x * 0.5f, viewSize_.y * 0.5f}, true, kZoomStep);
    fitMode_ = false;
}

void CanvasView::zoomOut() noexcept {
    zoomStep(Vec2f{viewSize_.x * 0.5f, viewSize_.y * 0.5f}, false, kZoomStep);
    fitMode_ = false;
}

void CanvasView::fitToWindow() noexcept {
    if (board_) {
        pendingFitBoard_ = true;
        return;
    }
    fitMode_ = true;
}

void CanvasView::actualSize() noexcept {
    pendingActualSize_ = true; // applied in draw(), where the document size is known
    fitMode_ = false;
}

void CanvasView::syncTexture(Document& doc) {
    // What is shown, not the layer being edited: for a single full-opacity layer it is the same image.
    const Image& img = doc.displayImage();
    if (img.empty())
        return;
    if (mirror_.sync(img, doc.takeDirty()) == TextureMirror::Sync::Recreated && !board_)
        fitMode_ = true; // a new size: show all of it (a board that grew keeps its view)
}

void CanvasView::followBoardOrigin(const Document& doc) noexcept {
    if (!board_)
        return;
    if (boardTracked_ && (doc.originX() != trackedOriginX_ || doc.originY() != trackedOriginY_)) {
        const float s = view_.scale();
        view_.panBy(static_cast<float>(doc.originX() - trackedOriginX_) * s,
                    static_cast<float>(doc.originY() - trackedOriginY_) * s);
    }
    boardTracked_ = true;
    trackedOriginX_ = doc.originX();
    trackedOriginY_ = doc.originY();
}

// Fit for a board: frames what is drawn; an empty board is shown at 100%, centered.
void CanvasView::fitBoard(Document& doc) {
    const Image& img = doc.displayImage();
    const IRect bounds = opaqueBounds(img);
    if (bounds.empty()) {
        view_.actualSize(viewSize_, img.width(), img.height());
        return;
    }
    view_.fitRect(viewSize_, static_cast<float>(bounds.x0), static_cast<float>(bounds.y0),
                  static_cast<float>(bounds.x1), static_cast<float>(bounds.y1), kFitMargin);
}

IRect CanvasView::visibleImageRect() const noexcept {
    // Panning is not bounded, so keep the numbers where converting them to int is defined.
    static constexpr float kLimit = 1.0e7f;
    const auto whole = [](float v, bool up) {
        const float c = std::clamp(std::isfinite(v) ? v : 0.0f, -kLimit, kLimit);
        return static_cast<int>(up ? std::ceil(c) : std::floor(c));
    };
    const Vec2f a = view_.toImage(Vec2f{0.0f, 0.0f});
    const Vec2f b = view_.toImage(viewSize_);
    return IRect{whole(a.x, false), whole(a.y, false), whole(b.x, true), whole(b.y, true)};
}

std::optional<Vec2f> CanvasView::screenToImage(float x, float y) const noexcept {
    const float lx = x - viewOrigin_.x;
    const float ly = y - viewOrigin_.y;
    if (lx < 0.0f || ly < 0.0f || lx >= viewSize_.x || ly >= viewSize_.y)
        return std::nullopt;
    return view_.toImage(Vec2f{lx, ly});
}

void CanvasView::releaseOnion() noexcept {
    for (OnionGhost& ghost : ghosts_)
        ghost.mirror.release();
    onionShown_ = 0;
    onionKey_ = OnionKey{};
}

// The ghosts are composites of other frames, so they cannot change while a tool draws on the active one: nothing is
// done during a gesture, and they are rebuilt once when it ends. The same goes for any other change (the document's
// revision covers edits, undo, and frame or layer changes). A composite is a full render, so it is never made per frame.
void CanvasView::updateOnion(const Document& doc, bool gestureActive) {
    const bool wanted = pixelArt_ && onionSkin_ && doc.frameCount() > 1 && (onion_.before > 0 || onion_.after > 0);
    if (!wanted) {
        if (onionShown_ > 0 || onionKey_.doc)
            releaseOnion(); // give the pictures back
        return;
    }
    if (gestureActive)
        return;
    const OnionKey key{&doc, doc.revision(), doc.activeFrame(), doc.frameCount(), onion_};
    if (key == onionKey_)
        return;
    onionKey_ = key;

    int used = 0;
    for (const OnionFrame& frame : onionFrames(doc.activeFrame(), doc.frameCount(), onion_.before, onion_.after, onion_.loop)) {
        if (used >= static_cast<int>(ghosts_.size()))
            break;
        Image picture;
        try {
            picture = doc.compositeOfFrame(frame.frame);
        } catch (const std::bad_alloc&) {
            continue; // out of memory: this frame goes without a ghost
        }
        if (picture.empty())
            continue;
        OnionGhost& ghost = ghosts_[static_cast<std::size_t>(used)];
        ghost.mirror.sync(picture, IRect{0, 0, picture.width(), picture.height()});
        if (!ghost.mirror.texture())
            continue;
        ghost.frame = frame;
        ++used;
    }
    onionShown_ = used;
    for (std::size_t i = static_cast<std::size_t>(used); i < ghosts_.size(); ++i)
        ghosts_[i].mirror.release();
}

// The ghosts, farthest first so the nearer ones lie over them, each multiplied by its tint: the onionPrev or onionNext
// color, fainter the farther the frame is. The active frame is drawn over them afterwards.
void CanvasView::drawOnion(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, const ImVec2& imgMin, int imgW, int imgH) {
    const ThemeTokens& t = tokens();
    const float s = view_.scale();
    const ImVec2 imgMax(imgMin.x + static_cast<float>(imgW) * s, imgMin.y + static_cast<float>(imgH) * s);
    const bool nearest = view_.zoom() >= 1.0f;
    for (int i = onionShown_ - 1; i >= 0; --i) {
        OnionGhost& ghost = ghosts_[static_cast<std::size_t>(i)];
        SDL_Texture* texture = ghost.mirror.texture();
        if (!texture)
            continue;
        ghost.mirror.setNearest(nearest);
        ImVec4 tint = ghost.frame.previous ? t.onionPrev : t.onionNext;
        const float fade = std::max(0.2f, 1.0f - 0.25f * static_cast<float>(ghost.frame.distance - 1));
        tint.w = std::clamp(tint.w * static_cast<float>(onion_.opacityPercent) / 100.0f * fade, 0.0f, 1.0f);
        drawTextureSlice(dl, texture, imgMin, imgMax, p0, p1, nearest, color(tint));
    }
}

// The picture 3 x 3: the transparency pattern under all nine copies, the eight neighbours of the center from the same
// texture, and over them the tileDim color at the settings' strength. The center is drawn by the caller, on top.
void CanvasView::drawTilePreview(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, const ImVec2& imgMin, int imgW,
                                 int imgH, float patternTile) {
    SDL_Texture* texture = mirror_.texture();
    if (!texture)
        return;
    const ThemeTokens& t = tokens();
    const float s = view_.scale();
    const float w = static_cast<float>(imgW) * s;
    const float h = static_cast<float>(imgH) * s;
    const ImVec2 tileMin(imgMin.x - w, imgMin.y - h);
    const ImVec2 tileMax(imgMin.x + 2.0f * w, imgMin.y + 2.0f * h);
    const ImVec2 visMin(std::max(tileMin.x, p0.x), std::max(tileMin.y, p0.y));
    const ImVec2 visMax(std::min(tileMax.x, p1.x), std::min(tileMax.y, p1.y));
    if (!(visMax.x > visMin.x && visMax.y > visMin.y))
        return;
    drawPattern(dl, transparencyPattern_, visMin, visMax, imgMin, patternTile, color(t.checkerA), color(t.checkerB),
                color(t.checkerB));

    const bool nearest = view_.zoom() >= 1.0f;
    mirror_.setNearest(nearest);
    for (int cy = -1; cy <= 1; ++cy) {
        for (int cx = -1; cx <= 1; ++cx) {
            if (cx == 0 && cy == 0)
                continue;
            const ImVec2 cellMin(imgMin.x + static_cast<float>(cx) * w, imgMin.y + static_cast<float>(cy) * h);
            drawTextureSlice(dl, texture, cellMin, ImVec2(cellMin.x + w, cellMin.y + h), p0, p1, nearest, IM_COL32_WHITE);
        }
    }

    if (tileDimPercent_ <= 0)
        return;
    ImVec4 dim = t.tileDim;
    dim.w = std::clamp(dim.w * static_cast<float>(tileDimPercent_) / 100.0f, 0.0f, 1.0f);
    const ImU32 dimColor = color(dim);
    // Four strips around the center, which together are exactly the eight neighbours.
    const auto strip = [&](float x0, float y0, float x1, float y1) {
        const ImVec2 a(std::max(x0, p0.x), std::max(y0, p0.y));
        const ImVec2 b(std::min(x1, p1.x), std::min(y1, p1.y));
        if (b.x > a.x && b.y > a.y)
            dl->AddRectFilled(a, b, dimColor);
    };
    strip(tileMin.x, tileMin.y, tileMax.x, imgMin.y);
    strip(tileMin.x, imgMin.y + h, tileMax.x, tileMax.y);
    strip(tileMin.x, imgMin.y, imgMin.x, imgMin.y + h);
    strip(imgMin.x + w, imgMin.y, tileMax.x, imgMin.y + h);
}

void CanvasView::handleViewInput(float originX, float originY, bool hovered, bool active) {
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2f mouse{io.MousePos.x - originX, io.MousePos.y - originY};

    if (hovered && io.MouseWheel != 0.0f && io.KeyCtrl) {
        if (pixelArt_)
            zoomStep(mouse, io.MouseWheel > 0.0f, kZoomPerWheelNotch);
        else
            view_.zoomByFactorAt(mouse, std::pow(kZoomPerWheelNotch, io.MouseWheel));
        fitMode_ = false;
    } else if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) {
        const float step = ImGui::GetFontSize() * 3.0f;
        float dx = io.MouseWheelH;
        float dy = io.MouseWheel;
        if (io.KeyShift)
            std::swap(dx, dy);
        view_.panBy(dx * step, dy * step);
        fitMode_ = false;
    }

    // Space + left drag pans only between gestures; mid-gesture the left button belongs to the tool.
    const bool spacePan = ImGui::IsKeyDown(ImGuiKey_Space) && gestureButton_ < 0;
    const bool panning = active && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                                    (spacePan && ImGui::IsMouseDown(ImGuiMouseButton_Left)));
    if (panning) {
        view_.panBy(io.MouseDelta.x, io.MouseDelta.y);
        fitMode_ = false;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else if (hovered && spacePan) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
}

// Left or right button down on the canvas starts a gesture; it runs until that button is released (or
// Escape cancels it), even if the pointer leaves the canvas.
void CanvasView::routeGesture(Document& doc, Toolbox& tools, Vec2f mouse, bool hovered, float snapRadius) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool mouseValid = ImGui::IsMousePosValid(&io.MousePos);
    if (mouseValid)
        lastPointer_ = mouse;

    const auto makeEvent = [&](bool secondary) {
        PointerEvent ev;
        ev.pos = view_.toImage(lastPointer_);
        ev.shift = io.KeyShift;
        ev.secondary = secondary;
        ev.snapRadius = snapRadius;
        return ev;
    };

    if (gestureButton_ < 0) {
        const bool panIntent = ImGui::IsKeyDown(ImGuiKey_Space) || io.MouseDown[ImGuiMouseButton_Middle];
        // A guide being held or under the pointer takes the click before the tool does.
        if (!hovered || !mouseValid || panIntent || guideDrag_.active || hoverGuide_ || symmetryHandleHot())
            return;
        for (const int button : {ImGuiMouseButton_Left, ImGuiMouseButton_Right}) {
            if (ImGui::IsMouseClicked(button)) {
                gestureButton_ = button;
                gestureCancelled_ = false;
                if (beforePress_) {
                    beforePress_(doc, view_.toImage(lastPointer_));
                    followBoardOrigin(doc);
                }
                tools.press(doc, makeEvent(button == ImGuiMouseButton_Right));
                return;
            }
        }
        return;
    }

    const bool secondary = gestureButton_ == ImGuiMouseButton_Right;
    const bool held = ImGui::IsMouseDown(gestureButton_);

    if (!gestureCancelled_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        tools.cancel(doc);
        gestureCancelled_ = true;
    }

    // Cancelled, or the toolbox dropped the gesture (the document was replaced): wait for the button to
    // come up so the same press cannot start a new gesture.
    if (gestureCancelled_ || !tools.gestureActive()) {
        if (!held) {
            gestureButton_ = -1;
            gestureCancelled_ = false;
        }
        return;
    }

    if (held) {
        if (mouseValid)
            tools.drag(doc, makeEvent(secondary));
    } else {
        tools.release(doc, makeEvent(secondary));
        gestureButton_ = -1;
    }
}

void CanvasView::draw(Document& doc, Toolbox& tools) {
    followBoardOrigin(doc);
    syncTexture(doc);

    // The rulers take a strip along the top and left of the window; everything else (the viewport) is
    // laid out as if it started below and to the right of them.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 full = ImGui::GetContentRegionAvail();
    const float rulerSize = showRulers_ ? std::round(ImGui::GetFontSize() * kRulerThicknessEm) : 0.0f;
    const ImVec2 avail(full.x - rulerSize, full.y - rulerSize);
    if (avail.x < 1.0f || avail.y < 1.0f) {
        symDrag_ = SymmetryDrag{}; // no room to finish a drag in
        hoverSymItem_ = -1;
        return;
    }

    const ImVec2 p0(origin.x + rulerSize, origin.y + rulerSize);
    const ImVec2 p1(p0.x + avail.x, p0.y + avail.y);

    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##canvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    bool topRulerPressed = false;
    bool leftRulerPressed = false;
    if (showRulers_) {
        ImGui::SetCursorScreenPos(ImVec2(p0.x, origin.y));
        ImGui::InvisibleButton("##rulerTop", ImVec2(avail.x, rulerSize), ImGuiButtonFlags_MouseButtonLeft);
        topRulerPressed = ImGui::IsItemActivated();
        ImGui::SetCursorScreenPos(ImVec2(origin.x, p0.y));
        ImGui::InvisibleButton("##rulerLeft", ImVec2(rulerSize, avail.y), ImGuiButtonFlags_MouseButtonLeft);
        leftRulerPressed = ImGui::IsItemActivated();
    }

    const int imgW = doc.image().width();
    const int imgH = doc.image().height();

    viewSize_ = Vec2f{avail.x, avail.y};
    viewOrigin_ = Vec2f{p0.x, p0.y};
    rulerOriginX_ = board_ ? doc.originX() : 0;
    rulerOriginY_ = board_ ? doc.originY() : 0;
    view_.setZoomLimits(board_ ? kBoardMinZoom : ViewTransform::kMinZoom, ViewTransform::kMaxZoom);
    if (board_)
        fitMode_ = false;
    view_.setDevicePixelRatio(ImGui::GetIO().DisplayFramebufferScale.x);
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2f mouse{io.MousePos.x - p0.x, io.MousePos.y - p0.y};
    handleViewInput(p0.x, p0.y, hovered, active);
    if (pendingActualSize_) {
        view_.actualSize(viewSize_, imgW, imgH);
        pendingActualSize_ = false;
        pendingFitBoard_ = false;
    } else if (pendingFitBoard_ && board_) {
        fitBoard(doc);
        pendingFitBoard_ = false;
    } else if (fitMode_) {
        view_.fit(viewSize_, imgW, imgH, kFitMargin, pixelArt_ ? ViewTransform::kMaxZoom : 1.0f, pixelArt_);
    }

    const ThemeTokens& t = tokens();
    const float s = view_.scale();
    const Vec2f o = view_.origin();
    const ImVec2 imgMin(p0.x + o.x, p0.y + o.y);
    const ImVec2 imgMax(imgMin.x + static_cast<float>(imgW) * s, imgMin.y + static_cast<float>(imgH) * s);
    const ImVec2 visMin(std::max(imgMin.x, p0.x), std::max(imgMin.y, p0.y));
    const ImVec2 visMax(std::min(imgMax.x, p1.x), std::min(imgMax.y, p1.y));
    const bool imageVisible = imgW > 0 && imgH > 0 && visMax.x > visMin.x && visMax.y > visMin.y;

    // Gestures use the view as it is after this frame's zoom and pan, so the pixel under the pointer is
    // the pixel that gets painted.
    updateGuides(doc, mouse, p0, p1, hovered, topRulerPressed, leftRulerPressed);
    updateSymmetryAxes(doc, tools, mouse, hovered);
    const float snapRadius =
        snapToGuides_ && showGuides_ && !doc.guides().empty() && s > 0.0f ? kGuideSnap / s : 0.0f;
    routeGesture(doc, tools, mouse, hovered, snapRadius);
    keepTextSession(doc, tools);
    syncTexture(doc); // upload what the tools just painted so it shows in this very frame
    updateOnion(doc, tools.gestureActive());

    // Hover pixel for the status bar.
    hoverValid_ = false;
    if (hovered && imgW > 0 && imgH > 0) {
        const Vec2f p = view_.toImage(mouse);
        const int hx = static_cast<int>(std::floor(p.x));
        const int hy = static_cast<int>(std::floor(p.y));
        if (doc.image().contains(hx, hy)) {
            hoverValid_ = true;
            hoverX_ = hx;
            hoverY_ = hy;
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, p1, true);
    // The backdrop pattern follows the image, so it pans and zooms with it. Spacing is in device pixels.
    const float tile = static_cast<float>(patternSpacing_) / view_.devicePixelRatio();
    if (board_)
        dl->AddRectFilled(p0, p1, IM_COL32(paper_.r, paper_.g, paper_.b, 255));
    else
        drawPattern(dl, backdropPattern_, p0, p1, imgMin, tile, color(t.canvasBackdrop), color(t.pattern),
                    color(t.pattern));

    // Pixel art tile preview: the picture 3 x 3 with the neighbours faded. It covers the pattern behind the center too.
    const bool tiled = pixelArt_ && tilePreview_ && imgW > 0 && imgH > 0 && s > 0.0f && mirror_.texture() != nullptr;
    if (tiled)
        drawTilePreview(dl, p0, p1, imgMin, imgW, imgH, tile);

    if (imageVisible) {
        // Behind transparent pixels. Only the visible part is drawn.
        if (!tiled && !board_)
            drawPattern(dl, transparencyPattern_, visMin, visMax, imgMin, tile, color(t.checkerA), color(t.checkerB),
                        color(t.checkerB));

        if (pixelArt_ && onionSkin_)
            drawOnion(dl, p0, p1, imgMin, imgW, imgH);

        if (SDL_Texture* texture = mirror_.texture()) {
            mirror_.setNearest(view_.zoom() >= 1.0f);
            drawTextureSlice(dl, texture, imgMin, imgMax, p0, p1, view_.zoom() >= 1.0f, IM_COL32_WHITE);
        }

        if (showPixelGrid_ && !board_ && view_.zoom() >= gridMinZoom_) {
            const ImU32 grid = color(t.pixelGrid);
            const int gx0 = static_cast<int>(std::floor((visMin.x - imgMin.x) / s));
            const int gx1 = static_cast<int>(std::ceil((visMax.x - imgMin.x) / s));
            const int gy0 = static_cast<int>(std::floor((visMin.y - imgMin.y) / s));
            const int gy1 = static_cast<int>(std::ceil((visMax.y - imgMin.y) / s));
            for (int gx = std::max(gx0, 0); gx <= std::min(gx1, imgW); ++gx) {
                const float x = imgMin.x + static_cast<float>(gx) * s;
                dl->AddLine(ImVec2(x, visMin.y), ImVec2(x, visMax.y), grid);
            }
            for (int gy = std::max(gy0, 0); gy <= std::min(gy1, imgH); ++gy) {
                const float y = imgMin.y + static_cast<float>(gy) * s;
                dl->AddLine(ImVec2(visMin.x, y), ImVec2(visMax.x, y), grid);
            }
        }

        if (hoverValid_ && view_.zoom() >= gridMinZoom_) {
            const ImVec2 a(imgMin.x + static_cast<float>(hoverX_) * s, imgMin.y + static_cast<float>(hoverY_) * s);
            dl->AddRect(a, ImVec2(a.x + s, a.y + s), color(t.accent), 0.0f, 0, 1.5f);
        }

        if (!board_)
            dl->AddRect(ImVec2(imgMin.x - 1.0f, imgMin.y - 1.0f), ImVec2(imgMax.x + 1.0f, imgMax.y + 1.0f),
                        color(t.canvasBorder));
    }

    if (imageVisible && tools.textEditing())
        drawTextPreview(dl, tools, p0, p1, imgMin);
    if (imageVisible && showGuides_)
        drawGuides(dl, doc, p0, p1, imgMin);
    if (imageVisible && pixelArt_)
        drawSymmetryAxes(dl, doc, tools.settings(), p0, p1, imgMin);

    // Selection outline, and a move cursor over it while the Select tool is active.
    if (doc.selection().active()) {
        const IRect sr = doc.selection().rect();
        const ImVec2 a(imgMin.x + static_cast<float>(sr.x0) * s, imgMin.y + static_cast<float>(sr.y0) * s);
        const ImVec2 b(imgMin.x + static_cast<float>(sr.x1) * s, imgMin.y + static_cast<float>(sr.y1) * s);
        dashedRect(dl, a, b, color(t.selection), color(t.windowBg), 4.0f);
        if (hoverValid_ && tools.current() == ToolId::Select && doc.selection().contains(hoverX_, hoverY_) &&
            gestureButton_ < 0)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    // Brush ring: shows the footprint of brush-like tools. It sits over arbitrary image content, so it is
    // drawn as a dark ring with a light ring just inside it and reads on any color in either theme.
    if ((hovered || gestureButton_ >= 0) && !hoverGuide_ && !guideDrag_.active && !symmetryHandleHot() &&
        ImGui::IsMousePosValid(&io.MousePos)) {
        const float ringRadius = tools.cursorRadius() * s;
        const int squareSize = tools.cursorSquareSize();
        if (squareSize > 0) {
            // A square stamp: outline the pixels it covers (the stamp is centered on the pixel under the pointer).
            const Vec2f p = view_.toImage(mouse);
            const float left = std::floor(p.x) - static_cast<float>(squareSize / 2);
            const float top = std::floor(p.y) - static_cast<float>(squareSize / 2);
            const Vec2f a = view_.toView(Vec2f{left, top});
            const Vec2f b = view_.toView(Vec2f{left + static_cast<float>(squareSize), top + static_cast<float>(squareSize)});
            const ImVec2 min(p0.x + a.x, p0.y + a.y);
            const ImVec2 max(p0.x + b.x, p0.y + b.y);
            dl->AddRect(min, max, IM_COL32(0, 0, 0, 200), 0.0f, 0, 1.0f);
            dl->AddRect(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, max.y - 1.0f), IM_COL32(255, 255, 255, 200),
                        0.0f, 0, 1.0f);
        } else if (ringRadius >= kMinRingRadius) {
            ImVec2 center = io.MousePos;
            if (!tools.settings().antialias) { // hard-edged brushes are centered on a pixel
                const Vec2f p = view_.toImage(mouse);
                const Vec2f snapped = view_.toView(Vec2f{std::floor(p.x) + 0.5f, std::floor(p.y) + 0.5f});
                center = ImVec2(p0.x + snapped.x, p0.y + snapped.y);
            }
            dl->AddCircle(center, ringRadius, IM_COL32(0, 0, 0, 200), 0, 1.0f);
            dl->AddCircle(center, ringRadius - 1.0f, IM_COL32(255, 255, 255, 200), 0, 1.0f);
        }
    }

    if (hovered && tools.current() == ToolId::Text && !hoverGuide_ && !guideDrag_.active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

    dl->PopClipRect();

    if (showRulers_) {
        const bool marker = (hovered || guideDrag_.active) && ImGui::IsMousePosValid(&io.MousePos);
        drawRulers(dl, origin, p0, p1, imgMin, rulerSize, mouse, marker);
    }

    if (tools.textEditing())
        drawTextEntry(doc, tools, p0, p1, imgMin);
}

// An open text box ends when the Text tool is put down (the text is applied), or when what it was typed onto went
// away (another document or layer: the text is dropped). Nothing happens mid-click, which could still open a box.
void CanvasView::keepTextSession(Document& doc, Toolbox& tools) {
    if (!tools.textEditing() || tools.gestureActive())
        return;
    if (tools.text().stale(doc))
        tools.discardText();
    else if (tools.current() != ToolId::Text)
        tools.commitText(doc);
}

// The typed text as the pixels it will become, with a dashed outline of its box. Every row is drawn as runs of
// equal coverage, so solid parts of a glyph are a few wide rectangles instead of one per pixel.
void CanvasView::drawTextPreview(ImDrawList* dl, Toolbox& tools, const ImVec2& p0, const ImVec2& p1,
                                 const ImVec2& imgMin) const {
    TextTool& tool = tools.text();
    const ToolSettings& settings = tools.settings();
    const TextBitmap& bitmap = tool.bitmap(settings);
    const ThemeTokens& t = tokens();
    const float s = view_.scale();
    if (!(s > 0.0f))
        return;

    const ImVec2 boxMin(imgMin.x + static_cast<float>(tool.originX()) * s,
                        imgMin.y + static_cast<float>(tool.originY()) * s);
    // No text yet: the box shows where it will start, about one letter wide.
    const float lineSize =
        static_cast<float>(std::clamp(settings.textSize, ToolSettings::kMinTextSize, ToolSettings::kMaxTextSize));
    const float boxW = bitmap.empty() ? std::max(2.0f, lineSize * 0.5f) : static_cast<float>(bitmap.width);
    const float boxH = bitmap.empty() ? lineSize : static_cast<float>(bitmap.height);

    if (!bitmap.empty()) {
        const Rgba8 c = tool.color(settings);
        const int x0 = std::max(0, static_cast<int>(std::floor((p0.x - boxMin.x) / s)));
        const int x1 = std::min(bitmap.width, static_cast<int>(std::ceil((p1.x - boxMin.x) / s)));
        const int y0 = std::max(0, static_cast<int>(std::floor((p0.y - boxMin.y) / s)));
        const int y1 = std::min(bitmap.height, static_cast<int>(std::ceil((p1.y - boxMin.y) / s)));
        int rects = 0;
        for (int y = y0; y < y1 && rects < kMaxTextPreviewRects; ++y) {
            const float top = boxMin.y + static_cast<float>(y) * s;
            for (int x = x0; x < x1 && rects < kMaxTextPreviewRects;) {
                const std::uint8_t cov = bitmap.at(x, y);
                if (cov == 0) {
                    ++x;
                    continue;
                }
                int run = x + 1;
                while (run < x1 && bitmap.at(run, y) == cov)
                    ++run;
                const int alpha = static_cast<int>(detail::mul255(c.a, cov));
                dl->AddRectFilled(ImVec2(boxMin.x + static_cast<float>(x) * s, top),
                                  ImVec2(boxMin.x + static_cast<float>(run) * s, top + s),
                                  IM_COL32(c.r, c.g, c.b, alpha));
                x = run;
                ++rects;
            }
        }
    }
    dashedRect(dl, ImVec2(boxMin.x - 1.0f, boxMin.y - 1.0f),
               ImVec2(boxMin.x + boxW * s + 1.0f, boxMin.y + boxH * s + 1.0f), color(t.selection), color(t.windowBg),
               4.0f);
}

// The floating text field. It sits just under the box (above it near the bottom edge) and takes the keyboard when a
// box opens. Typing updates the tool's text, and the preview follows at once. Ctrl+Enter or clicking anywhere else
// applies the text; Escape drops it. Clicking the field's own margin keeps it open.
void CanvasView::drawTextEntry(Document& doc, Toolbox& tools, const ImVec2& p0, const ImVec2& p1, const ImVec2& imgMin) {
    TextTool& tool = tools.text();
    const ImGuiIO& io = ImGui::GetIO();
    const float font = ImGui::GetFontSize();
    const float s = view_.scale();

    const bool opened = tool.serial() != textSerial_;
    if (opened) {
        textSerial_ = tool.serial();
        textBuffer_.assign(kMaxTextBytes + 1, '\0');
        std::memcpy(textBuffer_.data(), tool.text().data(), std::min(tool.text().size(), kMaxTextBytes));
        textFocusPending_ = true;
    }
    if (textBuffer_.size() != kMaxTextBytes + 1)
        textBuffer_.assign(kMaxTextBytes + 1, '\0');

    // Where the field goes: under the text box, or above it when there is no room below.
    const TextBitmap& bitmap = tool.bitmap(tools.settings());
    const float lineSize = static_cast<float>(
        std::clamp(tools.settings().textSize, ToolSettings::kMinTextSize, ToolSettings::kMaxTextSize));
    const float boxH = bitmap.empty() ? lineSize : static_cast<float>(bitmap.height);
    const float boxLeft = imgMin.x + static_cast<float>(tool.originX()) * s;
    const float boxTop = imgMin.y + static_cast<float>(tool.originY()) * s;
    const float boxBottom = boxTop + boxH * s;
    const float gap = std::round(font * 0.5f);
    const Vec2f size = textEntrySize_.x > 0.0f ? textEntrySize_ : Vec2f{font * 22.0f, font * 8.0f};
    const float x = std::clamp(boxLeft, p0.x, std::max(p0.x, p1.x - size.x));
    float y = boxBottom + gap;
    if (y + size.y > p1.y)
        y = boxTop - gap - size.y;
    if (y < p0.y)
        y = std::max(p0.y, p1.y - size.y);

    ImGui::SetNextWindowPos(ImVec2(std::round(x), std::round(y)));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar;
    bool apply = false;
    bool drop = false;
    if (ImGui::Begin("##TraceTextEntry", nullptr, flags)) {
        const ImVec2 current = ImGui::GetWindowSize();
        textEntrySize_ = Vec2f{current.x, current.y};

        const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool ctrlEnter = io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                              ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
        const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        apply = windowFocused && ctrlEnter;
        drop = windowFocused && escape;

        if (textFocusPending_) {
            ImGui::SetKeyboardFocusHere();
            textFocusPending_ = false;
        }
        if (ImGui::InputTextMultiline("##text", textBuffer_.data(), textBuffer_.size(),
                                      ImVec2(font * 22.0f, font * 5.0f), ImGuiInputTextFlags_AllowTabInput))
            tool.setText(textBuffer_.data());
        const bool deactivated = ImGui::IsItemDeactivated();
        ImGui::PushTextWrapPos(font * 22.0f);
        ImGui::TextDisabled("%s", tr(Str::TextEntryHint));
        ImGui::PopTextWrapPos();

        if (deactivated && !apply && !drop && !opened) {
            // The field lost the keyboard. Inside the window (its margin or hint) it just takes it back; anywhere
            // else it means the user moved on, so the text is applied.
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))
                textFocusPending_ = true;
            else
                apply = true;
        }
    }
    ImGui::End();

    if (drop)
        tools.discardText();
    else if (apply && !tools.gestureActive())
        tools.commitText(doc);
}

// Picks up, drags and drops guides. A guide is created by pressing on a ruler; an existing one is picked up
// by pressing on it. Both are held outside the document until released.
void CanvasView::updateGuides(Document& doc, Vec2f mouse, const ImVec2& p0, const ImVec2& p1, bool hovered,
                              bool topRulerPressed, bool leftRulerPressed) {
    hoverGuide_.reset();
    const ImGuiIO& io = ImGui::GetIO();
    Guides& guides = doc.guides();
    const int imgW = doc.image().width();
    const int imgH = doc.image().height();

    const auto boundaryUnderPointer = [&](GuideAxis axis) -> std::optional<int> {
        const Vec2f p = view_.toImage(mouse);
        return toBoundary(axis == GuideAxis::Vertical ? p.x : p.y);
    };
    const auto cursorFor = [](GuideAxis axis) {
        ImGui::SetMouseCursor(axis == GuideAxis::Vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    };

    if (guideDrag_.active) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            // Cancel: a guide that was picked up goes back where it was, a new one is dropped.
            if (guideDrag_.original)
                guides.add(guideDrag_.axis, *guideDrag_.original);
            guideDrag_ = GuideDrag{};
            return;
        }
        if (const auto position = boundaryUnderPointer(guideDrag_.axis))
            guideDrag_.position = *position;
        cursorFor(guideDrag_.axis);
        ImGui::SetTooltip("%s", trf(guideDrag_.axis == GuideAxis::Vertical ? Str::GuideTipX : Str::GuideTipY,
                                   {std::to_string(guideDrag_.position)})
                                .c_str());

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const bool overViewport = io.MousePos.x >= p0.x && io.MousePos.x < p1.x && io.MousePos.y >= p0.y &&
                                      io.MousePos.y < p1.y;
            const int limit = guideDrag_.axis == GuideAxis::Vertical ? imgW : imgH;
            if (overViewport && guideDrag_.position >= 0 && guideDrag_.position <= limit)
                guides.add(guideDrag_.axis, guideDrag_.position);
            guideDrag_ = GuideDrag{};
        }
        return;
    }

    if (!showGuides_ || doc.image().empty() || gestureButton_ >= 0 || symDrag_.active ||
        !ImGui::IsMousePosValid(&io.MousePos))
        return;

    if (topRulerPressed || leftRulerPressed) {
        guideDrag_.axis = topRulerPressed ? GuideAxis::Horizontal : GuideAxis::Vertical;
        guideDrag_.active = true;
        guideDrag_.original.reset();
        guideDrag_.position = boundaryUnderPointer(guideDrag_.axis).value_or(0);
        return;
    }

    const bool panIntent = ImGui::IsKeyDown(ImGuiKey_Space) || io.MouseDown[ImGuiMouseButton_Middle];
    if (!hovered || panIntent)
        return;

    // The closest guide within reach of the pointer; a vertical one wins a tie.
    const float s = view_.scale();
    const float reach = std::max(kGuideGrab, ImGui::GetFontSize() * 0.35f);
    float best = reach;
    for (const GuideAxis axis : {GuideAxis::Vertical, GuideAxis::Horizontal}) {
        const float along = axis == GuideAxis::Vertical ? mouse.x : mouse.y;
        const float start = axis == GuideAxis::Vertical ? view_.origin().x : view_.origin().y;
        for (const int g : guides.positions(axis)) {
            const float d = std::fabs(start + static_cast<float>(g) * s - along);
            if (d <= best) {
                best = d;
                hoverGuide_ = std::make_pair(axis, g);
            }
        }
    }
    if (!hoverGuide_)
        return;

    cursorFor(hoverGuide_->first);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        guides.remove(hoverGuide_->first, hoverGuide_->second);
        guideDrag_.active = true;
        guideDrag_.axis = hoverGuide_->first;
        guideDrag_.position = hoverGuide_->second;
        guideDrag_.original = hoverGuide_->second;
    }
}

void CanvasView::drawGuides(ImDrawList* dl, const Document& doc, const ImVec2& p0, const ImVec2& p1,
                            const ImVec2& imgMin) const {
    const ThemeTokens& t = tokens();
    const float s = view_.scale();
    const ImU32 line = color(t.guide);
    const ImU32 highlight = color(t.accent);

    const auto draw = [&](GuideAxis axis, int g, bool emphasised) {
        const float thickness = emphasised ? 2.0f : 1.0f;
        const ImU32 c = emphasised ? highlight : line;
        if (axis == GuideAxis::Vertical) {
            const float x = std::round(imgMin.x + static_cast<float>(g) * s);
            if (x >= p0.x && x <= p1.x)
                dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), c, thickness);
        } else {
            const float y = std::round(imgMin.y + static_cast<float>(g) * s);
            if (y >= p0.y && y <= p1.y)
                dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), c, thickness);
        }
    };

    for (const GuideAxis axis : {GuideAxis::Vertical, GuideAxis::Horizontal}) {
        for (const int g : doc.guides().positions(axis))
            draw(axis, g, hoverGuide_ && hoverGuide_->first == axis && hoverGuide_->second == g);
    }
    if (guideDrag_.active)
        draw(guideDrag_.axis, guideDrag_.position, true);
}

// Takes and drags the items of the symmetry list (Pixel Art): a rotation center by its marker, a mirror line by its grip
// (both move the point freely) or by the dashed line itself (slides it across, along its normal). Priority: rotation
// markers, then grips, then lines; among those, later items of the list win. The point follows the pointer's movement
// in half pixels, clamped to the canvas; Escape puts it back and a double-click centers the item. Guides win when they
// are under the pointer too, and nothing here starts while a tool gesture runs.
void CanvasView::updateSymmetryAxes(const Document& doc, Toolbox& tools, Vec2f mouse, bool hovered) {
    hoverSymItem_ = -1;
    hoverSymLine_ = false;
    SymmetrySet& set = tools.settings().symmetry;
    const int imgW = doc.image().width();
    const int imgH = doc.image().height();
    const int count = std::clamp(set.count, 0, kMaxSymmetryItems);
    if (!pixelArt_ || count == 0 || imgW <= 0 || imgH <= 0) {
        symDrag_ = SymmetryDrag{}; // an item that went away mid-drag is let go
        return;
    }
    const auto item = [&set](int i) -> SymmetryItem& { return set.items[static_cast<std::size_t>(i)]; };

    const ImGuiIO& io = ImGui::GetIO();
    const bool mouseValid = ImGui::IsMousePosValid(&io.MousePos);
    const float s = view_.scale();
    const Vec2f o = view_.origin();
    const auto cursorFor = [](const SymmetryItem& it, bool grip) {
        if (grip || it.kind == SymmetryKind::Rotate) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            return;
        }
        const int a = normalizeMirrorAngle(it.angle); // the direction of the line's normal decides the arrows
        ImGui::SetMouseCursor(a < 23 || a >= 158 ? ImGuiMouseCursor_ResizeEW
                              : a < 68            ? ImGuiMouseCursor_ResizeNWSE
                              : a < 113           ? ImGuiMouseCursor_ResizeNS
                                                  : ImGuiMouseCursor_ResizeNESW);
    };

    if (symDrag_.active) {
        const int i = symDrag_.index;
        if (i < 0 || i >= count || !item(i).enabled) { // switched off or removed mid-drag: let go where it is
            symDrag_ = SymmetryDrag{};
            return;
        }
        SymmetryItem& it = item(i);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            it.posX = symDrag_.originalX;
            it.posY = symDrag_.originalY;
            symDrag_ = SymmetryDrag{};
            return;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            symDrag_ = SymmetryDrag{};
            return;
        }
        if (mouseValid) {
            const Vec2f p = view_.toImage(mouse);
            float dx = p.x - symDrag_.start.x; // how far the pointer moved since the press, in image pixels
            float dy = p.y - symDrag_.start.y;
            if (symDrag_.perpendicular) { // only the part along the line's normal counts
                const double a = static_cast<double>(it.angle) * 3.14159265358979323846 / 180.0;
                const float nx = static_cast<float>(std::cos(a));
                const float ny = static_cast<float>(std::sin(a));
                const float along = dx * nx + dy * ny;
                dx = along * nx;
                dy = along * ny;
            }
            if (std::isfinite(dx) && std::isfinite(dy)) { // both or neither, so a bad coordinate never moves half
                const auto place = [](int start, float delta, int size) {
                    return std::clamp(static_cast<int>(std::lround(std::clamp(static_cast<float>(start) + delta * 2.0f, -1.0e6f, 1.0e6f))), 0, 2 * size);
                };
                it.posX = storedAxis(place(symDrag_.startX, dx, imgW), imgW);
                it.posY = storedAxis(place(symDrag_.startY, dy, imgH), imgH);
            }
        }
        cursorFor(it, !symDrag_.perpendicular);
        ImGui::SetTooltip("%s", trf(Str::SymmetryDragTip, {symmetryItemName(set, i),
                                                           axisText(resolveAxis(it.posX, imgW)),
                                                           axisText(resolveAxis(it.posY, imgH))})
                                    .c_str());
        return;
    }

    const bool panIntent = ImGui::IsKeyDown(ImGuiKey_Space) || io.MouseDown[ImGuiMouseButton_Middle];
    if (!hovered || !mouseValid || panIntent || gestureButton_ >= 0 || tools.gestureActive() || guideDrag_.active ||
        hoverGuide_)
        return;

    const Vec2f lo{std::max(o.x, 0.0f), std::max(o.y, 0.0f)};
    const Vec2f hi{std::min(o.x + static_cast<float>(imgW) * s, viewSize_.x),
                   std::min(o.y + static_cast<float>(imgH) * s, viewSize_.y)};
    std::array<SymmetryGeometry, kMaxSymmetryItems> geo{};
    for (int i = 0; i < count; ++i)
        geo[static_cast<std::size_t>(i)] = symmetryGeometry(item(i), imgW, imgH, s, o, lo, hi);

    const float reach = std::max(kAxisGrab, ImGui::GetFontSize() * 0.3f);
    const float handle = reach * 1.5f;
    int hit = -1;
    bool line = false;
    // Rotation markers first, then grips: later items on top.
    for (const SymmetryKind kind : {SymmetryKind::Rotate, SymmetryKind::Mirror}) {
        for (int i = count - 1; i >= 0 && hit < 0; --i) {
            const SymmetryItem& it = item(i);
            const SymmetryGeometry& g = geo[static_cast<std::size_t>(i)];
            if (it.enabled && it.kind == kind && g.shown && std::hypot(mouse.x - g.grip.x, mouse.y - g.grip.y) <= handle)
                hit = i;
        }
    }
    // Then the closest line within reach; on equal distances the later item wins.
    if (hit < 0) {
        float best = reach;
        for (int i = 0; i < count; ++i) {
            const SymmetryItem& it = item(i);
            const SymmetryGeometry& g = geo[static_cast<std::size_t>(i)];
            if (!it.enabled || it.kind != SymmetryKind::Mirror || !g.shown)
                continue;
            const float d = distanceToSegment(mouse, g.a, g.b);
            if (d <= best) {
                best = d;
                hit = i;
                line = true;
            }
        }
    }
    if (hit < 0)
        return;

    hoverSymItem_ = hit;
    hoverSymLine_ = line;
    SymmetryItem& it = item(hit);
    cursorFor(it, !line);
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        it.posX = kSymmetryCenter;
        it.posY = kSymmetryCenter;
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        symDrag_.active = true;
        symDrag_.index = hit;
        symDrag_.perpendicular = line;
        symDrag_.start = view_.toImage(mouse);
        symDrag_.startX = resolveAxis(it.posX, imgW);
        symDrag_.startY = resolveAxis(it.posY, imgH);
        symDrag_.originalX = it.posX;
        symDrag_.originalY = it.posY;
    }
}

void CanvasView::drawSymmetryAxes(ImDrawList* dl, const Document& doc, const ToolSettings& settings, const ImVec2& p0,
                                  const ImVec2& p1, const ImVec2& imgMin) const {
    const SymmetrySet& set = settings.symmetry;
    const int imgW = doc.image().width();
    const int imgH = doc.image().height();
    const int count = std::clamp(set.count, 0, kMaxSymmetryItems);
    if (count == 0 || imgW <= 0 || imgH <= 0)
        return;

    const ThemeTokens& t = tokens();
    const float s = view_.scale();
    const float unit = ImGui::GetFontSize();
    const float grip = std::max(3.0f, std::round(unit * 0.3f));
    ImVec4 haloColor = t.symmetryAxis;
    haloColor.w *= 0.3f;
    const ImU32 halo = color(haloColor);
    const ImU32 line = color(t.symmetryAxis);
    const ImU32 rim = color(t.windowBg);
    ImVec4 spokeColor = t.symmetryAxis;
    spokeColor.w *= 0.55f;
    const ImU32 spoke = color(spokeColor);

    const Vec2f origin{imgMin.x, imgMin.y};
    const Vec2f lo{std::max(imgMin.x, p0.x), std::max(imgMin.y, p0.y)};
    const Vec2f hi{std::min(imgMin.x + static_cast<float>(imgW) * s, p1.x),
                   std::min(imgMin.y + static_cast<float>(imgH) * s, p1.y)};
    const unsigned emphasis = symmetryEmphasisMask();

    for (int i = 0; i < count; ++i) { // later items are drawn over earlier ones
        const SymmetryItem& it = set.items[static_cast<std::size_t>(i)];
        if (!it.enabled)
            continue;
        const SymmetryGeometry g = symmetryGeometry(it, imgW, imgH, s, origin, lo, hi);
        const bool emphasised = ((emphasis >> static_cast<unsigned>(i)) & 1u) != 0 || hoverSymItem_ == i ||
                                (symDrag_.active && symDrag_.index == i);
        const float thickness = emphasised ? 2.0f : 1.0f;

        if (it.kind == SymmetryKind::Mirror) {
            if (!g.shown)
                continue;
            dl->AddLine(ImVec2(g.a.x, g.a.y), ImVec2(g.b.x, g.b.y), halo, thickness + 2.0f);
            dashedLine(dl, ImVec2(g.a.x, g.a.y), ImVec2(g.b.x, g.b.y), line, thickness, 6.0f, 4.0f);
            const ImVec2 center(g.grip.x, g.grip.y);
            const float radius = emphasised ? grip * 1.15f : grip;
            dl->AddCircleFilled(center, radius, line);
            dl->AddCircle(center, radius, rim);
            continue;
        }

        // Rotation: faint spokes from the center, one per copy (the first points up, the rest follow clockwise),
        // cut to what is visible, then the center marker on top.
        const int copies = sanitizeRotateCount(it.count);
        for (int k = 0; k < copies; ++k) {
            const double angle = 6.283185307179586 * static_cast<double>(k) / static_cast<double>(copies);
            Vec2f d{static_cast<float>(std::sin(angle)), -static_cast<float>(std::cos(angle))};
            if (std::fabs(d.x) < 1.0e-6f)
                d.x = 0.0f;
            if (std::fabs(d.y) < 1.0e-6f)
                d.y = 0.0f;
            const auto range = clipLine(g.p, d, lo, hi);
            if (!range || range->second <= 0.0f)
                continue;
            const float from = std::max(range->first, 0.0f);
            dashedLine(dl, ImVec2(g.p.x + d.x * from, g.p.y + d.y * from),
                       ImVec2(g.p.x + d.x * range->second, g.p.y + d.y * range->second), spoke, thickness, 6.0f, 4.0f);
        }
        const float radius = grip * 1.3f;
        const float arm = radius * 2.0f;
        const ImVec2 c(g.p.x, g.p.y);
        dl->AddCircleFilled(c, radius + 1.0f, halo);
        dl->AddLine(ImVec2(c.x - arm, c.y), ImVec2(c.x + arm, c.y), halo, thickness + 2.0f);
        dl->AddLine(ImVec2(c.x, c.y - arm), ImVec2(c.x, c.y + arm), halo, thickness + 2.0f);
        dl->AddLine(ImVec2(c.x - arm, c.y), ImVec2(c.x + arm, c.y), line, thickness);
        dl->AddLine(ImVec2(c.x, c.y - arm), ImVec2(c.x, c.y + arm), line, thickness);
        dl->AddCircle(c, radius, rim, 0, thickness + 1.0f);
        dl->AddCircle(c, radius, line, 0, thickness);
    }
}

void CanvasView::drawRulers(ImDrawList* dl, const ImVec2& origin, const ImVec2& p0, const ImVec2& p1,
                            const ImVec2& imgMin, float thickness, Vec2f mouse, bool showMarker) const {
    const ThemeTokens& t = tokens();
    const ImU32 bg = color(t.rulerBg);
    const ImU32 text = color(t.rulerText);
    const ImU32 edge = color(t.canvasBorder);
    const ImU32 marker = color(t.accent);
    const float s = view_.scale();
    const float fontSize = ImGui::GetFontSize();

    // Strips and the corner where they meet.
    dl->AddRectFilled(origin, ImVec2(p1.x, p0.y), bg);
    dl->AddRectFilled(ImVec2(origin.x, p0.y), ImVec2(p0.x, p1.y), bg);
    dl->AddLine(ImVec2(origin.x, p0.y), ImVec2(p1.x, p0.y), edge);
    dl->AddLine(ImVec2(p0.x, origin.y), ImVec2(p0.x, p1.y), edge);

    // One step per axis: the smallest "nice" number whose labels do not crowd each other.
    int step = kRulerSteps[std::size(kRulerSteps) - 1];
    for (const int candidate : kRulerSteps) {
        if (static_cast<float>(candidate) * s >= fontSize * kRulerLabelEm) {
            step = candidate;
            break;
        }
    }
    int minorDivisions = step % 10 == 0 ? 10 : step % 5 == 0 ? 5 : step % 2 == 0 ? 2 : 1;
    if (static_cast<float>(step / minorDivisions) * s < kRulerMinMinorTick)
        minorDivisions = 1;
    const int unit = step / minorDivisions;

    const auto drawAxis = [&](bool horizontal) {
        const float lo = horizontal ? p0.x : p0.y;
        const float hi = horizontal ? p1.x : p1.y;
        const float zero = horizontal ? imgMin.x + static_cast<float>(rulerOriginX_) * s
                                      : imgMin.y + static_cast<float>(rulerOriginY_) * s;
        const long long first = static_cast<long long>(std::floor((lo - zero) / s / static_cast<float>(unit)));
        const long long last = static_cast<long long>(std::ceil((hi - zero) / s / static_cast<float>(unit)));

        dl->PushClipRect(horizontal ? ImVec2(p0.x, origin.y) : ImVec2(origin.x, p0.y),
                         horizontal ? ImVec2(p1.x, p0.y) : ImVec2(p0.x, p1.y), true);
        for (long long k = first; k <= last; ++k) {
            const long long value = k * unit;
            const float pos = std::round(zero + static_cast<float>(value) * s);
            const bool major = k % minorDivisions == 0;
            const float length = thickness * (major ? 0.55f : 0.25f);
            if (horizontal)
                dl->AddLine(ImVec2(pos, p0.y - length), ImVec2(pos, p0.y), text);
            else
                dl->AddLine(ImVec2(p0.x - length, pos), ImVec2(p0.x, pos), text);
            if (!major)
                continue;

            const std::string label = std::to_string(value);
            if (horizontal) {
                dl->AddText(ImVec2(pos + 3.0f, origin.y + 1.0f), text, label.c_str());
            } else {
                // Digits are stacked upright, because the text cannot be rotated.
                float y = pos + 2.0f;
                for (const char& c : label) {
                    const float w = ImGui::CalcTextSize(&c, &c + 1).x;
                    dl->AddText(ImVec2(origin.x + (thickness - w) * 0.5f, y), text, &c, &c + 1);
                    y += fontSize * 0.8f;
                }
            }
        }
        if (showMarker) {
            if (horizontal)
                dl->AddLine(ImVec2(p0.x + mouse.x, origin.y), ImVec2(p0.x + mouse.x, p0.y), marker);
            else
                dl->AddLine(ImVec2(origin.x, p0.y + mouse.y), ImVec2(p0.x, p0.y + mouse.y), marker);
        }
        dl->PopClipRect();
    };
    drawAxis(true);
    drawAxis(false);
}

} // namespace trace
