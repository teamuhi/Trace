#include "app/flow_view.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <span>
#include <unordered_set>

#include "app/pattern_draw.hpp"
#include "app/sampled_image.hpp"
#include "app/theme.hpp"
#include "core/geometry.hpp"
#include "core/i18n.hpp"
#include "core/image.hpp"

namespace trace {

// Per-frame drawing context: the draw list, the board-to-screen mapping and the resolved theme colors.
struct FlowDrawCtx {
    ImDrawList* dl = nullptr;
    ImVec2 org{}; // screen position of board (0, 0)
    float s = 1.0f; // screen units per board unit
    ImVec2 clipMin{};
    ImVec2 clipMax{};
    ImFont* font = nullptr;
    ImU32 board = 0;
    ImU32 ink = 0;
    ImU32 paper = 0;
    ImU32 handle = 0;
    ImU32 accent = 0;
    ImU32 grid = 0;

    ImVec2 scr(float x, float y) const { return ImVec2(org.x + x * s, org.y + y * s); }
    ImVec2 scr(flow::Pt p) const { return scr(p.x, p.y); }
};

namespace {

using flow::Face;
using flow::NodeId;
using flow::NodeKind;
using flow::Pt;
using flow::Rect;

constexpr float kZoomStep = 1.25f;
constexpr float kZoomPerNotch = 1.15f;
constexpr float kFitMargin = 48.0f;
constexpr float kDragPx = 4.0f;          // pointer travel before a press becomes a drag
constexpr float kHandleHalfPx = 4.0f;    // half the side of a resize handle
constexpr float kHandleHitPx = 8.0f;
constexpr float kDotRadiusPx = 4.5f;
constexpr float kDotHitPx = 9.0f;
constexpr float kDotSnapPx = 16.0f;       // while connecting, a dot this close to the pointer is taken
constexpr float kDotOffsetPx = 11.0f;    // connection dots sit this far outside the outline
constexpr float kHoverNearPx = kDotOffsetPx + kDotHitPx;
constexpr float kEdgeTolPx = 6.0f;
constexpr float kEdgeEndPx = 7.0f;
constexpr float kBorderGrabPx = 5.0f;    // a container's border is grabbable this deep inside
constexpr float kGuideSnapPx = 6.0f;
constexpr float kTextPad = 8.0f;         // board units between a node's outline and its text
constexpr float kMinTextPx = 4.0f;       // smaller text is not drawn
constexpr float kMaxTextPx = 400.0f;
constexpr std::size_t kMaxTextLines = 400;
constexpr std::size_t kMaxGuideRects = 800;
constexpr int kMaxTexturePx = 4096;
constexpr int kMaxAncestors = 16;

// The translated name of a node kind (FlowShapeXxx follow the NodeKind order in strings.def).
const char* shapeName(NodeKind kind) {
    return tr(static_cast<Str>(static_cast<int>(Str::FlowShapeRectangle) + static_cast<int>(kind)));
}
const NodeKind kPickerKinds[] = {NodeKind::Rectangle, NodeKind::Rounded,   NodeKind::Ellipse,  NodeKind::Diamond,
                                 NodeKind::Parallelogram, NodeKind::Terminator, NodeKind::Document, NodeKind::Database,
                                 NodeKind::Hexagon,   NodeKind::Circle,    NodeKind::Sticky,   NodeKind::Polygon};

const ImU32 kStickyFill = IM_COL32(255, 233, 125, 255);
const ImU32 kStickyInk = IM_COL32(52, 44, 18, 255);

// --- small helpers ------------------------------------------------------------------------------------------
ImU32 u32(const ImVec4& c) {
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImU32 u32(Rgba8 c) {
    return IM_COL32(c.r, c.g, c.b, c.a);
}

ImU32 alphaScaled(ImU32 c, float f) {
    const float a = static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFFu) * std::clamp(f, 0.0f, 1.0f);
    return (c & ~static_cast<ImU32>(IM_COL32_A_MASK)) | (static_cast<ImU32>(a + 0.5f) << IM_COL32_A_SHIFT);
}

ImU32 mixColor(ImU32 a, ImU32 b, float t) {
    const auto ch = [&](int shift) {
        const float x = static_cast<float>((a >> shift) & 0xFFu);
        const float y = static_cast<float>((b >> shift) & 0xFFu);
        return static_cast<ImU32>(std::clamp(x + (y - x) * t, 0.0f, 255.0f) + 0.5f);
    };
    return (ch(IM_COL32_R_SHIFT) << IM_COL32_R_SHIFT) | (ch(IM_COL32_G_SHIFT) << IM_COL32_G_SHIFT) |
           (ch(IM_COL32_B_SHIFT) << IM_COL32_B_SHIFT) | (ch(IM_COL32_A_SHIFT) << IM_COL32_A_SHIFT);
}

float luminance(Rgba8 c) {
    return 0.299f * static_cast<float>(c.r) + 0.587f * static_cast<float>(c.g) + 0.114f * static_cast<float>(c.b);
}

bool containsPt(const Rect& r, Pt p) {
    return p.x >= r.x && p.x <= r.right() && p.y >= r.y && p.y <= r.bottom();
}

bool overlaps(const Rect& a, const Rect& b) {
    return a.x <= b.right() && b.x <= a.right() && a.y <= b.bottom() && b.y <= a.bottom();
}

bool encloses(const Rect& outer, const Rect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.right() <= outer.right() &&
           inner.bottom() <= outer.bottom();
}

Rect inflateRect(const Rect& r, float d) {
    return Rect{r.x - d, r.y - d, r.w + 2.0f * d, r.h + 2.0f * d};
}

Rect unionRect(const Rect& a, const Rect& b) {
    const float x0 = std::min(a.x, b.x);
    const float y0 = std::min(a.y, b.y);
    const float x1 = std::max(a.right(), b.right());
    const float y1 = std::max(a.bottom(), b.bottom());
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

float distance(Pt a, Pt b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

flow::Pt sizeFor(NodeKind kind) {
    switch (kind) {
    case NodeKind::Circle: return Pt{80.0f, 80.0f};
    case NodeKind::Text: return Pt{160.0f, 40.0f};
    case NodeKind::Sticky: return Pt{140.0f, 140.0f};
    case NodeKind::Container: return Pt{360.0f, 240.0f};
    case NodeKind::Polygon: return Pt{120.0f, 120.0f};
    default: return Pt{160.0f, 80.0f};
    }
}

// A new connector gets the defaults of the Properties panel: route, arrowheads and line color.
void styleNewEdge(FlowState& state, flow::EdgeId id) {
    state.doc.setEdgeRoute(id, state.defaultRoute, state.defaultArrowStart, state.defaultArrowEnd);
    if (!state.defaultEdgeStroke)
        return;
    if (const flow::Edge* edge = state.doc.edge(id)) {
        flow::Style style = edge->style;
        style.stroke = state.defaultEdgeStroke;
        state.doc.setEdgeStyle(id, style);
    }
}

flow::Style styleFor(NodeKind kind) {
    flow::Style s;
    if (kind == NodeKind::Text)
        s.align = flow::TextAlign::Left;
    return s;
}

// A copy of `n` whose outline is pushed outward, so its face anchors are where the connection dots are drawn.
flow::Node dotNode(const flow::Node& n, float offset) {
    flow::Node d = n;
    d.rect = inflateRect(n.rect, offset);
    return d;
}

bool movesLeft(flow::ResizeHandle h) {
    return h == flow::ResizeHandle::TopLeft || h == flow::ResizeHandle::Left || h == flow::ResizeHandle::BottomLeft;
}
bool movesRight(flow::ResizeHandle h) {
    return h == flow::ResizeHandle::TopRight || h == flow::ResizeHandle::Right || h == flow::ResizeHandle::BottomRight;
}
bool movesTop(flow::ResizeHandle h) {
    return h == flow::ResizeHandle::TopLeft || h == flow::ResizeHandle::Top || h == flow::ResizeHandle::TopRight;
}
bool movesBottom(flow::ResizeHandle h) {
    return h == flow::ResizeHandle::BottomLeft || h == flow::ResizeHandle::Bottom ||
           h == flow::ResizeHandle::BottomRight;
}

ImGuiMouseCursor cursorFor(flow::ResizeHandle h) {
    switch (h) {
    case flow::ResizeHandle::TopLeft:
    case flow::ResizeHandle::BottomRight: return ImGuiMouseCursor_ResizeNWSE;
    case flow::ResizeHandle::TopRight:
    case flow::ResizeHandle::BottomLeft: return ImGuiMouseCursor_ResizeNESW;
    case flow::ResizeHandle::Top:
    case flow::ResizeHandle::Bottom: return ImGuiMouseCursor_ResizeNS;
    case flow::ResizeHandle::Left:
    case flow::ResizeHandle::Right: return ImGuiMouseCursor_ResizeEW;
    case flow::ResizeHandle::None: break;
    }
    return ImGuiMouseCursor_Arrow;
}

// Face whose anchor is nearest to `p`.
Face nearestFace(const flow::Node& n, Pt p) {
    static constexpr Face kFaces[4] = {Face::Top, Face::Right, Face::Bottom, Face::Left};
    Face best = Face::Right;
    float bestD = FLT_MAX;
    for (const Face f : kFaces) {
        const float d = distance(flow::faceAnchor(n, f).point, p);
        if (d < bestD) {
            bestD = d;
            best = f;
        }
    }
    return best;
}

// --- colors -------------------------------------------------------------------------------------------------
void resolveThemeColors(FlowDrawCtx& c) {
    const ThemeTokens& t = tokens();
    c.board = u32(t.flowBoard);
    c.ink = u32(t.flowInkAuto);
    c.paper = u32(t.flowPaperAuto);
    c.handle = u32(t.flowHandle);
    c.accent = u32(t.accent);
    c.grid = u32(t.flowGrid);
}

bool nodeHasFill(const flow::Node& n) {
    if (n.style.fill)
        return true;
    return n.kind != NodeKind::Text && n.kind != NodeKind::Image;
}

ImU32 fillOf(const FlowDrawCtx& c, const flow::Node& n) {
    if (n.style.fill)
        return u32(*n.style.fill);
    switch (n.kind) {
    case NodeKind::Sticky: return kStickyFill;
    case NodeKind::Container: return alphaScaled(c.paper, 0.45f);
    default: return c.paper;
    }
}

ImU32 strokeOf(const FlowDrawCtx& c, const flow::Node& n) {
    if (n.style.stroke)
        return u32(*n.style.stroke);
    switch (n.kind) {
    case NodeKind::Sticky: return mixColor(kStickyFill, IM_COL32(0, 0, 0, 255), 0.28f);
    case NodeKind::Container: return alphaScaled(c.ink, 0.6f);
    default: return c.ink;
    }
}

ImU32 textOf(const FlowDrawCtx& c, const flow::Node& n) {
    if (n.style.textColor)
        return u32(*n.style.textColor);
    if (n.kind == NodeKind::Sticky && !n.style.fill)
        return kStickyInk;
    // An Auto text on a color the user chose must stay readable whatever the theme.
    if (n.style.fill && n.kind != NodeKind::Container && n.kind != NodeKind::Text && n.style.fill->a > 100)
        return luminance(*n.style.fill) > 150.0f ? IM_COL32(32, 33, 36, 255) : IM_COL32(244, 244, 246, 255);
    return c.ink;
}

float strokePx(const FlowDrawCtx& c, float strokeWidth) {
    const float w = std::isfinite(strokeWidth) ? std::clamp(strokeWidth, 0.5f, 32.0f) : 2.0f;
    return std::max(1.0f, w * c.s);
}

// --- polygon and line drawing ---------------------------------------------------------------------------------
// ImGui wants the points of a filled polygon in screen-clockwise order.
void fillPolygon(ImDrawList* dl, std::vector<ImVec2>& pts, ImU32 col, bool convex) {
    if (pts.size() < 3 || (col >> IM_COL32_A_SHIFT) == 0)
        return;
    double area = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const ImVec2& a = pts[i];
        const ImVec2& b = pts[(i + 1) % pts.size()];
        area += static_cast<double>(a.x) * static_cast<double>(b.y) - static_cast<double>(b.x) * static_cast<double>(a.y);
    }
    if (area < 0.0)
        std::reverse(pts.begin(), pts.end());
    if (convex)
        dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), col);
    else
        dl->AddConcavePolyFilled(pts.data(), static_cast<int>(pts.size()), col);
}

// Dashes along a polyline. The pattern runs on across vertices; the number of dashes is capped.
void dashPolyline(ImDrawList* dl, const std::vector<ImVec2>& in, bool closed, ImU32 col, float th, float dash,
                  float gap) {
    std::vector<ImVec2> pts = in;
    if (closed && !pts.empty())
        pts.push_back(pts.front());
    if (pts.size() < 2)
        return;
    float total = 0.0f;
    for (std::size_t i = 1; i < pts.size(); ++i)
        total += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    if (!(dash > 0.0f) || !(gap > 0.0f) || !std::isfinite(total)) {
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, 0, th);
        return;
    }
    const float cycle = dash + gap;
    if (total / cycle > 3000.0f) {
        const float k = total / (cycle * 3000.0f);
        dash *= k;
        gap *= k;
    }
    std::vector<ImVec2> cur;
    const auto flush = [&]() {
        if (cur.size() >= 2)
            dl->AddPolyline(cur.data(), static_cast<int>(cur.size()), col, 0, th);
        cur.clear();
    };
    bool on = true;
    float remain = dash;
    cur.push_back(pts[0]);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const ImVec2 a = pts[i - 1];
        const ImVec2 b = pts[i];
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        if (!(len > 1.0e-6f))
            continue;
        float pos = 0.0f;
        while (len - pos > remain) {
            pos += remain;
            const float t = pos / len;
            const ImVec2 q(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
            if (on) {
                cur.push_back(q);
                flush();
            }
            on = !on;
            remain = on ? dash : gap;
            if (on)
                cur.push_back(q);
        }
        remain -= len - pos;
        if (on)
            cur.push_back(b);
    }
    flush();
}

void strokePolyline(const FlowDrawCtx& c, const std::vector<ImVec2>& pts, bool closed, ImU32 col, float th,
                    flow::Dash dash) {
    if (pts.size() < 2 || (col >> IM_COL32_A_SHIFT) == 0)
        return;
    switch (dash) {
    case flow::Dash::Dashed: dashPolyline(c.dl, pts, closed, col, th, std::max(th * 4.0f, 7.0f), std::max(th * 2.5f, 5.0f)); return;
    case flow::Dash::Dotted: dashPolyline(c.dl, pts, closed, col, th, std::max(th, 1.5f), std::max(th * 2.0f, 4.0f)); return;
    case flow::Dash::Solid: break;
    }
    c.dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, closed ? ImDrawFlags_Closed : 0, th);
}

