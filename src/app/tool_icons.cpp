#include "app/tool_icons.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace trace {

namespace {

// Icons are designed on a 24 x 24 grid and scaled to the requested size.
class Canvas {
public:
    // Coordinates are snapped to whole pixels, shifted by half a pixel when the stroke is an odd number of
    // pixels wide, so horizontal and vertical strokes cover complete pixels instead of two blurry halves.
    Canvas(ImDrawList* dl, ImVec2 min, float size, ImU32 color)
        : dl_(dl),
          min_(std::round(min.x), std::round(min.y)),
          unit_(size / 24.0f),
          color_(color),
          thickness_(std::max(1.0f, std::round(size / 13.0f))) {
        half_ = std::fmod(thickness_, 2.0f) == 1.0f ? 0.5f : 0.0f;
    }

    ImVec2 pt(float x, float y) const {
        return ImVec2(min_.x + std::round(x * unit_) + half_, min_.y + std::round(y * unit_) + half_);
    }
    float len(float v) const { return v * unit_; }

    void line(float x0, float y0, float x1, float y1, float widthUnits = 0.0f) const {
        dl_->AddLine(pt(x0, y0), pt(x1, y1), color_, widthUnits > 0.0f ? std::max(1.0f, len(widthUnits)) : thickness_);
    }
    void polyline(std::initializer_list<ImVec2> points, bool closed) const {
        ImVec2 buf[8];
        int n = 0;
        for (const ImVec2& p : points) {
            if (n < 8)
                buf[n++] = pt(p.x, p.y);
        }
        dl_->AddPolyline(buf, n, color_, closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness_);
    }
    void fill(std::initializer_list<ImVec2> points, float alpha = 1.0f) const {
        ImVec2 buf[8];
        int n = 0;
        for (const ImVec2& p : points) {
            if (n < 8)
                buf[n++] = pt(p.x, p.y);
        }
        dl_->AddConvexPolyFilled(buf, n, scaledAlpha(alpha));
    }
    void circle(float x, float y, float r, bool filled) const {
        if (filled)
            dl_->AddCircleFilled(pt(x, y), len(r), color_, 16);
        else
            dl_->AddCircle(pt(x, y), len(r), color_, 20, thickness_);
    }
    void rect(float x0, float y0, float x1, float y1, float rounding) const {
        dl_->AddRect(pt(x0, y0), pt(x1, y1), color_, len(rounding), 0, thickness_);
    }
    void ellipse(float x, float y, float rx, float ry) const {
        dl_->AddEllipse(pt(x, y), ImVec2(len(rx), len(ry)), color_, 0.0f, 28, thickness_);
    }
    // An arc of the circle around (x, y); angles in radians as ImGui measures them (y points down).
    void arc(float x, float y, float r, float from, float to) const {
        dl_->PathArcTo(pt(x, y), len(r), from, to, 14);
        dl_->PathStroke(color_, ImDrawFlags_None, thickness_);
    }
    void curve(ImVec2 a, ImVec2 control, ImVec2 b) const {
        dl_->AddBezierQuadratic(pt(a.x, a.y), pt(control.x, control.y), pt(b.x, b.y), color_, thickness_, 12);
    }

    // A dashed segment: `on` and `off` are lengths in grid units.
    void dashed(float x0, float y0, float x1, float y1, float on, float off) const {
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float total = std::hypot(dx, dy);
        if (!(total > 0.0f))
            return;
        for (float t = 0.0f; t < total; t += on + off) {
            const float e = std::min(t + on, total);
            line(x0 + dx * t / total, y0 + dy * t / total, x0 + dx * e / total, y0 + dy * e / total);
        }
    }

private:
    ImU32 scaledAlpha(float alpha) const {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(color_);
        c.w *= alpha;
        return ImGui::ColorConvertFloat4ToU32(c);
    }

