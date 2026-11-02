#include "app/flow_panels.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "app/icon_widgets.hpp"
#include "app/keybinds.hpp"
#include "app/panel_ui.hpp"
#include "app/theme.hpp"
#include "core/i18n.hpp"
#include "core/keymap.hpp"
#include "flow/flow_geometry.hpp"
#include "flow/flow_layout.hpp"

namespace trace {

namespace {

using flow::Node;
using flow::NodeKind;
using flow::Style;

// --- small helpers ----------------------------------------------------------------------------------------

ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

ImVec4 toImVec4(Rgba8 c) {
    return ImVec4(static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
                  static_cast<float>(c.b) / 255.0f, static_cast<float>(c.a) / 255.0f);
}

std::uint8_t quantize(float v) {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255));
}

Rgba8 fromFloats(const float rgba[4]) { return Rgba8{quantize(rgba[0]), quantize(rgba[1]), quantize(rgba[2]), quantize(rgba[3])}; }

Rgba8 fromImVec4(const ImVec4& c) { return Rgba8{quantize(c.x), quantize(c.y), quantize(c.z), quantize(c.w)}; }

void toFloats(Rgba8 c, float out[4]) {
    const ImVec4 v = toImVec4(c);
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
    out[3] = v.w;
}

// Muted text that wraps at the panel's right edge.
void hintWrapped(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

// --- icons -------------------------------------------------------------------------------------------------

// Icons are designed on a 24 x 24 grid. Coordinates snap to whole pixels (shifted half a pixel for odd stroke
// widths) so horizontal and vertical strokes stay crisp at any interface scale.
class Pen {
public:
    Pen(ImDrawList* dl, ImVec2 min, float size, ImU32 color)
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
    float thickness() const { return thickness_; }
    float len(float v) const { return v * unit_; }

    void line(float x0, float y0, float x1, float y1) const { dl_->AddLine(pt(x0, y0), pt(x1, y1), color_, thickness_); }
    void poly(std::initializer_list<ImVec2> points, bool closed) const {
        ImVec2 buf[10];
        int n = 0;
        for (const ImVec2& p : points) {
            if (n < 10)
                buf[n++] = pt(p.x, p.y);
        }
        dl_->AddPolyline(buf, n, color_, closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness_);
    }
    void triangleFilled(ImVec2 a, ImVec2 b, ImVec2 c) const {
        dl_->AddTriangleFilled(pt(a.x, a.y), pt(b.x, b.y), pt(c.x, c.y), color_);
    }
    void rect(float x0, float y0, float x1, float y1, float rounding = 0.0f) const {
        dl_->AddRect(pt(x0, y0), pt(x1, y1), color_, len(rounding), 0, thickness_);
    }
    void rectFilled(float x0, float y0, float x1, float y1) const {
        dl_->AddRectFilled(pt(x0, y0), pt(x1, y1), color_);
    }
    void circle(float x, float y, float r, bool filled) const {
        if (filled)
            dl_->AddCircleFilled(pt(x, y), len(r), color_, 16);
        else
            dl_->AddCircle(pt(x, y), len(r), color_, 20, thickness_);
    }
    void curve(ImVec2 a, ImVec2 control, ImVec2 b) const {
        dl_->AddBezierQuadratic(pt(a.x, a.y), pt(control.x, control.y), pt(b.x, b.y), color_, thickness_, 14);
    }

private:
    ImDrawList* dl_;
    ImVec2 min_;
    float unit_;
    ImU32 color_;
    float thickness_;
    float half_ = 0.0f;
};

// The SVG icon of a tool; the Shape tool has none because the Shapes panel shows the shape being placed.
Icon flowToolIcon(FlowTool tool) {
    switch (tool) {
    case FlowTool::Select: return Icon::CursorSelect;
    case FlowTool::Hand: return Icon::HandDrag;
    case FlowTool::Connector: return Icon::Link;
    case FlowTool::Text: return Icon::Text;
    case FlowTool::Sticky: return Icon::StickyPin;
    case FlowTool::Image: return Icon::Photo;
    case FlowTool::Shape: break; // shape icons come from drawShapeIcon
    }
    return Icon::None;
}

// A rounded rectangle with a smooth outline however small it is drawn (flow::outlineFor keeps its arcs coarse, which
// made a small pill look lopsided): `radius` is clamped to half the shorter side, so radius >= h/2 is a stadium.
std::vector<ImVec2> smoothRounded(ImVec2 a, ImVec2 b, float radius) {
    const float r = std::clamp(radius, 0.0f, std::min(b.x - a.x, b.y - a.y) * 0.5f);
    const int steps = std::clamp(static_cast<int>(std::ceil(r * 1.2f)), 6, 24);
    std::vector<ImVec2> out;
    out.reserve(static_cast<std::size_t>(steps + 1) * 4);
    const auto arc = [&](float cx, float cy, float a0) {
        for (int i = 0; i <= steps; ++i) {
            const float t = a0 + 1.5707963f * static_cast<float>(i) / static_cast<float>(steps);
            out.emplace_back(cx + r * std::cos(t), cy + r * std::sin(t));
        }
    };
    arc(a.x + r, a.y + r, 3.1415927f);        // top-left
    arc(b.x - r, a.y + r, 4.712389f);         // top-right
    arc(b.x - r, b.y - r, 0.0f);              // bottom-right
    arc(a.x + r, b.y - r, 1.5707963f);        // bottom-left
    return out;
}

// The silhouette of a shape fitted into the square [min, min + size], centered and sized by kind so the whole set
// reads at one weight: the pill and the rounded rectangle are drawn smooth, the rest come from flow::outlineFor.
void drawShapeIcon(ImDrawList* dl, NodeKind kind, ImVec2 min, float size, ImU32 color) {
    const float thickness = std::max(1.0f, std::round(size / 14.0f));
    const float pad = std::round(size * 0.06f);
    const float area = size - 2.0f * pad;
    float w = area;
    float h = std::round(area * 0.7f);
    switch (kind) {
    case NodeKind::Circle: w = h = std::round(area * 0.86f); break;
    case NodeKind::Polygon: w = h = std::round(area * 0.92f); break;
    case NodeKind::Diamond: h = std::round(area * 0.86f); break;
    case NodeKind::Terminator: h = std::round(area * 0.56f); break;
    case NodeKind::Database: h = std::round(area * 0.94f); w = std::round(area * 0.76f); break;
    case NodeKind::Container: h = std::round(area * 0.86f); break;
    case NodeKind::Document: h = std::round(area * 0.8f); break;
    default: break;
    }
    const float odd = std::fmod(thickness, 2.0f) == 1.0f ? 0.5f : 0.0f;
    const flow::Rect r{std::round(min.x + (size - w) * 0.5f) + odd, std::round(min.y + (size - h) * 0.5f) + odd, w, h};
    const ImVec2 a(r.x, r.y);
    const ImVec2 b(r.right(), r.bottom());

    if (kind == NodeKind::Terminator || kind == NodeKind::Rounded) {
        const std::vector<ImVec2> pts = smoothRounded(a, b, kind == NodeKind::Terminator ? h * 0.5f : h * 0.28f);
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), color, ImDrawFlags_Closed, thickness);
        return;
    }
    if (kind == NodeKind::Ellipse || kind == NodeKind::Circle) {
        dl->AddEllipse(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), ImVec2(w * 0.5f, h * 0.5f), color, 0.0f, 0, thickness);
        return;
    }

    const std::vector<flow::Pt> outline = flow::outlineFor(kind, r, h * 0.28f);
    std::vector<ImVec2> pts;
    pts.reserve(outline.size());
    for (const flow::Pt& q : outline)
        pts.emplace_back(q.x, q.y);
    if (pts.size() >= 2)
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), color, ImDrawFlags_Closed, thickness);

    if (kind == NodeKind::Database) {
        const std::vector<flow::Pt> rim = flow::databaseRim(r);
        std::vector<ImVec2> rp;
        rp.reserve(rim.size());
        for (const flow::Pt& q : rim)
            rp.emplace_back(q.x, q.y);
        if (rp.size() >= 2)
            dl->AddPolyline(rp.data(), static_cast<int>(rp.size()), color, ImDrawFlags_None, thickness);
    } else if (kind == NodeKind::Container) {
        const float y = flow::containerTitleBand(r).bottom();
        dl->AddLine(ImVec2(r.x, y), ImVec2(r.right(), y), color, thickness);
    }
}