std::vector<ImVec2> toScreenPoly(const FlowDrawCtx& c, const std::vector<Pt>& pts) {
    std::vector<ImVec2> out;
    out.reserve(pts.size());
    for (const Pt& p : pts)
        out.push_back(c.scr(p));
    return out;
}

// --- text -----------------------------------------------------------------------------------------------------
struct TextLine {
    const char* a = nullptr;
    const char* b = nullptr;
    float w = 0.0f;
};

struct TextBlock {
    std::vector<TextLine> lines;
    float lineH = 0.0f;
};

// Splits `text` into lines: at newlines, and (wrap > 0) at word boundaries so a line fits `wrap` pixels at `size`.
void layoutText(ImFont* font, float size, const std::string& text, float wrap, TextBlock& out) {
    out.lines.clear();
    out.lineH = size;
    const char* p = text.data();
    const char* const end = p + text.size();
    const auto measure = [&](const char* a, const char* b) { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, a, b).x; };
    while (out.lines.size() < kMaxTextLines) {
        const char* nl = static_cast<const char*>(std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
        const char* paraEnd = nl ? nl : end;
        if (paraEnd > p && paraEnd[-1] == '\r')
            --paraEnd;
        if (paraEnd == p) {
            out.lines.push_back(TextLine{p, p, 0.0f});
        } else {
            const char* s = p;
            while (s < paraEnd && out.lines.size() < kMaxTextLines) {
                const char* e = wrap > 0.0f ? font->CalcWordWrapPosition(size, s, paraEnd, wrap) : paraEnd;
                if (e <= s) { // a single glyph wider than the line: take it anyway
                    e = s + 1;
                    while (e < paraEnd && (static_cast<unsigned char>(*e) & 0xC0u) == 0x80u)
                        ++e;
                }
                const char* t = e;
                while (t > s && t[-1] == ' ')
                    --t;
                out.lines.push_back(TextLine{s, t, measure(s, t)});
                s = e;
                while (s < paraEnd && *s == ' ')
                    ++s;
            }
        }
        if (!nl)
            break;
        p = nl + 1;
    }
}

// Draws `text` into the screen box [aMin, aMax], clipped to it.
void drawTextBlock(const FlowDrawCtx& c, const std::string& text, ImVec2 aMin, ImVec2 aMax, float fontSize,
                   flow::TextAlign align, flow::VAlign valign, ImU32 col, bool wrap) {
    if (text.empty())
        return;
    const float size = std::min(fontSize * c.s, kMaxTextPx);
    if (!(size >= kMinTextPx) || !(aMax.x - aMin.x >= 1.0f) || !(aMax.y - aMin.y >= 1.0f))
        return;
    const float vis0 = std::max(aMin.y, c.clipMin.y);
    const float vis1 = std::min(aMax.y, c.clipMax.y);
    if (std::max(aMin.x, c.clipMin.x) >= std::min(aMax.x, c.clipMax.x) || vis0 >= vis1)
        return;

    TextBlock tb;
    layoutText(c.font, size, text, wrap ? aMax.x - aMin.x : 0.0f, tb);
    const float areaW = aMax.x - aMin.x;
    const float areaH = aMax.y - aMin.y;
    const float blockH = static_cast<float>(tb.lines.size()) * tb.lineH;
    float y = aMin.y;
    if (blockH < areaH)
        y += valign == flow::VAlign::Top ? 0.0f : (valign == flow::VAlign::Bottom ? areaH - blockH : (areaH - blockH) * 0.5f);
    c.dl->PushClipRect(aMin, aMax, true);
    for (const TextLine& ln : tb.lines) {
        if (y + tb.lineH >= vis0 && ln.b > ln.a) {
            float x = aMin.x;
            if (align == flow::TextAlign::Center)
                x = aMin.x + (areaW - ln.w) * 0.5f;
            else if (align == flow::TextAlign::Right)
                x = aMax.x - ln.w;
            c.dl->AddText(c.font, size, ImVec2(x, y), col, ln.a, ln.b);
        }
        y += tb.lineH;
        if (y > vis1)
            break;
    }
    c.dl->PopClipRect();
}

// Where a node's text goes (board units).
Rect textAreaOf(const flow::Node& n) {
    Rect r = n.rect;
    float padX = kTextPad;
    float padY = kTextPad;
    const auto shrink = [&](float f) {
        const Pt ctr = r.center();
        r.w *= f;
        r.h *= f;
        r.x = ctr.x - r.w * 0.5f;
        r.y = ctr.y - r.h * 0.5f;
    };
    switch (n.kind) {
    case NodeKind::Ellipse:
    case NodeKind::Polygon:
    case NodeKind::Circle: shrink(0.7071f); break;
    case NodeKind::Diamond: shrink(0.5f); break;
    case NodeKind::Hexagon: {
        const float k = std::min(r.w * 0.25f, r.h * 0.5f);
        r.x += k;
        r.w -= 2.0f * k;
        break;
    }
    case NodeKind::Parallelogram: {
        const float k = std::min(r.w * 0.25f, r.h * 0.4f);
        r.x += k;
        r.w -= 2.0f * k;
        break;
    }
    case NodeKind::Document: r.h -= std::min(r.h * 0.1f, 16.0f) * 2.0f; break;
    case NodeKind::Database: {
        const float cap = std::clamp(std::min(r.h * 0.15f, r.w * 0.25f), 0.0f, r.h * 0.5f);
        r.y += cap * 1.6f;
        r.h -= cap * 2.6f;
        break;
    }
    case NodeKind::Rounded: padX += std::min(n.style.cornerRadius, std::min(r.w, r.h) * 0.5f) * 0.3f; break;
    case NodeKind::Terminator: padX += r.h * 0.2f; break;
    case NodeKind::Text:
        padX = 2.0f;
        padY = 2.0f;
        break;
    default: break;
    }
    r.x += padX;
    r.y += padY;
    r.w = std::max(r.w - 2.0f * padX, 1.0f);
    r.h = std::max(r.h - 2.0f * padY, 1.0f);
    return r;
}

// --- node drawing ---------------------------------------------------------------------------------------------
void drawImageQuad(const FlowDrawCtx& c, SDL_Texture* tex, ImVec2 a, ImVec2 b) {
    // Only the visible slice: huge off-screen quads lose float precision at high zoom.
    const float w = b.x - a.x;
    const float h = b.y - a.y;
    if (!(w > 0.0f) || !(h > 0.0f))
        return;
    const ImVec2 vmin(std::max(a.x, c.clipMin.x), std::max(a.y, c.clipMin.y));
    const ImVec2 vmax(std::min(b.x, c.clipMax.x), std::min(b.y, c.clipMax.y));
    if (!(vmax.x > vmin.x) || !(vmax.y > vmin.y))
        return;
    const ImVec2 uv0((vmin.x - a.x) / w, (vmin.y - a.y) / h);
    const ImVec2 uv1((vmax.x - a.x) / w, (vmax.y - a.y) / h);
    addImageSampled(c.dl, static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(tex)), vmin, vmax, uv0, uv1, false);
}

void drawNode(const FlowDrawCtx& c, const flow::Node& n, SDL_Texture* tex, bool hideText) {
    ImDrawList* dl = c.dl;
    const ImVec2 a = c.scr(n.rect.x, n.rect.y);
    const ImVec2 b = c.scr(n.rect.right(), n.rect.bottom());
    const float th = strokePx(c, n.style.strokeWidth);
    const ImU32 fill = fillOf(c, n);
    const ImU32 stroke = strokeOf(c, n);
    const bool filled = nodeHasFill(n);

    // Too small to show any detail: a dot of the outline color keeps it findable.
    if (b.x - a.x < 3.0f && b.y - a.y < 3.0f) {
        dl->AddRectFilled(a, ImVec2(std::max(b.x, a.x + 2.0f), std::max(b.y, a.y + 2.0f)), n.style.stroke ? stroke : c.ink);
        return;
    }

    switch (n.kind) {
    case NodeKind::Text:
        if (n.style.fill)
            dl->AddRectFilled(a, b, fill);
        break;
    case NodeKind::Image: {
        if (n.style.fill)
            dl->AddRectFilled(a, b, fill);
        if (tex) {
            drawImageQuad(c, tex, a, b);
        } else {
            // Missing or not uploaded: a faint box with a cross so the node can still be found and moved.
            const ImU32 faint = alphaScaled(c.ink, 0.35f);
            dl->AddRectFilled(a, b, alphaScaled(c.ink, 0.06f));
            dl->AddRect(a, b, faint, 0.0f, 0, 1.0f);
            dl->AddLine(a, b, faint, 1.0f);
            dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), faint, 1.0f);
        }
        if (n.style.stroke)
            dl->AddRect(a, b, stroke, 0.0f, 0, th);
        return; // images carry no text
    }
    case NodeKind::Container: {
        const Rect band = flow::containerTitleBand(n.rect);
        const ImVec2 bandEnd = c.scr(band.right(), band.bottom());
        dl->AddRectFilled(a, b, fill);
        const ImU32 bandCol = n.style.fill ? mixColor(fill, IM_COL32(128, 128, 128, 255), 0.18f)
                                           : mixColor(alphaScaled(c.paper, 0.95f), c.ink, 0.12f);
        dl->AddRectFilled(a, ImVec2(b.x, bandEnd.y), bandCol);
        if (n.style.dash == flow::Dash::Solid) {
            dl->AddRect(a, b, stroke, 0.0f, 0, th);
        } else {
            strokePolyline(c, {a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y)}, true, stroke, th, n.style.dash);
        }
        dl->AddLine(ImVec2(a.x, bandEnd.y), ImVec2(b.x, bandEnd.y), alphaScaled(stroke, 0.7f), std::max(1.0f, th * 0.6f));
        if (!hideText) {
            drawTextBlock(c, n.text, ImVec2(a.x + 10.0f * c.s, a.y), ImVec2(b.x - 10.0f * c.s, bandEnd.y), n.style.fontSize,
                          n.style.align, n.style.valign, textOf(c, n), false);
        }
        return;
    }
    case NodeKind::Rectangle:
    case NodeKind::Sticky: {
        if (n.kind == NodeKind::Sticky) // a soft shadow lifts the note off the board
            dl->AddRectFilled(ImVec2(a.x + 2.0f * c.s, a.y + 4.0f * c.s), ImVec2(b.x + 2.0f * c.s, b.y + 4.0f * c.s),
                              IM_COL32(0, 0, 0, 38));
        dl->AddRectFilled(a, b, fill);
        if (n.style.dash == flow::Dash::Solid)
            dl->AddRect(a, b, stroke, 0.0f, 0, th);
        else
            strokePolyline(c, {a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y)}, true, stroke, th, n.style.dash);
        break;
    }
    default: {
        std::vector<ImVec2> pts = toScreenPoly(c, flow::nodeOutline(n));
        if (filled)
            fillPolygon(dl, pts, fill, n.kind != NodeKind::Document);
        strokePolyline(c, pts, true, stroke, th, n.style.dash);
        if (n.kind == NodeKind::Database)
            strokePolyline(c, toScreenPoly(c, flow::databaseRim(n.rect)), false, stroke, th, n.style.dash);
        break;
    }
    }

    if (!hideText && !n.text.empty()) {
        const Rect ta = textAreaOf(n);
        drawTextBlock(c, n.text, c.scr(ta.x, ta.y), c.scr(ta.right(), ta.bottom()), n.style.fontSize, n.style.align,
                      n.style.valign, textOf(c, n), true);
    }
}

// --- edge drawing ----------------------------------------------------------------------------------------------
std::vector<Pt> trimPolyline(const std::vector<Pt>& in, float head, float tail) {
    std::vector<Pt> p = in;
    const float total = flow::polylineLength(p);
    if (!(head > 0.0f || tail > 0.0f) || !(total > (head + tail) * 1.2f + 1.0f))
        return p;
    const auto trimBack = [](std::vector<Pt>& v, float len) {
        while (len > 0.0f && v.size() >= 2) {
            const Pt a = v[v.size() - 2];
            const Pt b = v.back();
            const float l = distance(a, b);
            if (l > len) {
                const float t = len / l;
                v.back() = Pt{b.x + (a.x - b.x) * t, b.y + (a.y - b.y) * t};
                return;
            }
            len -= l;
            v.pop_back();
        }
    };
    trimBack(p, tail);
    std::reverse(p.begin(), p.end());
    trimBack(p, head);
    std::reverse(p.begin(), p.end());
    return p;
}

void drawArrowHead(const FlowDrawCtx& c, const flow::ArrowHead& h, ImU32 col, float th) {
    if (h.points.size() < 2)
        return;
    std::vector<ImVec2> pts = toScreenPoly(c, h.points);
    if (h.filled && h.closed)
        fillPolygon(c.dl, pts, col, true);
    else
        c.dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, h.closed ? ImDrawFlags_Closed : 0, th);
}

void drawEdgeLine(const FlowDrawCtx& c, const flow::Edge& e, const flow::EdgePath& path, bool selected,
                  float alphaFactor) {
    if (path.points.size() < 2)
        return;
    const ImU32 col = alphaScaled(e.style.stroke ? u32(*e.style.stroke) : c.ink, alphaFactor);
    const float sw = std::isfinite(e.style.strokeWidth) ? std::clamp(e.style.strokeWidth, 0.5f, 32.0f) : 2.0f;
    const float th = strokePx(c, sw);
    const flow::ArrowHead hs = flow::arrowHeadFor(path, true, e.arrowStart, sw);
    const flow::ArrowHead he = flow::arrowHeadFor(path, false, e.arrowEnd, sw);
    const std::vector<ImVec2> sp = toScreenPoly(c, trimPolyline(path.points, hs.inset, he.inset));
    if (selected)
        c.dl->AddPolyline(sp.data(), static_cast<int>(sp.size()), alphaScaled(c.handle, 0.35f), 0, th + 6.0f);
    strokePolyline(c, sp, false, col, th, e.style.dash);
    drawArrowHead(c, hs, col, th);
    drawArrowHead(c, he, col, th);
}

// A connector label's box in board units: where it sits (see flow::placeLabel) and its half size.
struct LabelBox {
    flow::LabelPlacement place;
    float halfW = 0.0f;
    float halfH = 0.0f;
};

LabelBox labelBoxOf(const flow::Edge& e, const flow::EdgePath& path, ImFont* font) {
    const float size = std::clamp(e.style.fontSize, 6.0f, 200.0f);
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, e.label.c_str(), e.label.c_str() + e.label.size());
    LabelBox box;
    box.halfW = sz.x * 0.5f + 4.0f;
    box.halfH = sz.y * 0.5f + 4.0f;
    box.place = flow::placeLabel(path.points, e.labelPos, e.labelAlong, e.labelSide, box.halfW, box.halfH);
    return box;
}