    ImDrawList* dl_;
    ImVec2 min_;
    float unit_;
    ImU32 color_;
    float thickness_;
    float half_ = 0.0f;
};

ImVec2 lerp(ImVec2 a, ImVec2 b, float t) {
    return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

} // namespace

void drawToolIcon(ImDrawList* dl, ToolId tool, ImVec2 min, float size, ImU32 color) {
    const Canvas c(dl, min, size, color);
    switch (tool) {
    case ToolId::Pencil: {
        // A pencil lying at 45 degrees: tip at the bottom left, ferrule and eraser at the top right.
        // Axis direction d = (1, -1) / sqrt(2); half width vector n * w = (1.9, 1.9).
        const ImVec2 tip(3.5f, 20.5f);
        const ImVec2 shoulderA(5.2f, 14.6f); // where the cone meets the body
        const ImVec2 shoulderB(9.4f, 18.8f);
        const ImVec2 endA(15.7f, 4.1f);
        const ImVec2 endB(19.9f, 8.3f);
        c.polyline({tip, shoulderA, endA, endB, shoulderB}, true);
        c.line(shoulderA.x, shoulderA.y, shoulderB.x, shoulderB.y);                         // cone / body edge
        c.line(lerp(shoulderA, endA, 0.78f).x, lerp(shoulderA, endA, 0.78f).y,
               lerp(shoulderB, endB, 0.78f).x, lerp(shoulderB, endB, 0.78f).y);               // ferrule
        c.fill({tip, lerp(tip, shoulderA, 0.45f), lerp(tip, shoulderB, 0.45f)});              // the lead
        c.fill({lerp(shoulderA, endA, 0.78f), endA, endB, lerp(shoulderB, endB, 0.78f)}, 0.45f); // eraser end
        break;
    }
    case ToolId::Brush: {
        // A paintbrush at 45 degrees: long handle at the top right, a short ferrule, and a teardrop of
        // bristles ending in a point at the lower left. P(t, w) is a point t along the brush from the
        // bristle tip and w across it.
        constexpr float k = 0.70710678f;
        const auto P = [k](float t, float w) { return ImVec2(2.8f + k * (t + w), 21.2f + k * (w - t)); };
        // Handle.
        c.polyline({P(10.2f, -2.0f), P(23.2f, -2.0f), P(23.2f, 2.0f), P(10.2f, 2.0f)}, true);
        // Ferrule.
        c.polyline({P(7.4f, -1.6f), P(10.2f, -1.6f), P(10.2f, 1.6f), P(7.4f, 1.6f)}, true);
        // Bristles: two curves from the ferrule that meet in the tip.
        const ImVec2 tip = P(0.0f, 0.0f);
        c.curve(P(7.4f, -1.6f), P(6.2f, -4.8f), tip);
        c.curve(P(7.4f, 1.6f), P(4.6f, 4.6f), tip);
        break;
    }
    case ToolId::Eraser: {
        const ImVec2 p0(3.5f, 14.5f);
        const ImVec2 p1(12.5f, 5.5f);
        const ImVec2 p2(20.0f, 13.0f);
        const ImVec2 p3(11.0f, 22.0f);
        c.polyline({p0, p1, p2, p3}, true);
        c.fill({p0, lerp(p0, p1, 0.42f), lerp(p3, p2, 0.42f), p3}, 0.6f);
        c.line(lerp(p0, p1, 0.42f).x, lerp(p0, p1, 0.42f).y, lerp(p3, p2, 0.42f).x, lerp(p3, p2, 0.42f).y);
        break;
    }
    case ToolId::Line:
        c.line(5.0f, 19.0f, 19.0f, 5.0f);
        c.circle(5.0f, 19.0f, 2.4f, true);
        c.circle(19.0f, 5.0f, 2.4f, true);
        break;
    case ToolId::Rectangle:
        c.rect(3.5f, 5.5f, 20.5f, 18.5f, 1.5f);
        break;
    case ToolId::Ellipse:
        c.ellipse(12.0f, 12.0f, 9.0f, 7.0f);
        break;
    case ToolId::Fill: {
        // A tilted bucket with its handle, and a drop falling from the spout.
        const ImVec2 b0(4.0f, 12.0f);
        const ImVec2 b1(11.5f, 4.5f);
        const ImVec2 b2(19.0f, 12.0f);
        const ImVec2 b3(11.5f, 19.5f);
        c.polyline({b0, b1, b2, b3}, true);
        c.fill({b0, lerp(b0, b1, 0.5f), lerp(b3, b2, 0.5f), b3}, 0.6f);
        c.curve(b1, {8.0f, 0.5f}, {4.5f, 3.0f}); // handle
        c.fill({{21.5f, 14.5f}, {19.6f, 18.0f}, {23.4f, 18.0f}});
        c.circle(21.5f, 19.4f, 2.1f, true);
        break;
    }
    case ToolId::Picker: {
        // An eyedropper lying at 45 degrees (bulb at the top right, tip at the lower left) with a drop under
        // the tip. P(t, w) is a point t along the dropper and w across it. (ox, oy) centres the artwork's
        // bounding box (about 1.8..20.3 x 2.3..23) on the grid.
        constexpr float k = 0.70710678f;
        constexpr float ox = 1.0f;
        constexpr float oy = -0.7f;
        const auto P = [k](float t, float w) { return ImVec2(7.0f + k * (t + w), 15.9f + k * (w - t)); };
        const auto edge = [&](ImVec2 a, ImVec2 b) { c.line(a.x, a.y, b.x, b.y); };

        // Glass tube, narrowing to the tip, and the small round tip.
        edge(P(1.2f, -0.9f), P(9.0f, -1.9f));
        edge(P(1.2f, 0.9f), P(9.0f, 1.9f));
        c.circle(P(0.4f, 0.0f).x, P(0.4f, 0.0f).y, 1.3f, true);
        // The collar.
        c.polyline({P(9.0f, -3.1f), P(11.4f, -3.1f), P(11.4f, 3.1f), P(9.0f, 3.1f)}, true);
        // The bulb: two sides and a rounded end.
        edge(P(11.4f, -2.1f), P(17.2f, -2.1f));
        edge(P(11.4f, 2.1f), P(17.2f, 2.1f));
        const ImVec2 end = P(17.2f, 0.0f);
        c.arc(end.x, end.y, 2.1f, -2.3561945f, 0.7853982f); // the half circle that faces up and to the right
        // The drop.
        c.polyline({{3.8f + ox, 17.6f + oy}, {5.6f + ox, 20.2f + oy}}, false);
        c.polyline({{3.8f + ox, 17.6f + oy}, {2.0f + ox, 20.2f + oy}}, false);
        c.arc(3.8f + ox, 21.0f + oy, 2.0f, -0.4f, 3.5415927f);
        break;
    }
    case ToolId::Select:
        c.dashed(4.0f, 5.0f, 20.0f, 5.0f, 3.0f, 2.2f);
        c.dashed(20.0f, 5.0f, 20.0f, 19.0f, 3.0f, 2.2f);
        c.dashed(20.0f, 19.0f, 4.0f, 19.0f, 3.0f, 2.2f);
        c.dashed(4.0f, 19.0f, 4.0f, 5.0f, 3.0f, 2.2f);
        break;
    }
}

void drawSettingsIcon(ImDrawList* dl, int page, ImVec2 min, float size, ImU32 color) {
    const Canvas c(dl, min, size, color);
    switch (page) {
    case 0: { // General: a gear, a ring with eight teeth around a hub
        c.circle(12.0f, 12.0f, 6.2f, false);
        c.circle(12.0f, 12.0f, 2.3f, false);
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * 0.78539816f;
            const float cs = std::cos(a);
            const float sn = std::sin(a);
            c.line(12.0f + cs * 7.4f, 12.0f + sn * 7.4f, 12.0f + cs * 10.4f, 12.0f + sn * 10.4f);
        }
        break;
    }
    case 1: // Theme: a disc with its right half filled
        c.circle(12.0f, 12.0f, 9.0f, false);
        c.fill({{12.0f, 3.0f}, {16.5f, 4.2f}, {19.8f, 7.5f}, {21.0f, 12.0f}, {19.8f, 16.5f}, {16.5f, 19.8f}, {12.0f, 21.0f}});
        break;
    case 2: // View: an eye
        c.curve({2.0f, 12.0f}, {12.0f, 2.0f}, {22.0f, 12.0f});
        c.curve({2.0f, 12.0f}, {12.0f, 22.0f}, {22.0f, 12.0f});
        c.circle(12.0f, 12.0f, 3.3f, true);
        break;
    case 3: // Tools: the brush
        drawToolIcon(dl, ToolId::Brush, min, size, color);
        break;
    case 4: // Pixel Art: a 3 x 3 grid of squares, some filled
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                const float x = 3.0f + static_cast<float>(col) * 6.0f;
                const float y = 3.0f + static_cast<float>(row) * 6.0f;
                if ((row + col) % 2 == 0)
                    c.fill({{x, y}, {x + 5.0f, y}, {x + 5.0f, y + 5.0f}, {x, y + 5.0f}});
                else
                    c.rect(x + 0.5f, y + 0.5f, x + 4.5f, y + 4.5f, 0.0f);
            }
        }
        break;
    case 5: // Scripts: </>
        c.polyline({{8.0f, 6.5f}, {2.8f, 12.0f}, {8.0f, 17.5f}}, false);
        c.polyline({{16.0f, 6.5f}, {21.2f, 12.0f}, {16.0f, 17.5f}}, false);
        c.line(14.0f, 4.5f, 10.0f, 19.5f);
        break;
    case 6: // Keybinds: a keyboard
        c.rect(2.5f, 5.5f, 21.5f, 18.5f, 2.0f);
        for (const float x : {6.5f, 10.2f, 13.8f, 17.5f}) {
            c.circle(x, 9.3f, 1.1f, true);
            c.circle(x, 12.3f, 1.1f, true);
        }
        c.line(8.0f, 15.5f, 16.0f, 15.5f);
        break;
    case 7: // About: an "i" in a circle
        c.circle(12.0f, 12.0f, 9.0f, false);
        c.circle(12.0f, 7.8f, 1.3f, true);
        c.line(12.0f, 11.2f, 12.0f, 17.2f);
        break;
    default: // unknown page: nothing
        break;
    }
}