enum class ArrangeIcon {
    AlignLeft, AlignCenter, AlignRight, AlignTop, AlignMiddle, AlignBottom,
    DistributeH, DistributeV, MatchWidth, MatchHeight,
    TextLeft, TextCenter, TextRight,
};

// Align, distribute and text alignment use the SVG icons. align_center.svg points two arrows at a horizontal line,
// which is the vertical centering of Middle; turned a quarter, it is the horizontal centering of Center. The
// vertical distribute icon is the horizontal one turned a quarter. Matching sizes has no SVG and stays drawn.
void drawArrangeIcon(ImDrawList* dl, ArrangeIcon icon, ImVec2 min, float size, ImU32 color) {
    switch (icon) {
    case ArrangeIcon::AlignLeft: drawIcon(dl, Icon::AlignLeft, min, size, color); return;
    case ArrangeIcon::AlignCenter: drawIcon(dl, Icon::AlignCenter, min, size, color, 1); return;
    case ArrangeIcon::AlignRight: drawIcon(dl, Icon::AlignRight, min, size, color); return;
    case ArrangeIcon::AlignTop: drawIcon(dl, Icon::AlignTop, min, size, color); return;
    case ArrangeIcon::AlignMiddle: drawIcon(dl, Icon::AlignCenter, min, size, color); return;
    case ArrangeIcon::AlignBottom: drawIcon(dl, Icon::AlignBottom, min, size, color); return;
    case ArrangeIcon::DistributeH: drawIcon(dl, Icon::ArrowGap, min, size, color); return;
    case ArrangeIcon::DistributeV: drawIcon(dl, Icon::ArrowGap, min, size, color, 1); return;
    case ArrangeIcon::TextLeft: drawIcon(dl, Icon::TextJustifyLeft, min, size, color); return;
    case ArrangeIcon::TextCenter: drawIcon(dl, Icon::TextJustifyCenter, min, size, color); return;
    case ArrangeIcon::TextRight: drawIcon(dl, Icon::TextJustifyRight, min, size, color); return;
    case ArrangeIcon::MatchWidth:
    case ArrangeIcon::MatchHeight: break;
    }
    const Pen p(dl, min, size, color);
    if (icon == ArrangeIcon::MatchWidth) {
        p.rect(5, 5, 19, 10);
        p.rect(5, 14, 19, 19);
        p.line(3, 3, 3, 21);
        p.line(21, 3, 21, 21);
    } else {
        p.rect(5, 5, 10, 19);
        p.rect(14, 5, 19, 19);
        p.line(3, 3, 21, 3);
        p.line(3, 21, 21, 21);
    }
}

// --- panel widgets -----------------------------------------------------------------------------------------
// Icon buttons in the look of the other panels (panel_ui.hpp): a rounded plate that is soft on hover and the accent
// when selected, grouped in a track. They take a draw function instead of an Icon because some icons are drawn by
// code (the shapes, the vertical text alignment, match size).

using DrawFn = std::function<void(ImDrawList*, ImVec2, float, ImU32)>;

DrawFn svgIcon(Icon icon, int quarterTurns = 0) {
    return [=](ImDrawList* dl, ImVec2 min, float size, ImU32 color) { drawIcon(dl, icon, min, size, color, quarterTurns); };
}

DrawFn arrangeIcon(ArrangeIcon icon) {
    return [=](ImDrawList* dl, ImVec2 min, float size, ImU32 color) { drawArrangeIcon(dl, icon, min, size, color); };
}

float em() { return ImGui::GetFontSize(); }

ImVec4 blend(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
}

// One plate button. `rest` draws the plate even at rest (a cell of a grid); without it only hover and selection show.
// `iconSide` is the size of the square the icon is drawn in. The tooltip also shows while the button is disabled.
bool plate(const char* id, ImVec2 size, bool selected, bool enabled, const std::string& tip, const DrawFn& draw,
           float iconSide, bool rest) {
    const panel::Colors c = panel::colors();
    const ThemeTokens& t = tokens();
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImGui::EndDisabled();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const bool focused = enabled && ImGui::IsItemFocused();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = std::round(em() * 0.35f);

    ImVec4 ink = t.text;
    if (selected) {
        ImVec4 fill = t.accent;
        if (held)
            fill = blend(fill, ImVec4(0, 0, 0, 1), 0.18f);
        else if (hovered)
            fill = blend(fill, ImVec4(1, 1, 1, 1), 0.14f);
        fill.w = enabled ? 1.0f : 0.5f;
        dl->AddRectFilled(min, max, u32(fill), rounding);
        ink = contrastText(t.accent);
    } else if (held || hovered) {
        dl->AddRectFilled(min, max, u32(held ? blend(c.buttonHover, t.accent, 0.3f) : c.buttonHover), rounding);
    } else if (rest) {
        dl->AddRectFilled(min, max, u32(c.track), rounding);
    }
    if (focused)
        dl->AddRect(min, max, u32(ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.9f)), rounding, 0, 2.0f);

    const float side = std::round(std::min({iconSide, size.x - 4.0f, size.y - 4.0f}));
    if (side > 0.0f) {
        ink.w *= enabled ? (hovered || selected ? 1.0f : 0.88f) : 0.38f;
        draw(dl, ImVec2(std::round(min.x + (size.x - side) * 0.5f), std::round(min.y + (size.y - side) * 0.5f)), side, u32(ink));
    }
    if (!tip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tip.c_str());
    return clicked && enabled;
}

struct RowItem {
    const char* id; // ImGui id, not shown
    DrawFn draw;
    std::string tip;
    bool enabled = true;
    bool selected = false;
};