// Whether board point `p` is inside the (possibly turned) label box.
bool labelContains(const LabelBox& box, Pt p) {
    const float dx = p.x - box.place.center.x;
    const float dy = p.y - box.place.center.y;
    const float ca = std::cos(box.place.angle);
    const float sa = std::sin(box.place.angle);
    const float lx = dx * ca + dy * sa;
    const float ly = -dx * sa + dy * ca;
    return std::abs(lx) <= box.halfW && std::abs(ly) <= box.halfH;
}

// Adds text like ImDrawList::AddText, then turns the vertices it produced about `pivot` (ImGui has no rotated text).
void addTextTurned(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* a, const char* b,
                   ImVec2 pivot, float angle) {
    const int first = dl->VtxBuffer.Size;
    dl->AddText(font, size, pos, col, a, b);
    if (angle == 0.0f)
        return;
    const float ca = std::cos(angle);
    const float sa = std::sin(angle);
    for (int i = first; i < dl->VtxBuffer.Size; ++i) {
        ImVec2& q = dl->VtxBuffer[i].pos;
        const float dx = q.x - pivot.x;
        const float dy = q.y - pivot.y;
        q.x = pivot.x + dx * ca - dy * sa;
        q.y = pivot.y + dx * sa + dy * ca;
    }
}

void drawEdgeLabel(const FlowDrawCtx& c, const flow::Edge& e, const flow::EdgePath& path) {
    if (e.label.empty())
        return;
    const float size = std::min(e.style.fontSize * c.s, kMaxTextPx);
    if (!(size >= kMinTextPx))
        return;
    TextBlock tb;
    layoutText(c.font, size, e.label, 0.0f, tb);
    float w = 0.0f;
    for (const TextLine& ln : tb.lines)
        w = std::max(w, ln.w);
    const float h = static_cast<float>(tb.lines.size()) * tb.lineH;
    const LabelBox box = labelBoxOf(e, path, c.font);
    const ImVec2 ctr = c.scr(box.place.center);
    const float pad = std::max(2.0f, 4.0f * c.s);
    const float reach = std::hypot(w, h) * 0.5f + pad;
    if (ctr.x + reach < c.clipMin.x || ctr.x - reach > c.clipMax.x || ctr.y + reach < c.clipMin.y ||
        ctr.y - reach > c.clipMax.y)
        return;

    const float angle = box.place.angle;
    const float ca = std::cos(angle);
    const float sa = std::sin(angle);
    if (e.labelSide == flow::LabelSide::On) {
        // The label interrupts the line: a board-colored plate behind the text, turned with it.
        const float hw = w * 0.5f + pad;
        const float hh = h * 0.5f + pad;
        const float xs[4] = {-hw, hw, hw, -hw};
        const float ys[4] = {-hh, -hh, hh, hh};
        ImVec2 quad[4];
        for (int i = 0; i < 4; ++i)
            quad[i] = ImVec2(ctr.x + xs[i] * ca - ys[i] * sa, ctr.y + xs[i] * sa + ys[i] * ca);
        c.dl->AddConvexPolyFilled(quad, 4, alphaScaled(c.board, 0.92f));
    }
    const ImU32 col = e.style.textColor ? u32(*e.style.textColor) : c.ink;
    float y = ctr.y - h * 0.5f;
    for (const TextLine& ln : tb.lines) {
        if (ln.b > ln.a)
            addTextTurned(c.dl, c.font, size, ImVec2(ctr.x - ln.w * 0.5f, y), col, ln.a, ln.b, ctr, angle);
        y += tb.lineH;
    }
}

// A small preview of a node kind (the shape picker rows).
void drawKindIcon(ImDrawList* dl, NodeKind kind, ImVec2 mn, ImVec2 mx, ImU32 col) {
    const float w = mx.x - mn.x;
    const float h = mx.y - mn.y;
    if (!(w > 1.0f) || !(h > 1.0f))
        return;
    if (kind == NodeKind::Sticky) {
        dl->AddRectFilled(mn, mx, alphaScaled(kStickyFill, 0.9f));
        dl->AddRect(mn, mx, col, 0.0f, 0, 1.2f);
        return;
    }
    const std::vector<Pt> poly = flow::outlineFor(kind, Rect{0.0f, 0.0f, w, h}, 4.0f);
    std::vector<ImVec2> pts;
    pts.reserve(poly.size());
    for (const Pt& p : poly)
        pts.push_back(ImVec2(mn.x + p.x, mn.y + p.y));
    dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, ImDrawFlags_Closed, 1.3f);
    if (kind == NodeKind::Database) {
        std::vector<ImVec2> rim;
        for (const Pt& p : flow::databaseRim(Rect{0.0f, 0.0f, w, h}))
            rim.push_back(ImVec2(mn.x + p.x, mn.y + p.y));
        dl->AddPolyline(rim.data(), static_cast<int>(rim.size()), col, 0, 1.3f);
    }
}

// Dots on a pattern that fades out when zoomed far out.
void drawGrid(const FlowDrawCtx& c, const FlowState& st, ImVec2 p0, ImVec2 p1) {
    if (!st.showGrid) {
        c.dl->AddRectFilled(p0, p1, c.board);
        return;
    }
    const float grid = std::isfinite(st.gridSize) ? std::clamp(st.gridSize, 2.0f, 1000.0f) : 20.0f;
    const float raw = grid * c.s;
    if (!(raw >= 3.0f)) {
        c.dl->AddRectFilled(p0, p1, c.board);
        return;
    }
    const float fade = std::clamp((raw - 3.0f) / 7.0f, 0.0f, 1.0f);
    float step = raw;
    for (int i = 0; i < 24 && step < 10.0f; ++i)
        step *= 2.0f;
    const ImU32 marks = alphaScaled(c.grid, fade);
    drawPattern(c.dl, PatternStyle::Dots, p0, p1, c.org, step, c.board, marks, marks);
}

// Draws a node clipped to the bodies of its containers (a child never spills out of the container it sits in).
// Shared by the board and by the export.
void drawNodeClipped(const FlowDrawCtx& c, const flow::FlowDocument& doc, const flow::Node& n, SDL_Texture* tex,
                     bool hideText) {
    bool clipped = false;
    if (n.parentId != flow::kNoNode) {
        ImVec2 cmin = c.clipMin;
        ImVec2 cmax = c.clipMax;
        flow::NodeId parent = n.parentId;
        for (int depth = 0; parent != flow::kNoNode && depth < kMaxAncestors; ++depth) {
            const flow::Node* anc = doc.node(parent);
            if (!anc)
                break;
            const Rect band = flow::containerTitleBand(anc->rect);
            const ImVec2 a = c.scr(anc->rect.x, anc->rect.y + band.h);
            const ImVec2 b = c.scr(anc->rect.right(), anc->rect.bottom());
            cmin = ImVec2(std::max(cmin.x, a.x), std::max(cmin.y, a.y));
            cmax = ImVec2(std::min(cmax.x, b.x), std::min(cmax.y, b.y));
            parent = anc->parentId;
        }
        if (!(cmax.x > cmin.x) || !(cmax.y > cmin.y))
            return;
        c.dl->PushClipRect(cmin, cmax, true);
        clipped = true;
    }
    drawNode(c, n, tex, hideText);
    if (clipped)
        c.dl->PopClipRect();
}

// Reads an ImGui text buffer into / out of a std::string.
int resizeStringCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}

// Reduces an image so no side exceeds `maxSide` (box average, alpha weighted).
Image downscaled(const Image& src, int maxSide) {
    const int factor = (std::max(src.width(), src.height()) + maxSide - 1) / maxSide;
    const int nw = std::max(1, (src.width() + factor - 1) / factor);
    const int nh = std::max(1, (src.height() + factor - 1) / factor);
    Image out(nw, nh);
    for (int y = 0; y < nh; ++y) {
        for (int x = 0; x < nw; ++x) {
            unsigned long long r = 0, g = 0, bl = 0, al = 0, count = 0;
            const int y1 = std::min(src.height(), (y + 1) * factor);
            const int x1 = std::min(src.width(), (x + 1) * factor);
            for (int sy = y * factor; sy < y1; ++sy) {
                const std::span<const Rgba8> row = src.row(sy);
                for (int sx = x * factor; sx < x1; ++sx) {
                    const Rgba8 p = row[static_cast<std::size_t>(sx)];
                    r += static_cast<unsigned long long>(p.r) * p.a;
                    g += static_cast<unsigned long long>(p.g) * p.a;
                    bl += static_cast<unsigned long long>(p.b) * p.a;
                    al += p.a;
                    ++count;
                }
            }
            Rgba8 px;
            if (al > 0 && count > 0) {
                px.r = static_cast<std::uint8_t>(r / al);
                px.g = static_cast<std::uint8_t>(g / al);
                px.b = static_cast<std::uint8_t>(bl / al);
                px.a = static_cast<std::uint8_t>(al / count);
            }
            out.set(x, y, px);
        }
    }
    return out;
}

} // namespace

// === lifetime and view ======================================================================================

void FlowView::release() noexcept {
    for (auto& entry : images_)
        if (entry.second)
            entry.second->mirror.release();
    images_.clear();
}

flow::Pt FlowView::toBoard(ImVec2 screen) const noexcept {
    const Vec2f v = view_.toImage(Vec2f{screen.x - p0_.x, screen.y - p0_.y});
    return Pt{v.x, v.y};
}

ImVec2 FlowView::toScreen(flow::Pt p) const noexcept {
    const Vec2f v = view_.toView(Vec2f{p.x, p.y});
    return ImVec2(p0_.x + v.x, p0_.y + v.y);
}

float FlowView::pxToBoard(float px) const noexcept {
    const float s = view_.scale();
    return s > 0.0f ? px / s : px;
}

flow::Rect FlowView::visibleBoard(float marginPx) const noexcept {
    const Pt a = toBoard(ImVec2(p0_.x - marginPx, p0_.y - marginPx));
    const Pt b = toBoard(ImVec2(p1_.x + marginPx, p1_.y + marginPx));
    return Rect{a.x, a.y, b.x - a.x, b.y - a.y};
}

flow::Pt FlowView::viewCenter() const noexcept {
    if (!hasView_)
        return Pt{};
    const Vec2f v = view_.toImage(Vec2f{viewSize_.x * 0.5f, viewSize_.y * 0.5f});
    return Pt{v.x, v.y};
}

flow::Pt FlowView::pointerOrCenter() const noexcept {
    return pointerInside_ ? pointerBoard_ : viewCenter();
}

void FlowView::zoomIn() {
    view_.setZoomLimits(0.05f, 8.0f);
    view_.zoomByFactorAt(Vec2f{viewSize_.x * 0.5f, viewSize_.y * 0.5f}, kZoomStep);
    fitMode_ = false;
}

void FlowView::zoomOut() {
    view_.setZoomLimits(0.05f, 8.0f);
    view_.zoomByFactorAt(Vec2f{viewSize_.x * 0.5f, viewSize_.y * 0.5f}, 1.0f / kZoomStep);
    fitMode_ = false;
}

void FlowView::actualSize() {
    view_.setZoomLimits(0.05f, 8.0f);
    view_.zoomAt(Vec2f{viewSize_.x * 0.5f, viewSize_.y * 0.5f}, 1.0f);
    fitMode_ = false;
}

void FlowView::fit(const FlowState&) {
    fitPending_ = true;
    fitMode_ = true;
}

void FlowView::applyFit(const FlowState& state) {
    view_.setZoomLimits(0.05f, 8.0f);
    bool any = false;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    const auto add = [&](float ax, float ay, float bx, float by) {
        if (!any) {
            x0 = ax;
            y0 = ay;
            x1 = bx;
            y1 = by;
            any = true;
        } else {
            x0 = std::min(x0, ax);
            y0 = std::min(y0, ay);
            x1 = std::max(x1, bx);
            y1 = std::max(y1, by);
        }
    };
    for (const flow::Node& n : state.doc.nodes())
        add(n.rect.x, n.rect.y, n.rect.right(), n.rect.bottom());
    for (const flow::Edge& e : state.doc.edges()) {
        for (const flow::End* end : {&e.from, &e.to})
            if (end->node == flow::kNoNode)
                add(end->point.x, end->point.y, end->point.x, end->point.y);
    }
    if (!any) // an empty board centers the origin
        view_.fitRect(viewSize_, 0.0f, 0.0f, 0.0f, 0.0f, kFitMargin);
    else
        view_.fitRect(viewSize_, x0, y0, x1, y1, kFitMargin);
    fitSize_ = viewSize_;
}

void FlowView::handleWheel(bool hovered) {
    if (!hovered)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    const float wheel = std::clamp(io.MouseWheel, -10.0f, 10.0f);
    const float wheelH = std::clamp(io.MouseWheelH, -10.0f, 10.0f);
    if (wheel != 0.0f && io.KeyCtrl) {
        view_.zoomByFactorAt(Vec2f{io.MousePos.x - p0_.x, io.MousePos.y - p0_.y}, std::pow(kZoomPerNotch, wheel));
        fitMode_ = false;
    } else if (wheel != 0.0f || wheelH != 0.0f) {
        const float step = ImGui::GetFontSize() * 3.0f;
        float dx = wheelH;
        float dy = wheel;
        if (io.KeyShift)
            std::swap(dx, dy);
        view_.panBy(dx * step, dy * step);
        fitMode_ = false;
    }
}

// === state queries and requests ===============================================================================

bool FlowView::gestureActive() const noexcept {
    return gesture_.mode != Mode::None || editor_.open || picker_.open || picker_.requestOpen;
}

bool FlowView::textEditing() const noexcept {
    return editor_.open;
}

void FlowView::reset() {
    if (histDepth_ > 0)
        abandon_ = true;
    gesture_ = Gesture{};
    hover_ = Hover{};
    editor_ = Editor{};
    picker_ = Picker{};
    enteredGroup_ = flow::kNoGroup;
    pendingEditNode_ = flow::kNoNode;
    pendingEditEdge_ = flow::kNoEdge;
    pendingFresh_ = false;
}

void FlowView::beginEditNode(flow::NodeId id) {
    pendingEditNode_ = id;
    pendingEditEdge_ = flow::kNoEdge;
    pendingFresh_ = false;
}

void FlowView::beginEditEdge(flow::EdgeId id) {
    pendingEditEdge_ = id;
    pendingEditNode_ = flow::kNoNode;
    pendingFresh_ = false;
}

// === history gestures ===========================================================================================

void FlowView::beginHist(FlowState& state) {
    state.history.beginGesture();
    ++histDepth_;
}

void FlowView::endHist(FlowState& state) {
    if (histDepth_ <= 0)
        return;
    --histDepth_;
    state.history.endGesture(state.doc);
}

void FlowView::cancelHist(FlowState& state) {
    if (histDepth_ <= 0)
        return;
    histDepth_ = 0;
    if (const flow::FlowDocument* committed = state.history.cancelGesture())
        state.replaceDocument(*committed);
}

