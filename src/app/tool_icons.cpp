#include "app/tool_icons.hpp"

#include <algorithm>
#include <cmath>

namespace trace {

namespace {

// The dots and chevrons are designed on a 24 x 24 grid and scaled to the requested size.
class Canvas {
public:
    // Coordinates are snapped to whole pixels, shifted by half a pixel when the stroke is an odd number of
    // pixels wide, so strokes cover complete pixels instead of two blurry halves.
    Canvas(ImDrawList* dl, ImVec2 min, float size, ImU32 color)
        : dl_(dl),
          min_(std::round(min.x), std::round(min.y)),
          unit_(size / 24.0f),
          color_(color),
          thickness_(std::max(1.0f, std::round(size / 13.0f))) {
        half_ = std::fmod(thickness_, 2.0f) == 1.0f ? 0.5f : 0.0f;
    }

    void polyline(ImVec2 a, ImVec2 b, ImVec2 c) const {
        const ImVec2 points[3] = {pt(a), pt(b), pt(c)};
        dl_->AddPolyline(points, 3, color_, ImDrawFlags_None, thickness_);
    }
    void dot(float x, float y, float r) const { dl_->AddCircleFilled(pt(ImVec2(x, y)), r * unit_, color_, 16); }

private:
    ImVec2 pt(ImVec2 p) const {
        return ImVec2(min_.x + std::round(p.x * unit_) + half_, min_.y + std::round(p.y * unit_) + half_);
    }

    ImDrawList* dl_;
    ImVec2 min_;
    float unit_;
    ImU32 color_;
    float thickness_;
    float half_ = 0.0f;
};

} // namespace

Icon toolIcon(ToolId tool) {
    switch (tool) {
    case ToolId::Pencil: return Icon::PencilTool;
    case ToolId::Brush: return Icon::BrushTool;
    case ToolId::Eraser: return Icon::Eraser;
    case ToolId::Line: return Icon::SingleLine;
    case ToolId::Rectangle: return Icon::Rectangle;
    case ToolId::Ellipse: return Icon::Ellipse;
    case ToolId::Fill: return Icon::BucketTool;
    case ToolId::Picker: return Icon::EyeDropperTool;
    case ToolId::Select: return Icon::Marquee;
    case ToolId::Text: return Icon::Text;
    }
    return Icon::None;
}

Icon settingsIcon(int page) {
    switch (page) {
    case 0: return Icon::Settings;
    case 1: return Icon::DarkTheme;
    case 2: return Icon::EyeOpenDisplayLayer;
    case 3: return Icon::SlidersHorizontal;
    case 4: return Icon::PixelartIcon;
    case 5: return Icon::FlowchartIcon;
    case 6: return Icon::Scripts;
    case 7: return Icon::Keybinds;
    case 8: return Icon::Info;
    default: return Icon::None;
    }
}

void drawToolIcon(ImDrawList* dl, ToolId tool, ImVec2 min, float size, ImU32 color) {
    drawIcon(dl, toolIcon(tool), min, size, color);
}

void drawSettingsIcon(ImDrawList* dl, int page, ImVec2 min, float size, ImU32 color) {
    drawIcon(dl, settingsIcon(page), min, size, color);
}

void drawUiIcon(ImDrawList* dl, UiIcon icon, ImVec2 min, float size, ImU32 color) {
    switch (icon) {
    case UiIcon::Eye: drawIcon(dl, Icon::EyeOpenDisplayLayer, min, size, color); break;
    case UiIcon::EyeOff: drawIcon(dl, Icon::EyeClosedHideLayer, min, size, color); break;
    case UiIcon::Lock: drawIcon(dl, Icon::Locked, min, size, color); break;
    case UiIcon::Unlock: drawIcon(dl, Icon::Unlocked, min, size, color); break;
    case UiIcon::Trash: drawIcon(dl, Icon::Delete, min, size, color); break;
    case UiIcon::Kebab: {
        const Canvas c(dl, min, size, color);
        c.dot(12.0f, 4.5f, 2.0f);
        c.dot(12.0f, 12.0f, 2.0f);
        c.dot(12.0f, 19.5f, 2.0f);
        break;
    }
    case UiIcon::ChevronRight: {
        const Canvas c(dl, min, size, color);
        c.polyline(ImVec2(8.5f, 5.5f), ImVec2(15.5f, 12.0f), ImVec2(8.5f, 18.5f));
        break;
    }
    case UiIcon::ChevronDown: {
        const Canvas c(dl, min, size, color);
        c.polyline(ImVec2(5.5f, 8.5f), ImVec2(12.0f, 15.5f), ImVec2(18.5f, 8.5f));
        break;
    }
    }
}

} // namespace trace
