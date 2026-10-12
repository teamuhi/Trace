#include "app/canvas_view.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>

#include "app/pattern_draw.hpp"
#include "app/theme.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "tools/toolbox.hpp"

namespace trace {

namespace {

constexpr float kZoomStep = 1.25f;          // menu and keyboard zoom
constexpr float kZoomPerWheelNotch = 1.15f; // Ctrl + wheel
constexpr float kGridMinZoom = 8.0f;        // pixel grid and hover outline appear from 800%
constexpr float kFitMargin = 24.0f;
constexpr float kMinRingRadius = 3.0f; // smaller rings are just a dot; the pointer itself is enough
constexpr float kRulerThicknessEm = 1.5f;  // ruler width as a multiple of the font size
constexpr float kRulerLabelEm = 5.5f;      // least distance between two ruler labels
constexpr float kRulerMinMinorTick = 4.0f; // minor ticks closer than this (view units) are left out
constexpr float kGuideGrab = 5.0f;         // how close the pointer must be to pick up a guide (view units)
constexpr float kGuideSnap = 6.0f;         // how close a shape or selection edge must be to snap (view units)
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

} // namespace

void CanvasView::release() noexcept {
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    textureWidth_ = 0;
    textureHeight_ = 0;
}

void CanvasView::zoomBy(float factor) noexcept {
    view_.zoomByFactorAt(Vec2f{viewSize_.x * 0.5f, viewSize_.y * 0.5f}, factor);
    fitMode_ = false;
}

void CanvasView::zoomIn() noexcept {
    zoomBy(kZoomStep);
}

void CanvasView::zoomOut() noexcept {
    zoomBy(1.0f / kZoomStep);
}

void CanvasView::fitToWindow() noexcept {
    fitMode_ = true;
}

void CanvasView::actualSize() noexcept {
    pendingActualSize_ = true; // applied in draw(), where the document size is known
    fitMode_ = false;
}

void CanvasView::syncTexture(Document& doc) {
    if (!renderer_)
        return;

    const Image& img = doc.image();
    if (img.empty())
        return;

    if (img.width() != textureWidth_ || img.height() != textureHeight_) {
        if (texture_) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }
        textureWidth_ = img.width();
        textureHeight_ = img.height();
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                     textureWidth_, textureHeight_);
        if (texture_) {
            SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
            textureNearest_ = true;
            SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
        } else {
            SDL_Log("SDL_CreateTexture(%dx%d) failed: %s", textureWidth_, textureHeight_, SDL_GetError());
        }
        fitMode_ = true;
        doc.markAllDirty();
    }

    const IRect dirty = doc.takeDirty();
    if (dirty.empty() || !texture_)
        return;

    const SDL_Rect rect{dirty.x0, dirty.y0, dirty.width(), dirty.height()};
    const std::size_t offset = static_cast<std::size_t>(dirty.y0) * static_cast<std::size_t>(img.width()) +
                               static_cast<std::size_t>(dirty.x0);
    const int pitch = img.width() * static_cast<int>(sizeof(Rgba8));
    if (!SDL_UpdateTexture(texture_, &rect, img.pixels().data() + offset, pitch))
        SDL_Log("SDL_UpdateTexture failed: %s", SDL_GetError());
}