// === per-frame preparation ======================================================================================

void FlowView::prepareFrame(FlowState& state) {
    zOrder_ = state.doc.nodesByZ();
    visible_ = visibleBoard(4.0f);
    visEdges_.clear();
    const flow::NodeLookup lookup = [&](flow::NodeId id) { return state.doc.node(id); };
    constexpr float kEdgeMargin = 260.0f; // curves and elbow detours stay within this of their end boxes
    for (const flow::Edge& e : state.doc.edges()) {
        bool ok = true;
        bool first = true;
        Rect bb;
        for (const flow::End* end : {&e.from, &e.to}) {
            Rect r;
            if (end->node != flow::kNoNode) {
                const flow::Node* n = state.doc.node(end->node);
                if (!n) {
                    ok = false;
                    break;
                }
                r = n->rect;
            } else {
                r = Rect{end->point.x, end->point.y, 0.0f, 0.0f};
            }
            bb = first ? r : unionRect(bb, r);
            first = false;
        }
        if (!ok || !overlaps(inflateRect(bb, kEdgeMargin), visible_))
            continue;
        if (std::optional<flow::EdgePath> path = flow::routeEdge(e, lookup))
            visEdges_.push_back(EdgeDraw{e.id, std::move(*path)});
    }
    // Textures of images that left the document are dropped.
    for (auto it = images_.begin(); it != images_.end();) {
        if (!state.doc.image(it->first))
            it = images_.erase(it);
        else
            ++it;
    }
}

SDL_Texture* FlowView::imageTexture(const flow::FlowDocument& doc, flow::ImageId id) {
    if (!renderer_ || id == flow::kNoImage)
        return nullptr;
    const flow::ImagePtr img = doc.image(id);
    if (!img || img->empty())
        return nullptr;
    auto it = images_.find(id);
    if (it == images_.end()) {
        auto entry = std::make_unique<ImageTex>();
        entry->mirror.init(renderer_);
        it = images_.emplace(id, std::move(entry)).first;
    }
    ImageTex& e = *it->second;
    if (e.src != img) {
        e.src = img;
        if (std::max(img->width(), img->height()) <= kMaxTexturePx) {
            e.mirror.sync(*img, IRect{0, 0, img->width(), img->height()});
        } else {
            const Image small = downscaled(*img, kMaxTexturePx);
            e.mirror.sync(small, IRect{0, 0, small.width(), small.height()});
        }
        e.mirror.setNearest(false);
    }
    return e.mirror.texture();
}

// === picking ====================================================================================================

bool FlowView::nodeHit(const FlowState& state, const flow::Node& n, flow::Pt p, float tolerance) const {
    if (n.kind == NodeKind::Container) {
        // A container is only grabbed by its title band and its border, so the inside can be rubber-banded.
        const Rect& r = n.rect;
        const float dEdge = std::min(std::min(p.x - r.x, r.right() - p.x), std::min(p.y - r.y, r.bottom() - p.y));
        if (dEdge < -tolerance)
            return false;
        if (dEdge >= 0.0f && !flow::hitContainerTitle(n, p) && dEdge > std::max(tolerance, pxToBoard(kBorderGrabPx)))
            return false;
    } else if (!flow::hitNode(n, p, tolerance)) {
        return false;
    }
    if (tolerance <= 0.0f) {
        // A child only exists inside its container's body.
        flow::NodeId parent = n.parentId;
        for (int depth = 0; parent != flow::kNoNode && depth < kMaxAncestors; ++depth) {
            const flow::Node* anc = state.doc.node(parent);
            if (!anc)
                break;
            const Rect band = flow::containerTitleBand(anc->rect);
            const Rect body{anc->rect.x, anc->rect.y + band.h, anc->rect.w, std::max(0.0f, anc->rect.h - band.h)};
            if (!containsPt(body, p))
                return false;
            parent = anc->parentId;
        }
    }
    return true;
}

FlowView::PickResult FlowView::pickNode(const FlowState& state, flow::Pt p, float nearTolerance) const {
    PickResult r;
    for (auto it = zOrder_.rbegin(); it != zOrder_.rend(); ++it) {
        const flow::Node* n = state.doc.node(*it);
        if (!n)
            continue;
        if (nodeHit(state, *n, p, 0.0f)) {
            r.body = n->id;
            if (r.nearby == flow::kNoNode)
                r.nearby = n->id;
            return r;
        }
        if (nearTolerance > 0.0f && r.nearby == flow::kNoNode && nodeHit(state, *n, p, nearTolerance))
            r.nearby = n->id;
    }
    return r;
}

const flow::EdgePath* FlowView::pathOf(flow::EdgeId id) const {
    for (const EdgeDraw& e : visEdges_)
        if (e.id == id)
            return &e.path;
    return nullptr;
}

bool FlowView::labelHit(const flow::Edge& e, const flow::EdgePath& path, flow::Pt p) const {
    return !e.label.empty() && labelContains(labelBoxOf(e, path, ImGui::GetFont()), p);
}

bool FlowView::onBendHandle(flow::EdgeId id, flow::Pt p) const {
    const flow::EdgePath* path = pathOf(id);
    if (!path || !path->hasHandle)
        return false;
    const float r = pxToBoard(kEdgeEndPx + 2.0f);
    return std::hypot(p.x - path->handle.x, p.y - path->handle.y) <= r;
}

flow::EdgeId FlowView::pickEdge(const FlowState& state, flow::Pt p) const {
    const float tol = pxToBoard(kEdgeTolPx);
    for (auto it = visEdges_.rbegin(); it != visEdges_.rend(); ++it) {
        const flow::Edge* e = state.doc.edge(it->id);
        if (!e)
            continue;
        if (flow::hitEdge(it->path, p, tol))
            return e->id;
        if (labelHit(*e, it->path, p))
            return e->id;
    }
    return flow::kNoEdge;
}

flow::NodeId FlowView::containerAt(const FlowState& state, flow::Pt p,
                                   const std::vector<flow::NodeId>& sortedExclude) const {
    for (auto it = zOrder_.rbegin(); it != zOrder_.rend(); ++it) {
        const flow::Node* n = state.doc.node(*it);
        if (!n || n->kind != NodeKind::Container)
            continue;
        if (std::binary_search(sortedExclude.begin(), sortedExclude.end(), n->id))
            continue;
        if (containsPt(n->rect, p))
            return n->id;
    }
    return flow::kNoNode;
}

FlowView::DropTarget FlowView::resolveDrop(const FlowState& state, flow::Pt p) const {
    const PickResult r = pickNode(state, p, pxToBoard(kHoverNearPx));
    const flow::NodeId host = r.body != flow::kNoNode ? r.body : r.nearby;
    const flow::Node* n = host != flow::kNoNode ? state.doc.node(host) : nullptr;
    if (!n)
        return {};
    // While connecting the dots sit on the outline, exactly where the connector will end. A dot near the pointer is
    // taken; anywhere else on (or just beside) the shape locks to the nearest dot, so a connector always ends in a dot.
    if (const std::optional<Face> f = flow::hitFaceHandle(*n, p, pxToBoard(kDotSnapPx)))
        return DropTarget{host, *f, true};
    return DropTarget{host, nearestFace(*n, p), false};
}

void FlowView::updateHover(const FlowState& state, flow::Pt p) {
    hover_ = Hover{};
    if (state.tool != FlowTool::Select && state.tool != FlowTool::Connector)
        return;
    const PickResult r = pickNode(state, p, pxToBoard(kHoverNearPx));
    hover_.body = r.body;
    hover_.host = r.body != flow::kNoNode ? r.body : r.nearby;
    if (const flow::Node* h = state.doc.node(hover_.host)) {
        const flow::Node dn = dotNode(*h, pxToBoard(kDotOffsetPx));
        if (const std::optional<Face> f = flow::hitFaceHandle(dn, p, pxToBoard(kDotHitPx))) {
            hover_.onFace = true;
            hover_.face = *f;
        }
    }
    if (state.tool != FlowTool::Select)
        return;
    if (state.selNodes.size() == 1 && state.selEdges.empty()) {
        if (const flow::Node* sel = state.doc.node(state.selNodes[0]))
            hover_.handle = flow::hitResizeHandle(sel->rect, p, pxToBoard(kHandleHitPx));
    }
    if (state.selEdges.size() == 1 && state.selNodes.empty()) {
        if (const flow::EdgePath* path = pathOf(state.selEdges[0]))
            hover_.onEdgeEnd = flow::hitEdgeEndpoint(*path, p, pxToBoard(kEdgeEndPx)) != flow::EdgeEnd::None;
        if (!hover_.onEdgeEnd)
            hover_.onBend = onBendHandle(state.selEdges[0], p);
    }
    if (hover_.body == flow::kNoNode)
        hover_.edge = pickEdge(state, p);
}

// === selection ==================================================================================================

std::vector<flow::NodeId> FlowView::selectionUnit(const FlowState& state, flow::NodeId id) const {
    const flow::Node* n = state.doc.node(id);
    if (!n)
        return {};
    if (n->groupId != flow::kNoGroup && n->groupId != enteredGroup_) {
        std::vector<flow::NodeId> members = state.doc.groupMembers(n->groupId);
        if (!members.empty())
            return members;
    }
    return {id};
}

void FlowView::selectNode(FlowState& state, flow::NodeId id) {
    const std::vector<flow::NodeId> unit = selectionUnit(state, id);
    state.clearSelection();
    state.selNodes = unit;
}

void FlowView::toggleNode(FlowState& state, flow::NodeId id) {
    const std::vector<flow::NodeId> unit = selectionUnit(state, id);
    if (state.isNodeSelected(id)) {
        state.selNodes.erase(std::remove_if(state.selNodes.begin(), state.selNodes.end(),
                                            [&](flow::NodeId s) {
                                                return std::find(unit.begin(), unit.end(), s) != unit.end();
                                            }),
                             state.selNodes.end());
    } else {
        for (const flow::NodeId m : unit)
            if (!state.isNodeSelected(m))
                state.selNodes.push_back(m);
    }
}

// === main entry =================================================================================================

void FlowView::draw(FlowState& state) {
    view_.setZoomLimits(0.05f, 8.0f);
    const ImGuiIO& io = ImGui::GetIO();

    // The Image tool is a one-shot request for the workspace's import dialog.
    if (state.tool == FlowTool::Image) {
        state.importImageRequested = true;
        state.tool = FlowTool::Select;
    }
    // reset() while a history gesture was open: roll back what that gesture changed.
    if (abandon_) {
        abandon_ = false;
        if (histDepth_ > 0 && state.history.gestureActive())
            cancelHist(state);
        histDepth_ = 0;
    }
    // The history was reset under us (new or opened document): the gesture and editor are void.
    if (histDepth_ > 0 && !state.history.gestureActive()) {
        histDepth_ = 0;
        gesture_ = Gesture{};
        editor_ = Editor{};
    }
    state.pruneSelection();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (!(avail.x >= 1.0f) || !(avail.y >= 1.0f)) {
        // No room: nothing can be finished here, so settle whatever is open.
        abortGesture(state);
        if (editor_.open)
            closeEditor(state, true);
        picker_ = Picker{};
        hover_ = Hover{};
        pointerInside_ = false;
        return;
    }

    p0_ = origin;
    p1_ = ImVec2(origin.x + avail.x, origin.y + avail.y);
    viewSize_ = Vec2f{avail.x, avail.y};
    hasView_ = true;
    view_.setDevicePixelRatio(io.DisplayFramebufferScale.x);

    if (refocus_) {
        ImGui::SetWindowFocus();
        refocus_ = false;
    }

    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##flowboard", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();

    if (fitPending_ || (fitMode_ && (viewSize_.x != fitSize_.x || viewSize_.y != fitSize_.y))) {
        applyFit(state);
        fitPending_ = false;
    }
    handleWheel(hovered);

    const bool mouseValid = ImGui::IsMousePosValid(&io.MousePos);
    pointerInside_ = hovered && mouseValid;
    if (mouseValid)
        pointerBoard_ = toBoard(io.MousePos);

    prepareFrame(state);

    // Edits asked for through beginEditNode / beginEditEdge or by a tool that just made a node.
    if (pendingEditNode_ != flow::kNoNode || pendingEditEdge_ != flow::kNoEdge) {
        const bool same = editor_.open && ((pendingEditNode_ != flow::kNoNode && !editor_.isEdge && editor_.node == pendingEditNode_) ||
                                           (pendingEditEdge_ != flow::kNoEdge && editor_.isEdge && editor_.edge == pendingEditEdge_));
        if (!same)
            openEditor(state);
        pendingEditNode_ = flow::kNoNode;
        pendingEditEdge_ = flow::kNoEdge;
        pendingFresh_ = false;
    }

    routeInput(state, hovered);
    handleKeys(state);

    // Drawing.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0_, p1_, true);
    FlowDrawCtx c;
    c.dl = dl;
    const Vec2f o = view_.toView(Vec2f{0.0f, 0.0f});
    c.org = ImVec2(p0_.x + o.x, p0_.y + o.y);
    c.s = view_.scale();
    c.clipMin = p0_;
    c.clipMax = p1_;
    c.font = ImGui::GetFont();
    resolveThemeColors(c);

    drawGrid(c, state, p0_, p1_);
    drawBoard(state, c);
    drawOverlays(state, c);
    drawGestureOverlay(state, c);
    dl->PopClipRect();

    drawEditor(state);
    drawPicker(state);
    drawEdgeMenu(state);
}

// === drawing ====================================================================================================

void FlowView::drawBoard(FlowState& state, const FlowDrawCtx& c) {
    for (const flow::NodeId id : zOrder_) {
        const flow::Node* n = state.doc.node(id);
        if (!n || !overlaps(n->rect, visible_))
            continue;
        const bool editing = editor_.open && !editor_.isEdge && editor_.node == id;
        SDL_Texture* tex = n->kind == NodeKind::Image ? imageTexture(state.doc, n->imageId) : nullptr;
        drawNodeClipped(c, state.doc, *n, tex, editing);
    }

    const flow::EdgeId ghost = gesture_.mode == Mode::Reconnect && gesture_.dragging ? gesture_.edge : flow::kNoEdge;
    for (const EdgeDraw& ed : visEdges_) {
        const flow::Edge* e = state.doc.edge(ed.id);
        if (!e)
            continue;
        const bool selected = state.isEdgeSelected(ed.id);
        drawEdgeLine(c, *e, ed.path, selected, ed.id == ghost ? 0.3f : 1.0f);
        if (!(editor_.open && editor_.isEdge && editor_.edge == ed.id))
            drawEdgeLabel(c, *e, ed.path);
    }
}

// === export =====================================================================================================