// The items in one grouped track that shares the width evenly and wraps in a narrow panel (like panel::iconToolbar,
// with custom icons). Returns the index clicked this frame, or -1.
int iconRow(std::span<const RowItem> items) {
    if (items.empty())
        return -1;
    const panel::Colors c = panel::colors();
    const float size = std::round(em() * 2.5f);
    const float pad = std::round(em() * 0.25f);
    const float gap = std::round(em() * 0.2f);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, size + 2.0f * pad);
    // As many columns as fit (each at least `size` wide), the buttons sharing the width evenly, more rows when narrow.
    const int count = static_cast<int>(items.size());
    const float inner = width - 2.0f * pad;
    const int columns = std::clamp(static_cast<int>(std::floor((inner + gap) / (size + gap))), 1, count);
    const int rows = (count + columns - 1) / columns;
    const float buttonWidth = std::max(1.0f, std::floor((inner - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns)));
    const float height = static_cast<float>(rows) * size + static_cast<float>(rows - 1) * gap + 2.0f * pad;
    ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2(start.x + width, start.y + height), u32(c.track),
                                              std::round(em() * 0.35f) + pad * 0.5f);
    int picked = -1;
    ImGui::PushID("##row");
    for (std::size_t i = 0; i < items.size(); ++i) {
        const int col = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        ImGui::SetCursorScreenPos(ImVec2(start.x + pad + static_cast<float>(col) * (buttonWidth + gap),
                                         start.y + pad + static_cast<float>(row) * (size + gap)));
        const RowItem& item = items[i];
        ImGui::PushID(static_cast<int>(i));
        const bool clicked = plate(item.id, ImVec2(buttonWidth, size), item.selected, item.enabled, item.tip, item.draw,
                                     std::round(em() * 1.6f), false);
        ImGui::PopID();
        if (clicked)
            picked = static_cast<int>(i);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height)); // grow the window's content to include the track
    ImGui::Dummy(ImVec2(width, 0.0f));
    return picked;
}

// A small muted caption above a control row.
void caption(const char* text) { ImGui::TextDisabled("%s", text); }

// Square cells that fill the width: as many columns as fit at least `minCell` wide, the cells sharing the rest.
struct CellGrid {
    float cell;
    float gap;
    int columns;
    int n = 0;

    CellGrid(float minCell, int count) {
        gap = std::round(em() * 0.3f);
        const float avail = std::max(ImGui::GetContentRegionAvail().x, minCell);
        columns = std::clamp(static_cast<int>(std::floor((avail + gap) / (minCell + gap))), 1, std::max(count, 1));
        cell = std::max(1.0f, std::floor((avail - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns)));
    }
    void next() {
        if (n % columns != 0)
            ImGui::SameLine(0.0f, gap);
        ++n;
    }
};


// --- Shapes panel data -------------------------------------------------------------------------------------

struct ToolEntry {
    FlowTool tool;
    Str name;
    Action action;
};
constexpr std::array<ToolEntry, 6> kTools{{
    {FlowTool::Select, Str::FlowToolSelect, Action::ToolFlowSelect},
    {FlowTool::Hand, Str::FlowToolHand, Action::ToolFlowHand},
    {FlowTool::Connector, Str::FlowToolConnector, Action::ToolFlowConnector},
    {FlowTool::Text, Str::FlowToolText, Action::ToolFlowText},
    {FlowTool::Sticky, Str::FlowToolSticky, Action::ToolFlowSticky},
    {FlowTool::Image, Str::FlowToolImage, Action::ToolFlowImage},
}};

struct ShapeEntry {
    NodeKind kind;
    Str name;
    Action action; // Action::Count = no shortcut
};
// Everything placeable from the grid; Sticky, Text and Image come from the tools row.
constexpr std::array<ShapeEntry, 12> kShapes{{
    {NodeKind::Rectangle, Str::FlowShapeRectangle, Action::ShapeRectangle},
    {NodeKind::Rounded, Str::FlowShapeRounded, Action::Count},
    {NodeKind::Ellipse, Str::FlowShapeEllipse, Action::ShapeEllipse},
    {NodeKind::Diamond, Str::FlowShapeDiamond, Action::ShapeDiamond},
    {NodeKind::Parallelogram, Str::FlowShapeParallelogram, Action::Count},
    {NodeKind::Terminator, Str::FlowShapeTerminator, Action::Count},
    {NodeKind::Document, Str::FlowShapeDocument, Action::Count},
    {NodeKind::Database, Str::FlowShapeDatabase, Action::Count},
    {NodeKind::Hexagon, Str::FlowShapeHexagon, Action::Count},
    {NodeKind::Circle, Str::FlowShapeCircle, Action::Count},
    {NodeKind::Container, Str::FlowShapeContainer, Action::ShapeContainer},
    {NodeKind::Polygon, Str::FlowShapePolygon, Action::Count},
}};

std::string tipWithShortcut(const Keymap& keys, Str name, Action action) {
    const char* text = tr(name);
    if (action == Action::Count)
        return text;
    const std::string shortcut = displayShortcut(keys, action);
    return shortcut.empty() ? std::string(text) : trf(Str::OptToolTip, {text, shortcut});
}

// --- Properties panel helpers ------------------------------------------------------------------------------

// How a panel edit reaches the history. A one-shot control (checkbox, combo, button) changes the document and
// commits. A control that changes while it is dragged (slider, color picker, text typing) calls live() first:
// the first call opens a history gesture, every later change applies at once, and the panel closes the gesture
// when no widget is active any more, so one drag is one undo step.
struct Edit {
    FlowState& st;
    bool& gestureOpen;

    void live() {
        if (!gestureOpen) {
            st.history.beginGesture();
            gestureOpen = true;
        }
    }
    void shot() { st.commit(); }
};

// The selected nodes (or edges) a control applies to, with a snapshot of their styles taken at the start of the
// frame. Controls read the values from the snapshot and write through the document.
struct StyleGroup {
    std::vector<std::uint32_t> ids;
    std::vector<Style> styles;
    bool edge = false;

    bool empty() const { return ids.empty(); }
    template <class F>
    void apply(FlowState& st, F&& mutate) const {
        for (std::size_t i = 0; i < ids.size(); ++i) {
            Style s = styles[i];
            mutate(s);
            if (edge)
                st.doc.setEdgeStyle(ids[i], s);
            else
                st.doc.setStyle(ids[i], s);
        }
    }
    template <class T>
    bool mixed(T Style::* member) const {
        for (std::size_t i = 1; i < styles.size(); ++i) {
            if (!(styles[i].*member == styles[0].*member))
                return true;
        }
        return false;
    }
};

template <class Pred>
StyleGroup nodeGroup(const FlowState& st, Pred pred) {
    StyleGroup g;
    for (const flow::NodeId id : st.selNodes) {
        const Node* n = st.doc.node(id);
        if (n && pred(*n)) {
            g.ids.push_back(id);
            g.styles.push_back(n->style);
        }
    }
    return g;
}

StyleGroup edgeGroup(const FlowState& st) {
    StyleGroup g;
    g.edge = true;
    for (const flow::EdgeId id : st.selEdges) {
        if (const flow::Edge* e = st.doc.edge(id)) {
            g.ids.push_back(id);
            g.styles.push_back(e->style);
        }
    }
    return g;
}

// The float state of the open color picker, so dragging through gray does not lose the hue to 8-bit rounding.
// Only one popup is open at a time.
float g_pickerRgba[4] = {0, 0, 0, 1};
ImVec4 g_pickerOriginal;