void CanvasView::handleViewInput(float originX, float originY, bool hovered, bool active) {
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2f mouse{io.MousePos.x - originX, io.MousePos.y - originY};

    if (hovered && io.MouseWheel != 0.0f && io.KeyCtrl) {
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
        if (!hovered || !mouseValid || panIntent || guideDrag_.active || hoverGuide_)
            return;
        for (const int button : {ImGuiMouseButton_Left, ImGuiMouseButton_Right}) {
            if (ImGui::IsMouseClicked(button)) {
                gestureButton_ = button;
                gestureCancelled_ = false;
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
    syncTexture(doc);

    // The rulers take a strip along the top and left of the window; everything else (the viewport) is
    // laid out as if it started below and to the right of them.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 full = ImGui::GetContentRegionAvail();
    const float rulerSize = showRulers_ ? std::round(ImGui::GetFontSize() * kRulerThicknessEm) : 0.0f;
    const ImVec2 avail(full.x - rulerSize, full.y - rulerSize);
    if (avail.x < 1.0f || avail.y < 1.0f)
        return;

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
    view_.setDevicePixelRatio(ImGui::GetIO().DisplayFramebufferScale.x);
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2f mouse{io.MousePos.x - p0.x, io.MousePos.y - p0.y};
    handleViewInput(p0.x, p0.y, hovered, active);
    if (pendingActualSize_) {
        view_.actualSize(viewSize_, imgW, imgH);
        pendingActualSize_ = false;
    } else if (fitMode_) {
        view_.fit(viewSize_, imgW, imgH, kFitMargin);
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
    const float snapRadius =
        snapToGuides_ && showGuides_ && !doc.guides().empty() && s > 0.0f ? kGuideSnap / s : 0.0f;
    routeGesture(doc, tools, mouse, hovered, snapRadius);
    syncTexture(doc); // upload what the tools just painted so it shows in this very frame

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
    drawPattern(dl, backdropPattern_, p0, p1, imgMin, tile, color(t.canvasBackdrop), color(t.pattern),
                color(t.pattern));

    if (imageVisible) {
        // Behind transparent pixels. Only the visible part is drawn.
        drawPattern(dl, transparencyPattern_, visMin, visMax, imgMin, tile, color(t.checkerA), color(t.checkerB),
                    color(t.checkerB));

        if (texture_) {
            const bool wantNearest = view_.zoom() >= 1.0f;
            if (wantNearest != textureNearest_) {
                SDL_SetTextureScaleMode(texture_, wantNearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
                textureNearest_ = wantNearest;
            }
            // Draw only the visible slice; huge off-screen quads lose float precision at high zoom.
            const float invW = 1.0f / (imgMax.x - imgMin.x);
            const float invH = 1.0f / (imgMax.y - imgMin.y);
            const ImVec2 uv0((visMin.x - imgMin.x) * invW, (visMin.y - imgMin.y) * invH);
            const ImVec2 uv1((visMax.x - imgMin.x) * invW, (visMax.y - imgMin.y) * invH);
            dl->AddImage((ImTextureID)(std::intptr_t)texture_, visMin, visMax, uv0, uv1);
        }

        if (showPixelGrid_ && view_.zoom() >= kGridMinZoom) {
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

        if (hoverValid_ && view_.zoom() >= kGridMinZoom) {
            const ImVec2 a(imgMin.x + static_cast<float>(hoverX_) * s, imgMin.y + static_cast<float>(hoverY_) * s);
            dl->AddRect(a, ImVec2(a.x + s, a.y + s), color(t.accent), 0.0f, 0, 1.5f);
        }

        dl->AddRect(ImVec2(imgMin.x - 1.0f, imgMin.y - 1.0f), ImVec2(imgMax.x + 1.0f, imgMax.y + 1.0f),
                    color(t.canvasBorder));
    }

    if (imageVisible && showGuides_)
        drawGuides(dl, doc, p0, p1, imgMin);

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
    if ((hovered || gestureButton_ >= 0) && !hoverGuide_ && !guideDrag_.active &&
        ImGui::IsMousePosValid(&io.MousePos)) {
        const float ringRadius = tools.cursorRadius() * s;
        if (ringRadius >= kMinRingRadius) {
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

    dl->PopClipRect();

    if (showRulers_) {
        const bool marker = (hovered || guideDrag_.active) && ImGui::IsMousePosValid(&io.MousePos);
        drawRulers(dl, origin, p0, p1, imgMin, rulerSize, mouse, marker);
    }
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

    if (!showGuides_ || doc.image().empty() || gestureButton_ >= 0 || !ImGui::IsMousePosValid(&io.MousePos))
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
        const float zero = horizontal ? imgMin.x : imgMin.y;
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