namespace {

// Whether an export of `only` (empty = all) includes this connector: both its ends must be shapes that are in.
bool exportsEdge(const flow::Edge& e, bool all, const std::unordered_set<NodeId>& keep) {
    return all || (e.from.node != flow::kNoNode && e.to.node != flow::kNoNode && keep.count(e.from.node) != 0 &&
                   keep.count(e.to.node) != 0);
}

float lineWidthOf(float strokeWidth) {
    return std::isfinite(strokeWidth) ? std::clamp(strokeWidth, 0.5f, 32.0f) : 2.0f;
}

} // namespace

bool FlowView::exportBounds(const flow::FlowDocument& doc, std::span<const flow::NodeId> only, flow::Rect& out) {
    const bool all = only.empty();
    const std::unordered_set<NodeId> keep(only.begin(), only.end());
    bool any = false;
    const auto add = [&](const Rect& r) {
        out = any ? unionRect(out, r) : r;
        any = true;
    };
    const auto addPoint = [&](Pt p, float reach) { add(Rect{p.x - reach, p.y - reach, 2.0f * reach, 2.0f * reach}); };

    for (const flow::Node& n : doc.nodes()) {
        if (!all && keep.count(n.id) == 0)
            continue;
        Rect r = inflateRect(n.rect, lineWidthOf(n.style.strokeWidth) * 0.5f);
        if (n.kind == NodeKind::Sticky) { // its shadow (see drawNode) falls down and to the right
            r.w += 2.0f;
            r.h += 4.0f;
        }
        add(r);
    }

    const flow::NodeLookup lookup = [&](flow::NodeId id) { return doc.node(id); };
    ImFont* const font = ImGui::GetFont();
    for (const flow::Edge& e : doc.edges()) {
        if (!exportsEdge(e, all, keep))
            continue;
        const std::optional<flow::EdgePath> path = flow::routeEdge(e, lookup);
        if (!path || path->points.size() < 2)
            continue;
        const float sw = lineWidthOf(e.style.strokeWidth);
        for (const Pt& p : path->points)
            addPoint(p, sw * 0.5f);
        for (const bool atStart : {true, false}) {
            const flow::ArrowHead h = flow::arrowHeadFor(*path, atStart, atStart ? e.arrowStart : e.arrowEnd, sw);
            for (const Pt& p : h.points)
                addPoint(p, sw * 0.5f);
        }
        if (!e.label.empty() && font) {
            // The four corners of the (possibly turned) label box.
            const LabelBox box = labelBoxOf(e, *path, font);
            const float ca = std::cos(box.place.angle);
            const float sa = std::sin(box.place.angle);
            for (const float sx : {-1.0f, 1.0f}) {
                for (const float sy : {-1.0f, 1.0f}) {
                    const float lx = sx * box.halfW;
                    const float ly = sy * box.halfH;
                    addPoint(Pt{box.place.center.x + lx * ca - ly * sa, box.place.center.y + lx * sa + ly * ca}, 0.0f);
                }
            }
        }
    }
    return any;
}

void FlowView::drawForExport(const flow::FlowDocument& doc, ImDrawList* dl, const FlowExportScene& scene) {
    FlowDrawCtx c;
    c.dl = dl;
    c.org = ImVec2(-scene.area.x * scene.scale, -scene.area.y * scene.scale);
    c.s = scene.scale;
    c.clipMin = ImVec2(0.0f, 0.0f);
    c.clipMax = scene.size;
    c.font = ImGui::GetFont();
    c.board = scene.board;
    c.ink = scene.ink;
    c.paper = scene.paper;

    const bool all = scene.only.empty();
    const std::unordered_set<NodeId> keep(scene.only.begin(), scene.only.end());
    for (const flow::NodeId id : doc.nodesByZ()) {
        const flow::Node* n = doc.node(id);
        if (!n || (!all && keep.count(id) == 0))
            continue;
        SDL_Texture* tex = n->kind == NodeKind::Image ? imageTexture(doc, n->imageId) : nullptr;
        drawNodeClipped(c, doc, *n, tex, false);
    }
    const flow::NodeLookup lookup = [&](flow::NodeId id) { return doc.node(id); };
    for (const flow::Edge& e : doc.edges()) {
        if (!exportsEdge(e, all, keep))
            continue;
        const std::optional<flow::EdgePath> path = flow::routeEdge(e, lookup);
        if (!path)
            continue;
        drawEdgeLine(c, e, *path, false, 1.0f);
        drawEdgeLabel(c, e, *path);
    }
}

// === overlays ===================================================================================================

void FlowView::drawOverlays(FlowState& state, const FlowDrawCtx& c) {
    ImDrawList* dl = c.dl;
    const float th = 1.5f;
    const Mode mode = gesture_.mode;

    // Hover: an outline on the node (or a highlight on the edge) the pointer would act on.
    if (mode == Mode::None) {
        if (const flow::Node* h = state.doc.node(hover_.body)) {
            if (!state.isNodeSelected(h->id)) {
                const ImU32 col = alphaScaled(c.handle, 0.7f);
                if (h->kind == NodeKind::Container || h->kind == NodeKind::Text || h->kind == NodeKind::Image) {
                    dl->AddRect(c.scr(h->rect.x, h->rect.y), c.scr(h->rect.right(), h->rect.bottom()), col, 0.0f, 0, th);
                } else {
                    const std::vector<ImVec2> pts = toScreenPoly(c, flow::nodeOutline(*h));
                    dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, ImDrawFlags_Closed, th);
                }
            }
        } else if (hover_.edge != flow::kNoEdge && !state.isEdgeSelected(hover_.edge)) {
            if (const flow::EdgePath* path = pathOf(hover_.edge)) {
                const std::vector<ImVec2> pts = toScreenPoly(c, path->points);
                dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), alphaScaled(c.handle, 0.4f), 0,
                                std::max(1.0f, 2.0f * c.s) + 4.0f);
            }
        }
    }

    // Selected nodes: a frame each, and the eight resize handles when there is just one.
    const bool single = state.selNodes.size() == 1 && state.selEdges.empty();
    bool haveBounds = false;
    Rect bounds;
    for (const flow::NodeId id : state.selNodes) {
        const flow::Node* n = state.doc.node(id);
        if (!n || !overlaps(n->rect, visible_))
            continue;
        const ImVec2 a = c.scr(n->rect.x, n->rect.y);
        const ImVec2 b = c.scr(n->rect.right(), n->rect.bottom());
        dl->AddRect(a, b, c.handle, 0.0f, 0, th);
        bounds = haveBounds ? unionRect(bounds, n->rect) : n->rect;
        haveBounds = true;
        if (single && mode != Mode::Move) {
            static constexpr flow::ResizeHandle kAll[8] = {
                flow::ResizeHandle::TopLeft,    flow::ResizeHandle::TopRight, flow::ResizeHandle::BottomRight,
                flow::ResizeHandle::BottomLeft, flow::ResizeHandle::Top,      flow::ResizeHandle::Right,
                flow::ResizeHandle::Bottom,     flow::ResizeHandle::Left};
            const bool small = std::min(b.x - a.x, b.y - a.y) < 24.0f;
            for (int i = 0; i < (small ? 4 : 8); ++i) {
                const ImVec2 hp = c.scr(flow::resizeHandlePos(n->rect, kAll[i]));
                dl->AddRectFilled(ImVec2(hp.x - kHandleHalfPx, hp.y - kHandleHalfPx),
                                  ImVec2(hp.x + kHandleHalfPx, hp.y + kHandleHalfPx), c.board);
                dl->AddRect(ImVec2(hp.x - kHandleHalfPx, hp.y - kHandleHalfPx),
                            ImVec2(hp.x + kHandleHalfPx, hp.y + kHandleHalfPx), c.handle, 0.0f, 0, th);
            }
        }
    }
    if (haveBounds && state.selNodes.size() > 1) {
        dl->AddRect(c.scr(bounds.x - 3.0f, bounds.y - 3.0f), c.scr(bounds.right() + 3.0f, bounds.bottom() + 3.0f),
                    alphaScaled(c.handle, 0.6f), 0.0f, 0, 1.0f);
    }

    // The selected edge shows its two end handles.
    if (state.selEdges.size() == 1 && state.selNodes.empty()) {
        if (const flow::EdgePath* path = pathOf(state.selEdges[0])) {
            if (path->points.size() >= 2) {
                for (const Pt& p : {path->points.front(), path->points.back()}) {
                    const ImVec2 sp = c.scr(p);
                    dl->AddCircleFilled(sp, kEdgeEndPx - 1.0f, c.board);
                    dl->AddCircle(sp, kEdgeEndPx - 1.0f, c.handle, 0, 2.0f);
                }
                // The route handle: a diamond on the line, filled once the route was shaped by hand.
                if (path->hasHandle) {
                    const flow::Edge* sel = state.doc.edge(state.selEdges[0]);
                    const ImVec2 sp = c.scr(path->handle);
                    const bool manual = sel && sel->via;
                    const bool active = mode == Mode::Bend || hover_.onBend;
                    const float rad = kEdgeEndPx - (active ? 0.0f : 1.0f);
                    dl->AddNgonFilled(sp, rad, manual || active ? c.handle : c.board, 4);
                    dl->AddNgon(sp, rad, c.handle, 4, 2.0f);
                }
            }
        }
    }

    // Connection dots on the node the pointer is over (or, while connecting, on the drop target).
    // `onOutline`: while connecting, the dots are drawn on the outline, where a connector docks.
    const auto drawDots = [&](const flow::Node& n, std::optional<Face> hot, bool onOutline = false) {
        const flow::Node dn = dotNode(n, onOutline ? 0.0f : pxToBoard(kDotOffsetPx));
        static constexpr Face kFaces[4] = {Face::Top, Face::Right, Face::Bottom, Face::Left};
        for (const Face f : kFaces) {
            const ImVec2 sp = c.scr(flow::faceAnchor(dn, f).point);
            const bool isHot = hot && *hot == f;
            dl->AddCircleFilled(sp, isHot ? kDotRadiusPx + 2.0f : kDotRadiusPx, isHot ? c.accent : c.handle);
            dl->AddCircle(sp, isHot ? kDotRadiusPx + 2.0f : kDotRadiusPx, c.board, 0, 1.5f);
        }
    };
    if (mode == Mode::None && hover_.host != flow::kNoNode && gesture_.mode == Mode::None) {
        if (const flow::Node* h = state.doc.node(hover_.host))
            drawDots(*h, hover_.onFace ? std::optional<Face>(hover_.face) : std::nullopt);
    }
    if ((mode == Mode::Connect || mode == Mode::Reconnect) && gesture_.dragging) {
        const DropTarget t = resolveDrop(state, pointerBoard_);
        if (const flow::Node* tn = state.doc.node(t.node)) {
            const std::vector<ImVec2> pts = toScreenPoly(c, flow::nodeOutline(*tn));
            dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), alphaScaled(c.accent, 0.6f), ImDrawFlags_Closed, 2.0f);
            drawDots(*tn, t.face, true);
        }
        if (mode == Mode::Connect) {
            if (const flow::Node* fn = state.doc.node(gesture_.from.node))
                drawDots(*fn, gesture_.from.face != Face::Auto ? std::optional<Face>(gesture_.from.face) : std::nullopt, true);
        }
    }
}

void FlowView::drawGestureOverlay(FlowState& state, const FlowDrawCtx& c) {
    ImDrawList* dl = c.dl;
    const Gesture& g = gesture_;

    const auto sizeLabel = [&](const Rect& r) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%d x %d", static_cast<int>(std::lround(r.w)), static_cast<int>(std::lround(r.h)));
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        const ImVec2 at = c.scr(r.center().x, r.bottom());
        const ImVec2 a(at.x - ts.x * 0.5f - 5.0f, at.y + 8.0f);
        const ImVec2 b(at.x + ts.x * 0.5f + 5.0f, at.y + 8.0f + ts.y + 4.0f);
        dl->AddRectFilled(a, b, c.handle, 3.0f);
        dl->AddText(ImVec2(a.x + 5.0f, a.y + 2.0f), ImGui::ColorConvertFloat4ToU32(contrastText(tokens().flowHandle)), buf);
    };

    switch (g.mode) {
    case Mode::Move: {
        for (const flow::Guide& gd : g.guides) {
            if (gd.vertical)
                dl->AddLine(c.scr(gd.pos, gd.from), c.scr(gd.pos, gd.to), c.handle, 1.0f);
            else
                dl->AddLine(c.scr(gd.from, gd.pos), c.scr(gd.to, gd.pos), c.handle, 1.0f);
        }
        break;
    }
    case Mode::Resize:
        if (g.dragging)
            if (const flow::Node* n = state.doc.node(g.node))
                sizeLabel(n->rect);
        break;
    case Mode::Band:
        if (g.dragging) {
            const ImVec2 a = c.scr(g.createRect.x, g.createRect.y);
            const ImVec2 b = c.scr(g.createRect.right(), g.createRect.bottom());
            dl->AddRectFilled(a, b, alphaScaled(c.handle, 0.14f));
            dl->AddRect(a, b, c.handle, 0.0f, 0, 1.0f);
        }
        break;
    case Mode::Create:
        if (g.dragging) {
            const std::vector<ImVec2> pts = toScreenPoly(c, flow::outlineFor(g.kind, g.createRect, 8.0f, g.sides));
            dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), alphaScaled(c.handle, 0.1f));
            strokePolyline(c, pts, true, c.handle, 1.5f, flow::Dash::Dashed);
            sizeLabel(g.createRect);
        }
        break;
    case Mode::Connect:
    case Mode::Reconnect: {
        if (!g.dragging)
            break;
        const flow::Node* fixedNode = state.doc.node(g.from.node);
        const DropTarget t = resolveDrop(state, pointerBoard_);
        const flow::Node* target = state.doc.node(t.node);
        if (g.mode == Mode::Connect && !fixedNode)
            break;

        flow::RouteEnd fixedEnd;
        if (fixedNode) {
            Face f = g.from.face;
            if (f == Face::Auto)
                f = nearestFace(*fixedNode, pointerBoard_);
            fixedEnd = flow::RouteEnd{flow::faceAnchor(*fixedNode, f), fixedNode->rect, true};
        } else {
            // A free fixed end (Reconnect of an edge whose other end is a free point).
            fixedEnd = flow::RouteEnd{flow::Anchor{g.from.point, Pt{1.0f, 0.0f}}, Rect{}, false};
        }
        flow::RouteEnd moving;
        if (target) {
            Face f = t.face;
            if (f == Face::Auto)
                f = nearestFace(*target, fixedEnd.anchor.point);
            moving = flow::RouteEnd{flow::faceAnchor(*target, f), target->rect, true};
        } else {
            Pt away{fixedEnd.anchor.point.x - pointerBoard_.x, fixedEnd.anchor.point.y - pointerBoard_.y};
            const float len = std::hypot(away.x, away.y);
            away = len > 1.0e-3f ? Pt{away.x / len, away.y / len} : Pt{-1.0f, 0.0f};
            moving = flow::RouteEnd{flow::Anchor{pointerBoard_, away}, Rect{}, false};
        }
        const bool movingIsFrom = g.mode == Mode::Reconnect && g.movingFrom;
        const flow::RouteEnd& a = movingIsFrom ? moving : fixedEnd;
        const flow::RouteEnd& b = movingIsFrom ? fixedEnd : moving;
        const flow::EdgePath path = flow::routePath(state.defaultRoute, a, b);
        if (path.points.size() < 2)
            break;
        const flow::Edge* orig = g.mode == Mode::Reconnect ? state.doc.edge(g.edge) : nullptr;
        const flow::Arrow arrowEnd = orig ? orig->arrowEnd : state.defaultArrowEnd;
        const flow::Arrow arrowStart = orig ? orig->arrowStart : state.defaultArrowStart;
        const flow::ArrowHead hs = flow::arrowHeadFor(path, true, arrowStart, 2.0f);
        const flow::ArrowHead he = flow::arrowHeadFor(path, false, arrowEnd, 2.0f);
        const std::vector<ImVec2> sp = toScreenPoly(c, trimPolyline(path.points, hs.inset, he.inset));
        dl->AddPolyline(sp.data(), static_cast<int>(sp.size()), c.handle, 0, std::max(2.0f, 2.0f * c.s));
        drawArrowHead(c, hs, c.handle, 2.0f);
        drawArrowHead(c, he, c.handle, 2.0f);
        break;
    }
    case Mode::None:
    case Mode::Pan:
    case Mode::Bend:
    case Mode::Label: break;
    }
}