// A color swatch that opens a picker popup with alpha. `shown` is what the swatch displays; a mixed selection
// gets a dash over it. Returns true, with `out` set, while the picker edits the color. Call inside a PushID.
bool colorSwatch(Rgba8 shown, bool mixed, Rgba8& out) {
    const float height = ImGui::GetFrameHeight();
    const ImVec4 shownV = toImVec4(shown);
    const ImVec2 size(std::round(height * 1.6f), height);
    if (ImGui::ColorButton("##swatch", shownV, ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip, size)) {
        toFloats(shown, g_pickerRgba);
        g_pickerOriginal = shownV;
        ImGui::OpenPopup("##picker");
    }
    if (mixed) {
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        const ImVec4 ink = contrastText(ImVec4(shownV.x, shownV.y, shownV.z, 1.0f));
        const float y = std::round((a.y + b.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x + size.x * 0.3f, y), ImVec2(b.x - size.x * 0.3f, y), u32(ink),
                                            std::max(1.0f, std::round(height / 10.0f)));
    }
    ImGui::SetItemTooltip("%s", tr(Str::FlowPropColorTip));

    bool edited = false;
    if (ImGui::BeginPopup("##picker")) {
        if (fromFloats(g_pickerRgba) != shown) // changed by something else (undo, another control): follow it
            toFloats(shown, g_pickerRgba);
        constexpr ImGuiColorEditFlags flags = ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_AlphaBar |
                                              ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_DisplayRGB |
                                              ImGuiColorEditFlags_AlphaPreviewHalf;
        if (ImGui::ColorPicker4("##colorpicker", g_pickerRgba, flags, &g_pickerOriginal.x)) {
            out = fromFloats(g_pickerRgba);
            edited = true;
        }
        ImGui::EndPopup();
    }
    return edited;
}

// A checkbox whose mixed state (some of the selection ticked) shows a dash. Returns true when clicked.
bool autoCheckbox(bool checked, bool mixed) {
    bool v = checked;
    const bool clicked = ImGui::Checkbox(tr(Str::WordAuto), &v);
    if (mixed) {
        // The box is the first frame-height square of the item.
        const ImVec2 a = ImGui::GetItemRectMin();
        const float s = ImGui::GetFrameHeight();
        const float y = std::round(a.y + s * 0.5f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(std::round(a.x + s * 0.3f), y), ImVec2(std::round(a.x + s * 0.7f), y),
                                            ImGui::GetColorU32(ImGuiCol_CheckMark), std::max(1.0f, std::round(s / 10.0f)));
    }
    return clicked;
}

// A color property with an Auto checkbox and a swatch. Auto stores nullopt; the swatch of an Auto color shows the
// theme color it resolves to. Editing the swatch un-ticks Auto.
void colorRow(Edit& e, const StyleGroup& g, Str label, std::optional<Rgba8> Style::* member, const ImVec4& autoColor, int id) {
    ImGui::PushID(id);
    ImGui::TextDisabled("%s", tr(label));

    const Rgba8 autoRgb = fromImVec4(autoColor);
    bool allAuto = true;
    bool anyAuto = false;
    for (const Style& s : g.styles) {
        const bool isAuto = !(s.*member).has_value();
        allAuto = allAuto && isAuto;
        anyAuto = anyAuto || isAuto;
    }
    const bool mixedValue = g.mixed(member);
    const Rgba8 shown = (g.styles[0].*member).value_or(autoRgb);

    Rgba8 picked{};
    if (colorSwatch(shown, mixedValue, picked)) {
        e.live();
        g.apply(e.st, [&](Style& s) { s.*member = picked; });
    }
    ImGui::SameLine();
    if (autoCheckbox(allAuto, anyAuto && !allAuto)) {
        // Auto on when anything is not Auto yet (so a mixed selection becomes all Auto); off gives the resolved color.
        const bool makeAuto = !allAuto;
        g.apply(e.st, [&](Style& s) {
            if (makeAuto)
                s.*member = std::nullopt;
            else
                s.*member = autoRgb;
        });
        e.shot();
    }
    ImGui::PopID();
}

void sliderRow(Edit& e, const StyleGroup& g, Str label, float Style::* member, float lo, float hi, const char* format,
               bool logarithmic, int id) {
    ImGui::PushID(id);
    ImGui::TextDisabled("%s", tr(label));
    float v = g.styles[0].*member;
    ImGui::SetNextItemWidth(-FLT_MIN);
    const ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | (logarithmic ? ImGuiSliderFlags_Logarithmic : 0);
    if (ImGui::SliderFloat("##v", &v, lo, hi, g.mixed(member) ? tr(Str::FlowMixed) : format, flags)) {
        e.live();
        g.apply(e.st, [&](Style& s) { s.*member = v; });
    }
    ImGui::PopID();
}

// A whole-number field (drag left or right, or double-click to type) for values a slider is too coarse for, such as a
// font size. A mixed selection shows "Mixed" until the value is touched.
void numberRow(Edit& e, const StyleGroup& g, Str label, float Style::* member, int lo, int hi, int id) {
    ImGui::PushID(id);
    ImGui::TextDisabled("%s", tr(label));
    int v = static_cast<int>(std::lround(g.styles[0].*member));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::DragInt("##v", &v, 0.5f, lo, hi, g.mixed(member) ? tr(Str::FlowMixed) : "%d px",
                       ImGuiSliderFlags_AlwaysClamp)) {
        e.live();
        g.apply(e.st, [&](Style& s) { s.*member = static_cast<float>(v); });
    }
    ImGui::PopID();
}

// A combo over `items`; the entry index is the enum value. Returns the picked index, or -1 when nothing changed.
int comboBox(const char* id, int current, bool mixed, std::span<const Str> items) {
    int picked = -1;
    const int cur = std::clamp(current, 0, static_cast<int>(items.size()) - 1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(id, mixed ? tr(Str::FlowMixed) : tr(items[static_cast<std::size_t>(cur)]))) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (ImGui::Selectable(tr(items[static_cast<std::size_t>(i)]), !mixed && i == cur) && (mixed || i != cur))
                picked = i;
        }
        ImGui::EndCombo();
    }
    return picked;
}

constexpr std::array<Str, 3> kDashNames{Str::FlowDashSolid, Str::FlowDashDashed, Str::FlowDashDotted};
constexpr std::array<Str, 3> kRouteNames{Str::FlowRouteCurved, Str::FlowRouteElbow, Str::FlowRouteStraight};
constexpr std::array<Str, 4> kArrowNames{Str::FlowArrowNone, Str::FlowArrowArrow, Str::FlowArrowOpen, Str::FlowArrowDot};

void dashRow(Edit& e, const StyleGroup& g, int id) {
    ImGui::PushID(id);
    ImGui::TextDisabled("%s", tr(Str::FlowPropLineStyle));
    const int pick = comboBox("##dash", static_cast<int>(g.styles[0].dash), g.mixed(&Style::dash), kDashNames);
    if (pick >= 0) {
        g.apply(e.st, [&](Style& s) { s.dash = static_cast<flow::Dash>(pick); });
        e.shot();
    }
    ImGui::PopID();
}