void drawUiIcon(ImDrawList* dl, UiIcon icon, ImVec2 min, float size, ImU32 color) {
    const Canvas c(dl, min, size, color);
    switch (icon) {
    case UiIcon::Eye:
    case UiIcon::EyeOff:
        c.curve({1.5f, 12.0f}, {12.0f, 2.0f}, {22.5f, 12.0f});
        c.curve({1.5f, 12.0f}, {12.0f, 22.0f}, {22.5f, 12.0f});
        c.circle(12.0f, 12.0f, 3.3f, true);
        if (icon == UiIcon::EyeOff)
            c.line(3.5f, 20.5f, 20.5f, 3.5f);
        break;
    case UiIcon::Lock:
    case UiIcon::Unlock: {
        // A padlock: body, shackle and keyhole. Open, the right leg of the shackle is missing.
        c.fill({{5.0f, 11.0f}, {19.0f, 11.0f}, {19.0f, 21.5f}, {5.0f, 21.5f}}, 0.3f);
        c.rect(5.0f, 11.0f, 19.0f, 21.5f, 1.5f);
        c.arc(12.0f, 8.0f, 3.8f, 3.1415927f, 6.2831855f);
        c.line(8.2f, 8.0f, 8.2f, 11.0f);
        if (icon == UiIcon::Lock)
            c.line(15.8f, 8.0f, 15.8f, 11.0f);
        c.circle(12.0f, 16.2f, 1.4f, true);
        break;
    }
    }
}

} // namespace trace