// === input ======================================================================================================

void FlowView::routeInput(FlowState& state, bool hovered) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool mouseValid = ImGui::IsMousePosValid(&io.MousePos);
    const bool typing = io.WantTextInput;

    if (gesture_.mode != Mode::None) {
        updateGesture(state);
        return;
    }
    hover_ = Hover{};
    if (!hovered || !mouseValid)
        return;

    const Pt p = pointerBoard_;
    const bool spaceDown = ImGui::IsKeyDown(ImGuiKey_Space) && !typing;

    const bool leftClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool middleClick = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    const bool rightClick = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    // A click on the board settles the text editor first (its own window handles the click-away otherwise).
    if (editor_.open && (leftClick || middleClick || rightClick))
        closeEditor(state, true);

    if (middleClick) {
        beginPan(ImGuiMouseButton_Middle);
        return;
    }
    if (leftClick) {
        if (spaceDown || state.tool == FlowTool::Hand) {
            beginPan(ImGuiMouseButton_Left);
            return;
        }
        switch (state.tool) {
        case FlowTool::Select: pressSelect(state, p, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)); return;
        case FlowTool::Connector: pressConnector(state, p); return;
        case FlowTool::Text:
        case FlowTool::Sticky:
        case FlowTool::Shape: beginCreate(state, p, state.tool); return;
        case FlowTool::Hand:
        case FlowTool::Image: return;
        }
        return;
    }
    if (rightClick) {
        pressRight(state, p);
        return;
    }

    updateHover(state, p);

    // Cursor hints.
    if (spaceDown || state.tool == FlowTool::Hand)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    else if (hover_.handle != flow::ResizeHandle::None)
        ImGui::SetMouseCursor(cursorFor(hover_.handle));
    else if (hover_.onFace || hover_.onEdgeEnd)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    else if (hover_.onBend)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    else if (state.tool == FlowTool::Text)
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
}

void FlowView::handleKeys(FlowState& state) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || editor_.open || picker_.open || gesture_.mode != Mode::None)
        return;
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && state.tool != FlowTool::Select) {
        state.tool = FlowTool::Select;
        return;
    }
    if (io.KeyCtrl || io.KeyAlt || io.KeySuper)
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        if (state.selNodes.size() == 1 && state.selEdges.empty()) {
            const flow::Node* n = state.doc.node(state.selNodes[0]);
            if (n && n->kind != NodeKind::Image)
                beginEditNode(n->id);
        } else if (state.selEdges.size() == 1 && state.selNodes.empty()) {
            beginEditEdge(state.selEdges[0]);
        }
        return;
    }

    float dx = 0.0f;
    float dy = 0.0f;
    const float step = io.KeyShift ? 10.0f : 1.0f;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        dx -= step;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        dx += step;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        dy -= step;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        dy += step;
    if ((dx != 0.0f || dy != 0.0f) && !state.selNodes.empty()) {
        if (state.doc.moveNodes(state.selNodes, dx, dy))
            state.commit();
    }
}

void FlowView::beginPan(int button) {
    gesture_ = Gesture{};
    gesture_.mode = Mode::Pan;
    gesture_.button = button;
    fitMode_ = false;
}

void FlowView::pressSelect(FlowState& state, flow::Pt p, bool doubleClick) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool toggle = io.KeyShift || io.KeyCtrl;

    // 1. The end handles of the selected edge.
    if (state.selEdges.size() == 1 && state.selNodes.empty()) {
        if (const flow::EdgePath* path = pathOf(state.selEdges[0])) {
            const flow::EdgeEnd end = flow::hitEdgeEndpoint(*path, p, pxToBoard(kEdgeEndPx));
            if (end != flow::EdgeEnd::None) {
                beginReconnect(state, p, state.selEdges[0], end == flow::EdgeEnd::From);
                return;
            }
        }
        // The route handle: drag to shape the connector by hand, double-click to route it automatically again.
        if (onBendHandle(state.selEdges[0], p)) {
            if (doubleClick) {
                if (state.doc.setEdgeVia(state.selEdges[0], std::nullopt))
                    state.commit();
            } else {
                beginBendDrag(state, p, state.selEdges[0]);
            }
            return;
        }
    }
    // 2. The resize handles of a lone selected node.
    if (state.selNodes.size() == 1 && state.selEdges.empty()) {
        if (const flow::Node* sel = state.doc.node(state.selNodes[0])) {
            const flow::ResizeHandle h = flow::hitResizeHandle(sel->rect, p, pxToBoard(kHandleHitPx));
            if (h != flow::ResizeHandle::None) {
                beginResize(state, p, sel->id, h);
                return;
            }
        }
    }
    // 3. A connection dot of the node under (or just beside) the pointer.
    const PickResult pick = pickNode(state, p, pxToBoard(kHoverNearPx));
    const flow::NodeId host = pick.body != flow::kNoNode ? pick.body : pick.nearby;
    if (const flow::Node* hn = state.doc.node(host)) {
        const flow::Node dn = dotNode(*hn, pxToBoard(kDotOffsetPx));
        if (const std::optional<Face> f = flow::hitFaceHandle(dn, p, pxToBoard(kDotHitPx))) {
            beginConnect(state, p, host, *f);
            return;
        }
    }
    // 4. A node.
    if (const flow::Node* n = state.doc.node(pick.body)) {
        if (n->groupId != enteredGroup_)
            enteredGroup_ = flow::kNoGroup;
        if (doubleClick) {
            if (n->groupId != flow::kNoGroup && n->groupId != enteredGroup_) {
                // Enter the group: from now on its members are picked one by one.
                enteredGroup_ = n->groupId;
                state.clearSelection();
                state.selNodes.push_back(n->id);
            } else {
                state.clearSelection();
                state.selNodes.push_back(n->id);
                if (n->kind != NodeKind::Image)
                    beginEditNode(n->id);
            }
            return;
        }
        flow::NodeId clickNode = flow::kNoNode;
        if (toggle) {
            if (state.isNodeSelected(n->id)) {
                toggleNode(state, n->id);
                return;
            }
            state.selEdges.clear();
            toggleNode(state, n->id);
        } else if (!state.isNodeSelected(n->id)) {
            selectNode(state, n->id);
        } else {
            state.selEdges.clear();
            clickNode = n->id; // a click without a drag narrows the selection to this node
        }
        beginMove(state, p, clickNode);
        return;
    }
    // 5. An edge (or its label).
    const flow::EdgeId edge = pickEdge(state, p);
    if (edge != flow::kNoEdge) {
        const flow::Edge* picked = state.doc.edge(edge);
        const flow::EdgePath* pickedPath = pathOf(edge);
        if (!doubleClick && !toggle && picked && pickedPath && labelHit(*picked, *pickedPath, p)) {
            // Pressing a label selects its connector and lets the label be dragged along (and across) the line.
            if (!state.isEdgeSelected(edge))
                state.selectOnlyEdge(edge);
            else
                state.selNodes.clear();
            beginLabelDrag(state, p, edge);
            return;
        }
        if (doubleClick) {
            state.selectOnlyEdge(edge);
            beginEditEdge(edge);
        } else if (toggle) {
            auto it = std::find(state.selEdges.begin(), state.selEdges.end(), edge);
            if (it != state.selEdges.end())
                state.selEdges.erase(it);
            else
                state.selEdges.push_back(edge);
        } else if (!state.isEdgeSelected(edge)) {
            state.selectOnlyEdge(edge);
        } else {
            state.selNodes.clear();
        }
        return;
    }
    // 6. Empty board: rubber band.
    if (doubleClick)
        return;
    if (!toggle) {
        state.clearSelection();
        enteredGroup_ = flow::kNoGroup;
    }
    beginBand(state, p, toggle);
}

void FlowView::pressConnector(FlowState& state, flow::Pt p) {
    const PickResult pick = pickNode(state, p, pxToBoard(kHoverNearPx));
    const flow::NodeId host = pick.body != flow::kNoNode ? pick.body : pick.nearby;
    const flow::Node* hn = state.doc.node(host);
    if (!hn)
        return;
    const flow::Node dn = dotNode(*hn, pxToBoard(kDotOffsetPx));
    if (const std::optional<Face> f = flow::hitFaceHandle(dn, p, pxToBoard(kDotHitPx))) {
        beginConnect(state, p, host, *f);
        return;
    }
    if (pick.body != flow::kNoNode)
        beginConnect(state, p, pick.body, nearestFace(*hn, p));
}

void FlowView::pressRight(FlowState& state, flow::Pt p) {
    const PickResult pick = pickNode(state, p, 0.0f);
    if (pick.body != flow::kNoNode) {
        if (!state.isNodeSelected(pick.body))
            selectNode(state, pick.body);
        return;
    }
    const flow::EdgeId edge = pickEdge(state, p);
    if (edge == flow::kNoEdge)
        return;
    if (!state.isEdgeSelected(edge))
        state.selectOnlyEdge(edge);
    edgeMenuRequest_ = true;
}

// --- gesture starts ---------------------------------------------------------------------------------------------

void FlowView::beginMove(FlowState& state, flow::Pt p, flow::NodeId clickNode) {
    gesture_ = Gesture{};
    Gesture& g = gesture_;
    g.mode = Mode::Move;
    g.startScreen = ImGui::GetIO().MousePos;
    g.startBoard = p;
    g.clickNode = clickNode;
    fitMode_ = false;

    // Everything that moves with the selection: group members (unless the group was entered) and the children
    // of containers.
    std::unordered_set<flow::NodeId> seen;
    std::vector<flow::NodeId> work;
    const auto add = [&](flow::NodeId id) {
        if (id != flow::kNoNode && state.doc.node(id) && seen.insert(id).second) {
            g.ids.push_back(id);
            work.push_back(id);
        }
    };
    for (const flow::NodeId id : state.selNodes)
        add(id);
    while (!work.empty()) {
        const flow::NodeId id = work.back();
        work.pop_back();
        const flow::Node* n = state.doc.node(id);
        if (!n)
            continue;
        if (n->groupId != flow::kNoGroup && n->groupId != enteredGroup_)
            for (const flow::NodeId m : state.doc.groupMembers(n->groupId))
                add(m);
        if (n->kind == NodeKind::Container)
            for (const flow::NodeId ch : state.doc.childrenOf(id))
                add(ch);
    }
    g.sortedIds = g.ids;
    std::sort(g.sortedIds.begin(), g.sortedIds.end());
    bool first = true;
    for (const flow::NodeId id : g.ids) {
        const flow::Node* n = state.doc.node(id);
        g.startRects.push_back(n ? n->rect : Rect{});
        if (n) {
            g.startBounds = first ? n->rect : unionRect(g.startBounds, n->rect);
            first = false;
        }
    }
}

void FlowView::beginResize(FlowState&, flow::Pt p, flow::NodeId node, flow::ResizeHandle handle) {
    gesture_ = Gesture{};
    gesture_.mode = Mode::Resize;
    gesture_.startScreen = ImGui::GetIO().MousePos;
    gesture_.startBoard = p;
    gesture_.node = node;
    gesture_.handle = handle;
    fitMode_ = false;
}

void FlowView::beginBand(FlowState& state, flow::Pt p, bool additive) {
    gesture_ = Gesture{};
    gesture_.mode = Mode::Band;
    gesture_.startScreen = ImGui::GetIO().MousePos;
    gesture_.startBoard = p;
    gesture_.additive = additive;
    gesture_.baseNodes = state.selNodes;
    gesture_.baseEdges = state.selEdges;
    gesture_.createRect = Rect{p.x, p.y, 0.0f, 0.0f};
    fitMode_ = false;
}

void FlowView::beginConnect(FlowState& state, flow::Pt p, flow::NodeId node, flow::Face face) {
    gesture_ = Gesture{};
    gesture_.mode = Mode::Connect;
    gesture_.startScreen = ImGui::GetIO().MousePos;
    gesture_.startBoard = p;
    gesture_.from = flow::End{node, face, Pt{}};
    if (!state.isNodeSelected(node))
        selectNode(state, node);
    fitMode_ = false;
}

void FlowView::beginReconnect(FlowState& state, flow::Pt p, flow::EdgeId edge, bool movingFrom) {
    const flow::Edge* e = state.doc.edge(edge);
    if (!e)
        return;
    gesture_ = Gesture{};
    gesture_.mode = Mode::Reconnect;
    gesture_.startScreen = ImGui::GetIO().MousePos;
    gesture_.startBoard = p;
    gesture_.edge = edge;
    gesture_.movingFrom = movingFrom;
    gesture_.from = movingFrom ? e->to : e->from; // the end that stays
    fitMode_ = false;
}

void FlowView::beginCreate(FlowState& state, flow::Pt p, FlowTool tool) {
    gesture_ = Gesture{};
    Gesture& g = gesture_;
    g.mode = Mode::Create;
    g.startScreen = ImGui::GetIO().MousePos;
    g.tool = tool;
    g.kind = tool == FlowTool::Text ? NodeKind::Text : (tool == FlowTool::Sticky ? NodeKind::Sticky : state.shapeKind);
    if (g.kind == NodeKind::Image)
        g.kind = NodeKind::Rectangle;
    g.sides = state.polygonSides;
    g.startBoard = state.snapToGrid && !ImGui::GetIO().KeyAlt ? flow::snapToGrid(p, state.gridSize) : p;
    const Pt sz = sizeFor(g.kind);
    g.createRect = Rect{g.startBoard.x, g.startBoard.y, sz.x, sz.y};
    fitMode_ = false;
}