// Where text sits in its shape: three icons across (left, center, right) and three down (top, middle, bottom).
void drawTextVAlignIcon(ImDrawList* dl, flow::VAlign v, ImVec2 min, float size, ImU32 color) {
    const Pen p(dl, min, size, color);
    p.rect(4, 4, 20, 20, 1.5f);
    const float y = v == flow::VAlign::Top ? 8.0f : (v == flow::VAlign::Middle ? 10.5f : 13.0f);
    p.line(8, y, 16, y);
    p.line(8, y + 3.5f, 14, y + 3.5f);
}

void textAlignRows(Edit& e, const StyleGroup& g, int id) {
    ImGui::PushID(id);
    {
        caption(tr(Str::FlowPropTextHoriz));
        const bool mixed = g.mixed(&Style::align);
        const flow::TextAlign cur = g.styles[0].align;
        struct Entry {
            flow::TextAlign value;
            Icon icon;
            Str name;
        };
        const std::array<Entry, 3> entries{{{flow::TextAlign::Left, Icon::TextJustifyLeft, Str::MenuFlowLeft},
                                            {flow::TextAlign::Center, Icon::TextJustifyCenter, Str::MenuFlowCenter},
                                            {flow::TextAlign::Right, Icon::TextJustifyRight, Str::MenuFlowRight}}};
        std::vector<RowItem> items;
        for (const Entry& a : entries)
            items.push_back(RowItem{"##h", svgIcon(a.icon), tr(a.name), true, !mixed && cur == a.value});
        const int picked = iconRow(items);
        if (picked >= 0) {
            const flow::TextAlign value = entries[static_cast<std::size_t>(picked)].value;
            g.apply(e.st, [&](Style& s) { s.align = value; });
            e.shot();
        }
    }
    {
        caption(tr(Str::FlowPropTextVert));
        const bool mixed = g.mixed(&Style::valign);
        const flow::VAlign cur = g.styles[0].valign;
        struct Entry {
            flow::VAlign value;
            Str name;
        };
        const std::array<Entry, 3> entries{{{flow::VAlign::Top, Str::FlowTextTop},
                                            {flow::VAlign::Middle, Str::FlowTextMiddle},
                                            {flow::VAlign::Bottom, Str::FlowTextBottom}}};
        std::vector<RowItem> items;
        for (const Entry& a : entries) {
            const flow::VAlign v = a.value;
            items.push_back(RowItem{"##v",
                                    [v](ImDrawList* dl, ImVec2 min, float size, ImU32 color) { drawTextVAlignIcon(dl, v, min, size, color); },
                                    tr(a.name), true, !mixed && cur == a.value});
        }
        const int picked = iconRow(items);
        if (picked >= 0) {
            const flow::VAlign value = entries[static_cast<std::size_t>(picked)].value;
            g.apply(e.st, [&](Style& s) { s.valign = value; });
            e.shot();
        }
    }
    ImGui::PopID();
}

// A text field that grows with its content.
int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(data->UserData);
        s->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = s->data();
    }
    return 0;
}

bool inputString(const char* id, const char* hint, std::string& s) {
    return ImGui::InputTextWithHint(id, hint, s.data(), s.capacity() + 1, ImGuiInputTextFlags_CallbackResize, resizeCallback, &s);
}

// --- sections ----------------------------------------------------------------------------------------------

void drawRouteIcon(ImDrawList* dl, flow::Route route, ImVec2 min, float size, ImU32 color) {
    const Pen p(dl, min, size, color);
    switch (route) {
    case flow::Route::Curved:
        p.curve(ImVec2(4, 19), ImVec2(4, 12), ImVec2(12, 12));
        p.curve(ImVec2(12, 12), ImVec2(20, 12), ImVec2(20, 5));
        break;
    case flow::Route::Elbow: p.poly({ImVec2(4, 19), ImVec2(4, 12), ImVec2(20, 12), ImVec2(20, 5)}, false); break;
    case flow::Route::Straight: p.line(4, 19, 20, 5); break;
    }
    p.circle(4, 19, 1.8f, true);
    p.circle(20, 5, 1.8f, true);
}

void drawLabelSideIcon(ImDrawList* dl, flow::LabelSide side, ImVec2 min, float size, ImU32 color) {
    const Pen p(dl, min, size, color);
    switch (side) {
    case flow::LabelSide::On:
        p.line(2, 12, 7, 12);
        p.line(17, 12, 22, 12);
        p.rectFilled(8, 9, 16, 15);
        break;
    case flow::LabelSide::Above:
        p.line(2, 17, 22, 17);
        p.rectFilled(7, 6, 17, 12);
        break;
    case flow::LabelSide::Below:
        p.line(2, 7, 22, 7);
        p.rectFilled(7, 12, 17, 18);
        break;
    }
}

struct ColorPreset {
    Str name;
    Rgba8 color;
};
// Mid-tone colors that read on both the dark and the light board, for color coding shapes and connectors.
constexpr std::array<ColorPreset, 8> kColorPresets{{
    {Str::FlowColorRed, Rgba8{229, 83, 75, 255}},
    {Str::FlowColorOrange, Rgba8{240, 150, 50, 255}},
    {Str::FlowColorYellow, Rgba8{230, 190, 40, 255}},
    {Str::FlowColorGreen, Rgba8{70, 170, 90, 255}},
    {Str::FlowColorTeal, Rgba8{40, 170, 170, 255}},
    {Str::FlowColorBlue, Rgba8{66, 133, 244, 255}},
    {Str::FlowColorPurple, Rgba8{150, 100, 220, 255}},
    {Str::FlowColorGray, Rgba8{140, 145, 155, 255}},
}};

// A wrapping row of the preset colors. `current` (when all agree) gets a ring. Returns the picked index or -1.
int presetRow(std::optional<Rgba8> current, bool mixed, int id) {
    ImGui::PushID(id);
    const float cell = std::round(ImGui::GetFrameHeight());
    CellGrid grid(cell, static_cast<int>(kColorPresets.size()));
    int picked = -1;
    for (int i = 0; i < static_cast<int>(kColorPresets.size()); ++i) {
        grid.next();
        ImGui::PushID(i);
        const ColorPreset& c = kColorPresets[static_cast<std::size_t>(i)];
        if (ImGui::ColorButton("##preset", toImVec4(c.color), ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip,
                               ImVec2(grid.cell, grid.cell)))
            picked = i;
        ImGui::SetItemTooltip("%s", tr(c.name));
        if (!mixed && current && *current == c.color) {
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(a, b, u32(tokens().text), ImGui::GetStyle().FrameRounding, 0, 2.0f);
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return picked;
}

// The preset row under a color property: a click sets the color on the whole group, as one undo step.
void presetsFor(Edit& e, const StyleGroup& g, std::optional<Rgba8> Style::* member, int id) {
    ImGui::TextDisabled("%s", tr(Str::FlowPropColorCode));
    const bool mixed = g.mixed(member);
    const int picked = presetRow(g.styles[0].*member, mixed, id);
    if (picked >= 0) {
        const Rgba8 color = kColorPresets[static_cast<std::size_t>(picked)].color;
        g.apply(e.st, [&](Style& s) { s.*member = color; });
        e.shot();
    }
}

// The route type icons, three across; returns the type clicked, or -1.
int routeRow(flow::Route current, bool mixed) {
    std::vector<RowItem> items;
    for (int i = 0; i < static_cast<int>(kRouteNames.size()); ++i) {
        const flow::Route r = static_cast<flow::Route>(i);
        items.push_back(RowItem{"##route",
                                [r](ImDrawList* dl, ImVec2 min, float size, ImU32 color) { drawRouteIcon(dl, r, min, size, color); },
                                tr(kRouteNames[static_cast<std::size_t>(i)]), true, !mixed && current == r});
    }
    return iconRow(items);
}

void routeControls(flow::Route& route, flow::Arrow& start, flow::Arrow& end, bool mixedRoute, bool mixedStart, bool mixedEnd,
                   bool& routeChanged, bool& startChanged, bool& endChanged) {
    caption(tr(Str::FlowPropRoute));
    if (const int picked = routeRow(route, mixedRoute); picked >= 0) {
        route = static_cast<flow::Route>(picked);
        routeChanged = true;
    }
    int pick = 0;
    caption(tr(Str::FlowPropStartArrow));
    pick = comboBox("##arrowstart", static_cast<int>(start), mixedStart, kArrowNames);
    if (pick >= 0) {
        start = static_cast<flow::Arrow>(pick);
        startChanged = true;
    }
    caption(tr(Str::FlowPropEndArrow));
    pick = comboBox("##arrowend", static_cast<int>(end), mixedEnd, kArrowNames);
    if (pick >= 0) {
        end = static_cast<flow::Arrow>(pick);
        endChanged = true;
    }
}

// Nothing selected: the defaults a new connector starts with.
void defaultsSection(FlowState& st) {
    hintWrapped(tr(Str::FlowPropNone));
    panel::sectionHeader(tr(Str::FlowPropNewConnectors));
    if (panel::beginCard("##defaults")) {
        bool r = false, s = false, e = false;
        routeControls(st.defaultRoute, st.defaultArrowStart, st.defaultArrowEnd, false, false, false, r, s, e);
        caption(tr(Str::FlowPropColorCode));
        const int picked = presetRow(st.defaultEdgeStroke, false, 100);
        if (picked >= 0)
            st.defaultEdgeStroke = kColorPresets[static_cast<std::size_t>(picked)].color;
        if (st.defaultEdgeStroke && ImGui::SmallButton(tr(Str::WordAuto)))
            st.defaultEdgeStroke = std::nullopt;
    }
    panel::endCard();
}

void nodeSection(Edit& e) {
    FlowState& st = e.st;
    const ThemeTokens& t = tokens();
    const StyleGroup all = nodeGroup(st, [](const Node&) { return true; });
    if (all.empty())
        return;
    const StyleGroup fill = nodeGroup(st, [](const Node& n) { return n.kind != NodeKind::Text && n.kind != NodeKind::Image; });
    const StyleGroup line = nodeGroup(st, [](const Node& n) { return n.kind != NodeKind::Text; });
    const StyleGroup text = nodeGroup(st, [](const Node& n) { return n.kind != NodeKind::Image; });
    const StyleGroup rounded = nodeGroup(st, [](const Node& n) { return n.kind == NodeKind::Rounded; });
    const StyleGroup polygons = nodeGroup(st, [](const Node& n) { return n.kind == NodeKind::Polygon; });
    const bool onlyRounded = rounded.ids.size() == all.ids.size(); // only when every selected shape is a rounded rectangle
    const bool onlyPolygons = polygons.ids.size() == all.ids.size(); // only when every selected shape is a polygon

    ImGui::PushID("nodes");
    if (!fill.empty() || !line.empty() || onlyRounded || onlyPolygons) {
        panel::sectionHeader(tr(Str::FlowPropShape));
        if (!fill.empty() || onlyRounded || onlyPolygons) {
            if (panel::beginCard("##fill")) {
                if (!fill.empty()) {
                    colorRow(e, fill, Str::FlowPropFill, &Style::fill, t.flowPaperAuto, 0);
                    presetsFor(e, fill, &Style::fill, 10);
                }
                if (onlyRounded)
                    sliderRow(e, rounded, Str::FlowPropCornerRadius, &Style::cornerRadius, 0.0f, 200.0f, "%.0f", false, 4);
                if (onlyPolygons) {
                    ImGui::PushID(8);
                    caption(tr(Str::FlowPropSides));
                    const Node* first = st.doc.node(polygons.ids[0]);
                    bool mixedSides = false;
                    for (const std::uint32_t id : polygons.ids) {
                        const Node* n = st.doc.node(id);
                        mixedSides = mixedSides || (n && first && n->sides != first->sides);
                    }
                    int sides = first ? first->sides : flow::kDefaultPolygonSides;
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::SliderInt("##sides", &sides, flow::kMinPolygonSides, flow::kMaxPolygonSides,
                                         mixedSides ? tr(Str::FlowMixed) : "%d", ImGuiSliderFlags_AlwaysClamp)) {
                        e.live();
                        for (const std::uint32_t id : polygons.ids)
                            st.doc.setSides(id, sides);
                    }
                    ImGui::PopID();
                }
            }
            panel::endCard();
        }
        if (!line.empty()) {
            if (panel::beginCard("##line")) {
                colorRow(e, line, Str::FlowPropLine, &Style::stroke, t.flowInkAuto, 1);
                presetsFor(e, line, &Style::stroke, 11);
                sliderRow(e, line, Str::FlowPropLineWidth, &Style::strokeWidth, 0.5f, 32.0f, "%.1f", false, 2);
                dashRow(e, line, 3);
            }
            panel::endCard();
        }
    }
    if (!text.empty()) {
        panel::sectionHeader(tr(Str::FlowPropText));
        if (panel::beginCard("##text")) {
            colorRow(e, text, Str::FlowPropTextColor, &Style::textColor, t.flowInkAuto, 5);
            numberRow(e, text, Str::FlowPropFontSize, &Style::fontSize, 6, 200, 6);
            textAlignRows(e, text, 7);
        }
        panel::endCard();
    }
    ImGui::PopID();
}

void edgeSection(Edit& e, std::string& labelBuf, bool& labelEditing) {
    FlowState& st = e.st;
    const ThemeTokens& t = tokens();
    const StyleGroup g = edgeGroup(st);
    if (g.empty())
        return;

    ImGui::PushID("edges");
    panel::sectionHeader(tr(Str::FlowPropConnector));

    // Route and arrowheads: read from the first edge, mixed when any edge differs.
    const flow::Edge* first = st.doc.edge(static_cast<flow::EdgeId>(g.ids[0]));
    bool mixedRoute = false, mixedStart = false, mixedEnd = false, mixedLabel = false, anyManual = false;
    for (const std::uint32_t id : g.ids) {
        const flow::Edge* o = st.doc.edge(static_cast<flow::EdgeId>(id));
        if (!first || !o)
            continue;
        mixedRoute = mixedRoute || o->route != first->route;
        mixedStart = mixedStart || o->arrowStart != first->arrowStart;
        mixedEnd = mixedEnd || o->arrowEnd != first->arrowEnd;
        mixedLabel = mixedLabel || o->label != first->label;
        anyManual = anyManual || o->via.has_value();
    }
    if (first) {
        if (panel::beginCard("##route")) {
            flow::Route route = first->route;
            flow::Arrow start = first->arrowStart;
            flow::Arrow end = first->arrowEnd;
            bool routeChanged = false, startChanged = false, endChanged = false;
            routeControls(route, start, end, mixedRoute, mixedStart, mixedEnd, routeChanged, startChanged, endChanged);
            if (routeChanged || startChanged || endChanged) {
                for (const std::uint32_t id : g.ids) {
                    const flow::Edge* o = st.doc.edge(static_cast<flow::EdgeId>(id));
                    if (!o)
                        continue;
                    st.doc.setEdgeRoute(o->id, routeChanged ? route : o->route, startChanged ? start : o->arrowStart,
                                        endChanged ? end : o->arrowEnd);
                }
                e.shot();
            }

            // Drag the diamond on a selected connector to shape its route; this goes back to the automatic one.
            ImGui::Spacing();
            ImGui::BeginDisabled(!anyManual);
            if (iconTextButton("##resetroute", Icon::Reset, tr(Str::FlowResetRoute), ImVec2(-FLT_MIN, 0.0f))) {
                bool changed = false;
                for (const std::uint32_t id : g.ids)
                    changed = st.doc.setEdgeVia(static_cast<flow::EdgeId>(id), std::nullopt) || changed;
                if (changed)
                    e.shot();
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("%s", tr(Str::FlowRouteHandleTip));
        }
        panel::endCard();

        if (panel::beginCard("##label")) {
            // The label applies as it is typed (one undo step per editing session, see Edit).
            caption(tr(Str::FlowPropLabel));
            if (!labelEditing) // while the field is active it owns the text
                labelBuf = mixedLabel ? std::string() : first->label;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (inputString("##label", mixedLabel ? tr(Str::FlowMixed) : "", labelBuf)) {
                e.live();
                for (const std::uint32_t id : g.ids)
                    st.doc.setEdgeLabel(static_cast<flow::EdgeId>(id), labelBuf);
            }
            labelEditing = ImGui::IsItemActive();

            // Where the label sits: beside or on the line, turned with it or not, and how far along it is. A label can
            // also be dragged on the board, which sets the same values.
            bool mixedSide = false, mixedAlong = false, mixedAt = false;
            for (const std::uint32_t id : g.ids) {
                const flow::Edge* o = st.doc.edge(static_cast<flow::EdgeId>(id));
                if (!o)
                    continue;
                mixedSide = mixedSide || o->labelSide != first->labelSide;
                mixedAlong = mixedAlong || o->labelAlong != first->labelAlong;
                mixedAt = mixedAt || o->labelPos != first->labelPos;
            }
            const auto place = [&](auto&& change) {
                for (const std::uint32_t id : g.ids) {
                    const flow::Edge* o = st.doc.edge(static_cast<flow::EdgeId>(id));
                    if (o)
                        change(*o);
                }
            };
            caption(tr(Str::FlowLabelPlacement));
            {
                static constexpr std::array<Str, 3> kSideNames{Str::FlowLabelOnLine, Str::FlowLabelAbove, Str::FlowLabelBelow};
                std::vector<RowItem> items;
                for (int i = 0; i < 3; ++i) {
                    const flow::LabelSide side = static_cast<flow::LabelSide>(i);
                    items.push_back(RowItem{"##side",
                                            [side](ImDrawList* dl, ImVec2 min, float size, ImU32 color) { drawLabelSideIcon(dl, side, min, size, color); },
                                            tr(kSideNames[static_cast<std::size_t>(i)]), true, !mixedSide && first->labelSide == side});
                }
                if (const int picked = iconRow(items); picked >= 0) {
                    const flow::LabelSide side = static_cast<flow::LabelSide>(picked);
                    place([&](const flow::Edge& o) { st.doc.setEdgeLabelPlacement(o.id, o.labelPos, side, o.labelAlong); });
                    e.shot();
                }
            }
            bool along = first->labelAlong;
            if (ImGui::Checkbox(tr(Str::FlowLabelAlong), &along)) {
                // A mixed selection turns all on first.
                const bool value = mixedAlong ? true : along;
                place([&](const flow::Edge& o) { st.doc.setEdgeLabelPlacement(o.id, o.labelPos, o.labelSide, value); });
                e.shot();
            }
            if (mixedAlong) {
                const ImVec2 a = ImGui::GetItemRectMin();
                const float s = ImGui::GetFrameHeight();
                const float y = std::round(a.y + s * 0.5f);
                ImGui::GetWindowDrawList()->AddLine(ImVec2(std::round(a.x + s * 0.3f), y), ImVec2(std::round(a.x + s * 0.7f), y),
                                                    ImGui::GetColorU32(ImGuiCol_CheckMark), std::max(1.0f, std::round(s / 10.0f)));
            }
            caption(tr(Str::FlowLabelAt));
            float percent = first->labelPos * 100.0f;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderFloat("##labelat", &percent, 0.0f, 100.0f, mixedAt ? tr(Str::FlowMixed) : "%.0f%%",
                                   ImGuiSliderFlags_AlwaysClamp)) {
                e.live();
                place([&](const flow::Edge& o) { st.doc.setEdgeLabelPlacement(o.id, percent * 0.01f, o.labelSide, o.labelAlong); });
            }
        }
        panel::endCard();
    }

    if (panel::beginCard("##edgeline")) {
        colorRow(e, g, Str::FlowPropLine, &Style::stroke, t.flowInkAuto, 0);
        presetsFor(e, g, &Style::stroke, 10);
        sliderRow(e, g, Str::FlowPropLineWidth, &Style::strokeWidth, 0.5f, 32.0f, "%.1f", false, 1);
        dashRow(e, g, 2);
    }
    panel::endCard();
    ImGui::PopID();
}

void arrangeSection(Edit& e) {
    FlowState& st = e.st;
    flow::FlowDocument& doc = st.doc;
    const std::span<const flow::NodeId> ids(st.selNodes);
    const std::size_t units = flow::layoutUnitCount(doc, ids);
    const bool two = st.selNodes.size() >= 2;

    ImGui::PushID("arrange");
    panel::sectionHeader(tr(Str::FlowPropArrange));
    if (panel::beginCard("##arrange")) {
        if (units < 2)
            hintWrapped(tr(Str::FlowArrangeHint));

        // Align: the six edges and centers of the selection.
        caption(tr(Str::MenuFlowAlign));
        struct Align {
            flow::AlignMode mode;
            ArrangeIcon icon;
            Str tip;
        };
        const std::array<Align, 6> aligns{{{flow::AlignMode::Left, ArrangeIcon::AlignLeft, Str::FlowAlignLeft},
                                           {flow::AlignMode::Center, ArrangeIcon::AlignCenter, Str::FlowAlignCenter},
                                           {flow::AlignMode::Right, ArrangeIcon::AlignRight, Str::FlowAlignRight},
                                           {flow::AlignMode::Top, ArrangeIcon::AlignTop, Str::FlowAlignTop},
                                           {flow::AlignMode::Middle, ArrangeIcon::AlignMiddle, Str::FlowAlignMiddle},
                                           {flow::AlignMode::Bottom, ArrangeIcon::AlignBottom, Str::FlowAlignBottom}}};
        {
            std::vector<RowItem> items;
            for (const Align& a : aligns)
                items.push_back(RowItem{"##align", arrangeIcon(a.icon), tr(a.tip), units >= 2, false});
            const int picked = iconRow(items);
            if (picked >= 0 && flow::alignNodes(doc, ids, aligns[static_cast<std::size_t>(picked)].mode))
                e.shot();
        }

        // Distribute (needs three) and match size (needs two) share one row.
        caption((std::string(tr(Str::MenuFlowDistribute)) + " / " + tr(Str::MenuFlowMatchSize)).c_str());
        {
            std::vector<RowItem> items;
            items.push_back(RowItem{"##disth", arrangeIcon(ArrangeIcon::DistributeH), tr(Str::FlowDistributeH), units >= 3, false});
            items.push_back(RowItem{"##distv", arrangeIcon(ArrangeIcon::DistributeV), tr(Str::FlowDistributeV), units >= 3, false});
            items.push_back(RowItem{"##matchw", arrangeIcon(ArrangeIcon::MatchWidth), tr(Str::FlowMatchWidth), two, false});
            items.push_back(RowItem{"##matchh", arrangeIcon(ArrangeIcon::MatchHeight), tr(Str::FlowMatchHeight), two, false});
            const int picked = iconRow(items);
            if (picked == 0 && flow::distributeNodes(doc, ids, flow::DistributeAxis::Horizontal))
                e.shot();
            else if (picked == 1 && flow::distributeNodes(doc, ids, flow::DistributeAxis::Vertical))
                e.shot();
            else if (picked == 2 && flow::matchSize(doc, ids, flow::MatchMode::Width))
                e.shot();
            else if (picked == 3 && flow::matchSize(doc, ids, flow::MatchMode::Height))
                e.shot();
        }

        // Group needs two nodes that are not already exactly one group; Ungroup needs a grouped node.
        flow::GroupId common = flow::kNoGroup;
        bool sameGroup = true;
        bool anyGrouped = false;
        std::vector<flow::GroupId> groups;
        for (const flow::NodeId id : st.selNodes) {
            const Node* n = doc.node(id);
            if (!n)
                continue;
            if (n->groupId != flow::kNoGroup) {
                anyGrouped = true;
                if (std::find(groups.begin(), groups.end(), n->groupId) == groups.end())
                    groups.push_back(n->groupId);
            }
            if (id == st.selNodes.front())
                common = n->groupId;
            else if (n->groupId != common)
                sameGroup = false;
        }
        const bool alreadyOneGroup =
            sameGroup && common != flow::kNoGroup && doc.groupMembers(common).size() == st.selNodes.size();

        // Stacking order, then grouping.
        caption((std::string(tr(Str::FlowArrangeOrder)) + " / " + tr(Str::FlowArrangeGrouping)).c_str());
        {
            const bool any = !st.selNodes.empty();
            std::vector<RowItem> items;
            items.push_back(RowItem{"##front", svgIcon(Icon::FlowchartBringForward), tr(Str::ActionBringToFront), any, false});
            items.push_back(RowItem{"##back", svgIcon(Icon::FlowchartBringBack), tr(Str::ActionSendToBack), any, false});
            items.push_back(RowItem{"##forward", svgIcon(Icon::ItemForward), tr(Str::ActionBringForward), any, false});
            items.push_back(RowItem{"##backward", svgIcon(Icon::ItemBackward), tr(Str::ActionSendBackward), any, false});
            items.push_back(RowItem{"##group", svgIcon(Icon::LinkAdd), tr(Str::ActionGroupNodes), two && !alreadyOneGroup, false});
            items.push_back(RowItem{"##ungroup", svgIcon(Icon::LinkBroken), tr(Str::ActionUngroupNodes), anyGrouped, false});
            switch (iconRow(items)) {
            case 0: if (doc.bringToFront(ids)) e.shot(); break;
            case 1: if (doc.sendToBack(ids)) e.shot(); break;
            case 2: if (doc.bringForward(ids)) e.shot(); break;
            case 3: if (doc.sendBackward(ids)) e.shot(); break;
            case 4: if (doc.group(ids) != flow::kNoGroup) e.shot(); break;
            case 5: {
                bool changed = false;
                for (const flow::GroupId gid : groups)
                    changed = doc.ungroup(gid) || changed;
                if (changed)
                    e.shot();
                break;
            }
            default: break;
            }
        }
    }
    panel::endCard();
    ImGui::PopID();
}

} // namespace