void FlowView::beginLabelDrag(FlowState& state, flow::Pt p, flow::EdgeId edge) {
    if (!state.doc.edge(edge))
        return;
    gesture_ = Gesture{};
    gesture_.mode = Mode::Label;
    gesture_.startScreen = ImGui::GetIO().MousePos;
    gesture_.startBoard = p;
    gesture_.edge = edge;
    fitMode_ = false;
}

void FlowView::beginBendDrag(FlowState& state, flow::Pt p, flow::EdgeId edge) {
    if (!state.doc.edge(edge))
        return;
    gesture_ = Gesture{};
    gesture_.mode = Mode::Bend;
    gesture_.startScreen = ImGui::GetIO().MousePos;
    gesture_.startBoard = p;
    gesture_.edge = edge;
    fitMode_ = false;
}

// --- gesture updates ----------------------------------------------------------------------------------------------

bool FlowView::gestureValid(const FlowState& state) const {
    const Gesture& g = gesture_;
    switch (g.mode) {
    case Mode::Move:
        for (const flow::NodeId id : g.ids)
            if (!state.doc.node(id))
                return false;
        return true;
    case Mode::Resize: return state.doc.node(g.node) != nullptr;
    case Mode::Label:
    case Mode::Bend: return state.doc.edge(g.edge) != nullptr;
    case Mode::Connect: return state.doc.node(g.from.node) != nullptr;
    case Mode::Reconnect:
        return state.doc.edge(g.edge) != nullptr && (g.from.node == flow::kNoNode || state.doc.node(g.from.node) != nullptr);
    default: return true;
    }
}

void FlowView::endGestureState() {
    gesture_ = Gesture{};
}

void FlowView::abortGesture(FlowState& state) {
    Gesture& g = gesture_;
    if (g.mode == Mode::Band) {
        state.selNodes = g.baseNodes;
        state.selEdges = g.baseEdges;
        state.pruneSelection();
    }
    if (g.histOwned)
        cancelHist(state);
    endGestureState();
}

void FlowView::updateGesture(FlowState& state) {
    const ImGuiIO& io = ImGui::GetIO();
    Gesture& g = gesture_;
    if (!gestureValid(state) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        abortGesture(state);
        return;
    }
    const bool mouseValid = ImGui::IsMousePosValid(&io.MousePos);
    const bool held = ImGui::IsMouseDown(g.button);
    if (!g.dragging && mouseValid &&
        std::hypot(io.MousePos.x - g.startScreen.x, io.MousePos.y - g.startScreen.y) >= kDragPx)
        g.dragging = true;

    const Pt p = pointerBoard_;
    switch (g.mode) {
    case Mode::Pan:
        if (held && mouseValid)
            view_.panBy(io.MouseDelta.x, io.MouseDelta.y);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        break;
    case Mode::Move:
        if (mouseValid)
            updateMove(state, p);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        break;
    case Mode::Resize:
        if (mouseValid)
            updateResize(state, p);
        ImGui::SetMouseCursor(cursorFor(g.handle));
        break;
    case Mode::Band:
        if (mouseValid)
            updateBand(state, p);
        break;
    case Mode::Create:
        if (mouseValid)
            updateCreate(state, p);
        break;
    case Mode::Label:
        if (mouseValid)
            updateLabelDrag(state, p);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        break;
    case Mode::Bend:
        if (mouseValid)
            updateBendDrag(state, p);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        break;
    case Mode::Connect:
    case Mode::Reconnect:
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        break;
    case Mode::None: break;
    }
    if (!held)
        releaseGesture(state, p);
}

// Dragging a label: it follows the pointer along the line, and counts as sitting beside the line (Above or Below)
// once the pointer is clearly off it on that side, as sitting on the line otherwise.
void FlowView::updateLabelDrag(FlowState& state, flow::Pt p) {
    Gesture& g = gesture_;
    if (!g.dragging)
        return;
    const flow::Edge* e = state.doc.edge(g.edge);
    const flow::EdgePath* path = pathOf(g.edge);
    if (!e || !path || path->points.size() < 2)
        return;
    if (!g.histOwned) {
        beginHist(state);
        g.histOwned = true;
    }
    const flow::PathProjection hit = flow::projectOnPath(path->points, p);
    const LabelBox box = labelBoxOf(*e, *path, ImGui::GetFont());
    const flow::LabelPlacement beside =
        flow::placeLabel(path->points, hit.t, e->labelAlong, flow::LabelSide::Above, box.halfW, box.halfH);
    const float reach = std::hypot(beside.center.x - beside.base.x, beside.center.y - beside.base.y);
    flow::LabelSide side = flow::LabelSide::On;
    if (hit.side > reach * 0.5f)
        side = flow::LabelSide::Above;
    else if (hit.side < -reach * 0.5f)
        side = flow::LabelSide::Below;
    state.doc.setEdgeLabelPlacement(g.edge, hit.t, side, e->labelAlong);
}

// Dragging the route handle: the connector's manual offset follows the pointer (an elbow only slides its segment).
void FlowView::updateBendDrag(FlowState& state, flow::Pt p) {
    Gesture& g = gesture_;
    if (!g.dragging)
        return;
    const flow::EdgePath* path = pathOf(g.edge);
    if (!path || !path->hasHandle)
        return;
    if (!g.histOwned) {
        beginHist(state);
        g.histOwned = true;
    }
    const bool snap = state.snapToGrid && !ImGui::GetIO().KeyAlt;
    state.doc.setEdgeVia(g.edge, flow::viaFromPoint(*path, snap ? flow::snapToGrid(p, state.gridSize) : p));
}

void FlowView::updateMove(FlowState& state, flow::Pt p) {
    Gesture& g = gesture_;
    if (!g.dragging || g.ids.empty())
        return;
    const ImGuiIO& io = ImGui::GetIO();
    if (!g.histOwned) {
        beginHist(state);
        g.histOwned = true;
    }
    float dx = p.x - g.startBoard.x;
    float dy = p.y - g.startBoard.y;
    if (io.KeyShift) { // Shift locks the move to one axis
        if (std::abs(dx) >= std::abs(dy))
            dy = 0.0f;
        else
            dx = 0.0f;
    }
    g.guides.clear();
    if (!io.KeyAlt) {
        bool snapX = false;
        bool snapY = false;
        if (state.smartGuides) {
            std::vector<Rect> others;
            const Rect nearRect = inflateRect(visible_, pxToBoard(200.0f));
            for (const flow::Node& n : state.doc.nodes()) {
                if (others.size() >= kMaxGuideRects)
                    break;
                if (std::binary_search(g.sortedIds.begin(), g.sortedIds.end(), n.id) || !overlaps(n.rect, nearRect))
                    continue;
                others.push_back(n.rect);
            }
            const Rect moving{g.startBounds.x + dx, g.startBounds.y + dy, g.startBounds.w, g.startBounds.h};
            const flow::SnapResult res = flow::snapToGuides(moving, others, pxToBoard(kGuideSnapPx));
            if (res.snappedX) {
                dx += res.delta.x;
                snapX = true;
            }
            if (res.snappedY) {
                dy += res.delta.y;
                snapY = true;
            }
            for (const flow::Guide& gd : res.guides)
                if (gd.vertical ? snapX : snapY)
                    g.guides.push_back(gd);
        }
        if (state.snapToGrid) {
            if (!snapX)
                dx = flow::snapToGrid(g.startBounds.x + dx, state.gridSize) - g.startBounds.x;
            if (!snapY)
                dy = flow::snapToGrid(g.startBounds.y + dy, state.gridSize) - g.startBounds.y;
        }
    }
    if (dx == g.applied.x && dy == g.applied.y)
        return;
    for (std::size_t i = 0; i < g.ids.size(); ++i) {
        Rect r = g.startRects[i];
        r.x += dx;
        r.y += dy;
        state.doc.setNodeRect(g.ids[i], r);
    }
    g.applied = Pt{dx, dy};
}