// --- FlowShapesPanel ---------------------------------------------------------------------------------------

void FlowShapesPanel::draw(FlowState& state, const Keymap& keys) {
    panel::sectionHeader(tr(Str::FlowSectionTools));
    {
        std::vector<RowItem> items;
        for (const ToolEntry& t : kTools) {
            const FlowTool tool = t.tool;
            items.push_back(RowItem{"##tool", svgIcon(flowToolIcon(tool)), tipWithShortcut(keys, t.name, t.action), true,
                                    state.tool == tool});
        }
        const int picked = iconRow(items);
        if (picked >= 0) {
            const FlowTool tool = kTools[static_cast<std::size_t>(picked)].tool;
            state.tool = tool;
            if (tool == FlowTool::Image)
                state.importImageRequested = true;
        }
    }

    panel::sectionHeader(tr(Str::FlowSectionShapes));
    if (panel::beginCard("##shapes")) {
        CellGrid grid(std::round(em() * 3.2f), static_cast<int>(kShapes.size()));
        for (const ShapeEntry& s : kShapes) {
            grid.next();
            ImGui::PushID(static_cast<int>(s.kind));
            const bool active = state.tool == FlowTool::Shape && state.shapeKind == s.kind;
            const NodeKind kind = s.kind;
            if (plate("##shape", ImVec2(grid.cell, grid.cell), active, true, tipWithShortcut(keys, s.name, s.action),
                      [kind](ImDrawList* dl, ImVec2 min, float size, ImU32 color) { drawShapeIcon(dl, kind, min, size, color); },
                      std::round(grid.cell * 0.78f), true)) {
                state.tool = FlowTool::Shape;
                state.shapeKind = s.kind;
            }
            ImGui::PopID();
        }

        // The polygon's corner count is chosen before placing it (and changed later in Properties).
        if (state.tool == FlowTool::Shape && state.shapeKind == NodeKind::Polygon) {
            ImGui::Spacing();
            caption(tr(Str::FlowPropSides));
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt("##polysides", &state.polygonSides, flow::kMinPolygonSides, flow::kMaxPolygonSides, "%d",
                             ImGuiSliderFlags_AlwaysClamp);
        }
    }
    panel::endCard();
}

// --- FlowPropertiesPanel -----------------------------------------------------------------------------------

void FlowPropertiesPanel::draw(FlowState& state) {
    // Close the gesture of a slider, picker or text edit once nothing is being dragged or typed any more.
    if (gestureOpen_ && !ImGui::IsAnyItemActive()) {
        state.history.endGesture(state.doc);
        gestureOpen_ = false;
    }

    state.pruneSelection();
    if (!state.hasSelection()) {
        defaultsSection(state);
        return;
    }

    Edit edit{state, gestureOpen_};
    if (!state.selNodes.empty())
        nodeSection(edit);
    if (!state.selEdges.empty())
        edgeSection(edit, labelBuf_, labelEditing_);
    if (!state.selNodes.empty())
        arrangeSection(edit);
}

} // namespace trace