void FlowView::updateResize(FlowState& state, flow::Pt p) {
    Gesture& g = gesture_;
    if (!g.dragging)
        return;
    if (!g.histOwned) {
        beginHist(state);
        g.histOwned = true;
        g.startRect = state.doc.node(g.node)->rect;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const Rect r = g.startRect;
    const bool snap = state.snapToGrid && !io.KeyAlt;
    const auto sx = [&](float v) { return snap ? flow::snapToGrid(v, state.gridSize) : v; };
    float x0 = r.x;
    float x1 = r.right();
    float y0 = r.y;
    float y1 = r.bottom();
    const bool ml = movesLeft(g.handle);
    const bool mr = movesRight(g.handle);
    const bool mt = movesTop(g.handle);
    const bool mb = movesBottom(g.handle);
    if (ml)
        x0 = sx(p.x);
    if (mr)
        x1 = sx(p.x);
    if (mt)
        y0 = sx(p.y);
    if (mb)
        y1 = sx(p.y);
    constexpr float kMin = flow::kMinNodeSize;
    if (ml)
        x0 = std::min(x0, x1 - kMin);
    if (mr)
        x1 = std::max(x1, x0 + kMin);
    if (mt)
        y0 = std::min(y0, y1 - kMin);
    if (mb)
        y1 = std::max(y1, y0 + kMin);

    if (io.KeyShift && r.w > 0.0f && r.h > 0.0f) { // keep the aspect ratio
        float w = x1 - x0;
        float h = y1 - y0;
        const bool corner = (ml || mr) && (mt || mb);
        if (corner) {
            const float f = std::max(w / r.w, h / r.h);
            w = std::max(r.w * f, kMin);
            h = std::max(r.h * f, kMin);
            if (ml)
                x0 = x1 - w;
            else
                x1 = x0 + w;
            if (mt)
                y0 = y1 - h;
            else
                y1 = y0 + h;
        } else if (ml || mr) {
            h = std::max(w * r.h / r.w, kMin);
            const float cy = r.y + r.h * 0.5f;
            y0 = cy - h * 0.5f;
            y1 = cy + h * 0.5f;
        } else {
            w = std::max(h * r.w / r.h, kMin);
            const float cx = r.x + r.w * 0.5f;
            x0 = cx - w * 0.5f;
            x1 = cx + w * 0.5f;
        }
    }
    state.doc.setNodeRect(g.node, Rect{x0, y0, x1 - x0, y1 - y0});
}

void FlowView::updateBand(FlowState& state, flow::Pt p) {
    Gesture& g = gesture_;
    const float x0 = std::min(g.startBoard.x, p.x);
    const float y0 = std::min(g.startBoard.y, p.y);
    g.createRect = Rect{x0, y0, std::abs(p.x - g.startBoard.x), std::abs(p.y - g.startBoard.y)};
    if (!g.dragging)
        return;

    std::unordered_set<flow::NodeId> hit;
    for (const flow::Node& n : state.doc.nodes()) {
        // A container is picked only when the band holds it whole, so a band started inside one selects its content.
        const bool inside = n.kind == NodeKind::Container ? encloses(g.createRect, n.rect) : overlaps(g.createRect, n.rect);
        if (!inside)
            continue;
        if (n.groupId != flow::kNoGroup && n.groupId != enteredGroup_) {
            for (const flow::NodeId m : state.doc.groupMembers(n.groupId))
                hit.insert(m);
        } else {
            hit.insert(n.id);
        }
    }
    std::vector<flow::NodeId> nodes = g.additive ? g.baseNodes : std::vector<flow::NodeId>{};
    for (const flow::NodeId id : hit)
        if (std::find(nodes.begin(), nodes.end(), id) == nodes.end())
            nodes.push_back(id);
    std::vector<flow::EdgeId> edges = g.additive ? g.baseEdges : std::vector<flow::EdgeId>{};
    for (const EdgeDraw& ed : visEdges_) {
        bool all = !ed.path.points.empty();
        for (const Pt& q : ed.path.points)
            if (!containsPt(g.createRect, q)) {
                all = false;
                break;
            }
        if (all && std::find(edges.begin(), edges.end(), ed.id) == edges.end())
            edges.push_back(ed.id);
    }
    state.selNodes = std::move(nodes);
    state.selEdges = std::move(edges);
}

void FlowView::updateCreate(FlowState& state, flow::Pt p) {
    Gesture& g = gesture_;
    if (!g.dragging)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    const Pt sz = sizeFor(g.kind);
    float dx = p.x - g.startBoard.x;
    float dy = p.y - g.startBoard.y;
    if (io.KeyShift && sz.x > 0.0f && sz.y > 0.0f) { // keep the default proportions
        const float f = std::max(std::abs(dx) / sz.x, std::abs(dy) / sz.y);
        dx = (dx < 0.0f ? -1.0f : 1.0f) * sz.x * f;
        dy = (dy < 0.0f ? -1.0f : 1.0f) * sz.y * f;
    } else if (state.snapToGrid && !io.KeyAlt) {
        const Pt q = flow::snapToGrid(Pt{g.startBoard.x + dx, g.startBoard.y + dy}, state.gridSize);
        dx = q.x - g.startBoard.x;
        dy = q.y - g.startBoard.y;
    }
    const float w = std::max(std::abs(dx), flow::kMinNodeSize);
    const float h = std::max(std::abs(dy), flow::kMinNodeSize);
    g.createRect = Rect{dx < 0.0f ? g.startBoard.x - w : g.startBoard.x, dy < 0.0f ? g.startBoard.y - h : g.startBoard.y, w, h};
}

// --- gesture ends ---------------------------------------------------------------------------------------------------

void FlowView::releaseGesture(FlowState& state, flow::Pt p) {
    switch (gesture_.mode) {
    case Mode::Move: releaseMove(state); break;
    case Mode::Resize:
    case Mode::Label:
    case Mode::Bend:
        if (gesture_.histOwned)
            endHist(state);
        break;
    case Mode::Connect:
    case Mode::Reconnect: releaseConnect(state, p); break;
    case Mode::Create: releaseCreate(state); break;
    case Mode::Band:
        if (!gesture_.dragging && !gesture_.additive)
            state.clearSelection();
        break;
    case Mode::Pan:
    case Mode::None: break;
    }
    endGestureState();
}

void FlowView::reparentMoved(FlowState& state) {
    const Gesture& g = gesture_;
    for (const flow::NodeId id : g.ids) {
        const flow::Node* n = state.doc.node(id);
        if (!n)
            continue;
        // A node that moves because its parent moves keeps that parent.
        if (n->parentId != flow::kNoNode && std::binary_search(g.sortedIds.begin(), g.sortedIds.end(), n->parentId))
            continue;
        const Pt center = n->rect.center();
        const flow::NodeId target = containerAt(state, center, g.sortedIds);
        if (target == n->parentId)
            continue;
        // Staying put wins over hopping to an unrelated, overlapping container.
        if (n->parentId != flow::kNoNode) {
            const flow::Node* cur = state.doc.node(n->parentId);
            if (cur && containsPt(cur->rect, center)) {
                bool nested = false;
                flow::NodeId walk = target;
                for (int depth = 0; walk != flow::kNoNode && depth < kMaxAncestors; ++depth) {
                    if (walk == n->parentId) {
                        nested = true;
                        break;
                    }
                    const flow::Node* w = state.doc.node(walk);
                    walk = w ? w->parentId : flow::kNoNode;
                }
                if (!nested)
                    continue;
            }
        }
        state.doc.reparent(id, target);
    }
}

void FlowView::releaseMove(FlowState& state) {
    Gesture& g = gesture_;
    if (!g.dragging || !g.histOwned) {
        // A plain click on one of several selected nodes keeps just that node.
        if (g.clickNode != flow::kNoNode && state.doc.node(g.clickNode))
            selectNode(state, g.clickNode);
        if (g.histOwned)
            endHist(state);
        return;
    }
    reparentMoved(state);
    endHist(state);
    g.histOwned = false;
}

void FlowView::releaseConnect(FlowState& state, flow::Pt p) {
    Gesture& g = gesture_;
    if (!g.dragging)
        return; // a click on a dot only selects its node
    const DropTarget t = resolveDrop(state, p);

    if (g.mode == Mode::Connect) {
        if (t.node != flow::kNoNode) {
            // Dropping back on the node's own body (or its own dot) is not a connection.
            if (t.node == g.from.node && (!t.onDot || t.face == g.from.face))
                return;
            const flow::EdgeId e = state.doc.connect(g.from, flow::End{t.node, t.face, Pt{}});
            if (e != flow::kNoEdge) {
                styleNewEdge(state, e);
                state.commit();
                state.selectOnlyEdge(e);
            }
            return;
        }
        picker_ = Picker{};
        picker_.requestOpen = true;
        picker_.from = g.from;
        picker_.drop = p;
        return;
    }

    // Reconnect: onto a node it docks there, onto empty board it becomes a free point.
    flow::End moved;
    if (t.node != flow::kNoNode)
        moved = flow::End{t.node, t.face, Pt{}};
    else
        moved = flow::End{flow::kNoNode, Face::Auto, flow::sanitize(p)};
    if (state.doc.reconnect(g.edge, g.movingFrom, moved))
        state.commit();
}

void FlowView::releaseCreate(FlowState& state) {
    Gesture& g = gesture_;
    Rect r = g.createRect;
    const Pt sz = sizeFor(g.kind);
    if (!g.dragging) { // a click: a default-sized node centered on the click
        r = Rect{g.startBoard.x - sz.x * 0.5f, g.startBoard.y - sz.y * 0.5f, sz.x, sz.y};
        if (state.snapToGrid && !ImGui::GetIO().KeyAlt)
            r = flow::snapRectPosition(r, state.gridSize);
    }
    const bool edit = g.tool == FlowTool::Text || g.tool == FlowTool::Sticky;
    if (edit)
        beginHist(state);
    const flow::NodeId id = state.doc.addNode(g.kind, r, {}, styleFor(g.kind), g.sides);
    state.tool = FlowTool::Select;
    if (id == flow::kNoNode) {
        if (edit)
            cancelHist(state);
        return;
    }
    const flow::NodeId container = containerAt(state, r.center(), {});
    if (container != flow::kNoNode)
        state.doc.reparent(id, container);
    enteredGroup_ = flow::kNoGroup;
    state.selectOnlyNode(id);
    if (edit) {
        pendingEditNode_ = id;
        pendingFresh_ = true;
    } else {
        state.commit();
    }
}

// === inline text editor =========================================================================================

void FlowView::openEditor(FlowState& state) {
    const bool fresh = pendingFresh_;
    const bool owns = fresh && histDepth_ > 0;
    Editor e;
    if (pendingEditNode_ != flow::kNoNode) {
        const flow::Node* n = state.doc.node(pendingEditNode_);
        if (!n || n->kind == NodeKind::Image) {
            if (owns)
                cancelHist(state);
            return;
        }
        e.node = n->id;
        e.text = n->text;
    } else if (pendingEditEdge_ != flow::kNoEdge) {
        const flow::Edge* ed = state.doc.edge(pendingEditEdge_);
        if (!ed)
            return;
        e.isEdge = true;
        e.edge = ed->id;
        e.text = ed->label;
    } else {
        return;
    }
    if (editor_.open)
        closeEditor(state, true);
    e.open = true;
    e.original = e.text;
    e.fresh = fresh;
    e.ownsGesture = owns;
    editor_ = std::move(e);
}

void FlowView::closeEditor(FlowState& state, bool commit) {
    Editor e = std::move(editor_);
    editor_ = Editor{};
    if (!e.open)
        return;

    const flow::Node* n = e.isEdge ? nullptr : state.doc.node(e.node);
    const flow::Edge* ed = e.isEdge ? state.doc.edge(e.edge) : nullptr;
    if (!n && !ed) { // the target vanished (undo, delete): nothing to write
        if (e.ownsGesture)
            cancelHist(state);
        return;
    }
    if (!commit) {
        if (e.ownsGesture)
            cancelHist(state); // a node made by a tool and never filled in is dropped again
        return;
    }
    if (e.fresh && e.text.empty()) {
        if (e.ownsGesture) {
            cancelHist(state);
        } else if (n) {
            const flow::NodeId id = e.node;
            state.doc.removeNodes(std::span<const flow::NodeId>(&id, 1));
            state.pruneSelection();
            state.commit();
        }
        return;
    }
    if (n && n->text != e.text)
        state.doc.setText(e.node, e.text);
    if (ed && ed->label != e.text)
        state.doc.setEdgeLabel(e.edge, e.text);
    if (e.ownsGesture)
        endHist(state);
    else
        state.commit();
}

void FlowView::drawEditor(FlowState& state) {
    if (!editor_.open)
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        closeEditor(state, false);
        refocus_ = true;
        return;
    }

    FlowDrawCtx c;
    resolveThemeColors(c);
    Rect area;
    float fontSize = 16.0f;
    ImU32 bg = c.board;
    ImU32 fg = c.ink;
    bool minWidth = false;
    if (!editor_.isEdge) {
        const flow::Node* n = state.doc.node(editor_.node);
        if (!n) {
            closeEditor(state, false);
            return;
        }
        area = n->kind == NodeKind::Container ? flow::containerTitleBand(n->rect) : n->rect;
        fontSize = n->style.fontSize;
        fg = textOf(c, *n);
        bg = nodeHasFill(*n) ? fillOf(c, *n) : c.board;
        if (n->kind == NodeKind::Container)
            bg = mixColor(c.board, c.ink, 0.12f);
    } else {
        const flow::Edge* e = state.doc.edge(editor_.edge);
        if (!e) {
            closeEditor(state, false);
            return;
        }
        const flow::NodeLookup lookup = [&](flow::NodeId id) { return state.doc.node(id); };
        const std::optional<flow::EdgePath> path = flow::routeEdge(*e, lookup);
        if (!path) {
            closeEditor(state, false);
            return;
        }
        fontSize = e->style.fontSize;
        const Pt at = labelBoxOf(*e, *path, ImGui::GetFont()).place.center;
        area = Rect{at.x - 70.0f, at.y - fontSize, 140.0f, fontSize * 2.0f};
        fg = e->style.textColor ? u32(*e->style.textColor) : c.ink;
        minWidth = true;
    }
    // Opaque background whatever the fill's alpha.
    bg |= IM_COL32_A_MASK;

    const float s = view_.scale();
    const float px = std::clamp(fontSize * s, 6.0f, 300.0f);
    const ImVec2 a = toScreen(Pt{area.x, area.y});
    ImVec2 size(std::clamp(area.w * s, 90.0f, 4000.0f), std::clamp(area.h * s, px * 1.6f + 10.0f, 4000.0f));
    if (minWidth)
        size.x = std::max(size.x, 150.0f);
    editor_.rectMin = a;
    editor_.rectMax = ImVec2(a.x + size.x, a.y + size.y);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float ratio = style.FontSizeBase > 0.0f ? ImGui::GetFontSize() / style.FontSizeBase : 1.0f;

    ImGui::SetNextWindowPos(a);
    ImGui::SetNextWindowSize(size);
    if (editor_.focus)
        ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoBackground;
    bool entered = false;
    bool active = false;
    if (ImGui::Begin("##FlowTextEditor", nullptr, flags)) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, bg);
        ImGui::PushStyleColor(ImGuiCol_Text, fg);
        ImGui::PushStyleColor(ImGuiCol_Border, c.handle);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImGui::PushFont(nullptr, px / ratio);
        if (editor_.focus) {
            ImGui::SetKeyboardFocusHere();
            editor_.focus = false;
        }
        entered = ImGui::InputTextMultiline("##flowtext", editor_.text.data(), editor_.text.capacity() + 1, size,
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll |
                                                ImGuiInputTextFlags_CallbackResize,
                                            resizeStringCallback, &editor_.text);
        active = ImGui::IsItemActive();
        ImGui::PopFont();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    ++editor_.frames;
    // Ctrl+Enter or a click elsewhere commits.
    if (entered || (editor_.frames > 2 && !active)) {
        closeEditor(state, true);
        refocus_ = entered; // Ctrl+Enter hands the keyboard back to the board; a click elsewhere keeps its focus
    }
}

// === shape picker ===============================================================================================

void FlowView::drawPicker(FlowState& state) {
    constexpr const char* kId = "##FlowShapePicker";
    if (picker_.requestOpen) {
        ImGui::OpenPopup(kId);
        picker_.requestOpen = false;
        picker_.open = true;
    }
    if (!picker_.open)
        return;

    ImGui::SetNextWindowPos(toScreen(picker_.drop));
    int choice = -2; // -2 nothing, -1 free end, otherwise an index into kPickerKinds
    if (ImGui::BeginPopup(kId, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("%s", tr(Str::FlowPickerTitle));
        ImGui::Separator();
        const float rowH = ImGui::GetTextLineHeight() + 8.0f;
        const float rowW = ImGui::GetFontSize() * 12.0f;
        const float iconW = ImGui::GetFontSize() * 1.7f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
        for (int i = 0; i < static_cast<int>(std::size(kPickerKinds)); ++i) {
            ImGui::PushID(i);
            const bool clicked = ImGui::Selectable("##row", false, 0, ImVec2(rowW, rowH));
            ImGui::PopID();
            const ImVec2 mn = ImGui::GetItemRectMin();
            drawKindIcon(dl, kPickerKinds[i], ImVec2(mn.x + 6.0f, mn.y + 4.0f), ImVec2(mn.x + 6.0f + iconW, mn.y + rowH - 4.0f),
                         textCol);
            dl->AddText(ImVec2(mn.x + 6.0f + iconW + 8.0f, mn.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), textCol,
                        shapeName(kPickerKinds[i]));
            if (clicked)
                choice = i;
        }
        ImGui::Separator();
        if (ImGui::Selectable(tr(Str::FlowPickerNoShape), false, 0, ImVec2(rowW, 0.0f)))
            choice = -1;
        if (choice != -2)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    } else {
        picker_.open = false; // closed by a click elsewhere or Escape
    }

    if (choice == -1) {
        connectToFreePoint(state);
        picker_ = Picker{};
    } else if (choice >= 0) {
        createConnectedNode(state, kPickerKinds[choice]);
        picker_ = Picker{};
    }
}

// The right-click menu of a connector.
void FlowView::drawEdgeMenu(FlowState& state) {
    constexpr const char* kId = "##FlowEdgeMenu";
    if (edgeMenuRequest_) {
        ImGui::OpenPopup(kId);
        edgeMenuRequest_ = false;
    }
    if (!ImGui::BeginPopup(kId))
        return;
    bool anyManual = false;
    for (const flow::EdgeId id : state.selEdges) {
        const flow::Edge* e = state.doc.edge(id);
        anyManual = anyManual || (e && e->via);
    }
    if (ImGui::MenuItem(tr(Str::FlowResetRoute), nullptr, false, anyManual)) {
        bool changed = false;
        for (const flow::EdgeId id : state.selEdges)
            changed = state.doc.setEdgeVia(id, std::nullopt) || changed;
        if (changed)
            state.commit();
    }
    ImGui::EndPopup();
}

void FlowView::connectToFreePoint(FlowState& state) {
    if (!state.doc.node(picker_.from.node))
        return;
    const flow::EdgeId e = state.doc.connect(picker_.from, flow::End{flow::kNoNode, Face::Auto, flow::sanitize(picker_.drop)});
    if (e == flow::kNoEdge)
        return;
    styleNewEdge(state, e);
    state.commit();
    state.selectOnlyEdge(e);
}

void FlowView::createConnectedNode(FlowState& state, flow::NodeKind kind) {
    const flow::Node* src = state.doc.node(picker_.from.node);
    if (!src)
        return;
    const Pt drop = picker_.drop;
    const Face fromFace = picker_.from.face == Face::Auto ? nearestFace(*src, drop) : picker_.from.face;
    const Pt anchor = flow::faceAnchor(*src, fromFace).point;
    const float ddx = drop.x - anchor.x;
    const float ddy = drop.y - anchor.y;
    const Pt sz = sizeFor(kind);

    // The new node sits beyond the drop point with the side facing the source on it.
    Rect r;
    Face toFace;
    if (std::abs(ddx) >= std::abs(ddy)) {
        toFace = ddx >= 0.0f ? Face::Left : Face::Right;
        r = Rect{ddx >= 0.0f ? drop.x : drop.x - sz.x, drop.y - sz.y * 0.5f, sz.x, sz.y};
    } else {
        toFace = ddy >= 0.0f ? Face::Top : Face::Bottom;
        r = Rect{drop.x - sz.x * 0.5f, ddy >= 0.0f ? drop.y : drop.y - sz.y, sz.x, sz.y};
    }
    if (state.snapToGrid)
        r = flow::snapRectPosition(r, state.gridSize);

    const flow::NodeId id = state.doc.addNode(kind, r, {}, styleFor(kind), state.polygonSides);
    if (id == flow::kNoNode)
        return;
    const flow::NodeId container = containerAt(state, r.center(), {});
    if (container != flow::kNoNode)
        state.doc.reparent(id, container);
    const flow::EdgeId e = state.doc.connect(picker_.from, flow::End{id, toFace, Pt{}});
    if (e != flow::kNoEdge)
        styleNewEdge(state, e);
    state.commit();
    state.selectOnlyNode(id);
}

} // namespace trace
