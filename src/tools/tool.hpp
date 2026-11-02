#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include "core/color.hpp"
#include "core/document.hpp"
#include "core/geometry.hpp"
#include "core/symmetry.hpp"
#include "core/symmetry_set.hpp"
#include "core/text_raster.hpp"

namespace trace {

enum class ToolId { Pencil, Brush, Eraser, Line, Rectangle, Ellipse, Fill, Picker, Select, Text };
inline constexpr int kToolCount = 10;

enum class ShapeMode {
    Outline, // stroke in the active color
    Filled,  // interior in the active color, no stroke
    Both,    // stroke in the active color, interior in the other one
};

// The typeface of the Text tool: the interface's Inter, or JetBrains Mono.

struct ToolSettings {
    static constexpr int kMinSize = 1;
    static constexpr int kMaxSize = 200;
    static constexpr int kMaxStrokeWidth = 50; // line, rectangle and ellipse outline
    static constexpr int kMaxPixelSize = 16;   // brush and outline size in the pixel workspace
    static constexpr int kMinHardness = 1;
    static constexpr int kMinTextSize = 6;     // Text tool: height of a line, in pixels
    static constexpr int kMaxTextSize = 200;

    Rgba8 primary{0, 0, 0, 255};
    Rgba8 secondary{255, 255, 255, 255};

    int brushSize = 6;    // brush and eraser diameter in pixels
    int brushHardness = 100; // brush and eraser edge, 1..100: 100 is a crisp edge, lower fades it (antialiasing only)
    int strokeWidth = 2;  // line, rectangle and ellipse outline width in pixels
    bool antialias = true;
    bool eraseToTransparent = false; // eraser paints transparency instead of the secondary color
    ShapeMode shapeMode = ShapeMode::Outline;
    int fillTolerance = 0; // 0..255
    int textSize = 24;     // Text tool: line height in pixels
    std::string textFontFamily = "Inter"; // Text tool: font family, "Inter" and "JetBrains Mono" are built in
    std::string textFontStyle = "Regular";  // and the style inside the family (the nearest one is used when missing)
    TextAlign textAlign = TextAlign::Left; // Text tool: how the lines of a several-line text line up

    // Pixel art options. All off in Paint, so its tools behave as they always did.
    bool squareBrush = false; // brush and eraser stamp a square instead of a disc
    bool pixelPerfect = false; // pencil: a stroke loses the corner pixel of every L-shaped step
    bool fillGlobal = false;   // fill: every matching pixel of the image, not only the connected region

    // Symmetry drawing: a list of mirror lines and rotation centers (see core/symmetry_set.hpp for the model and
    // core/symmetry.hpp for the drawing math). Session state: not saved in settings.ini.
    SymmetrySet symmetry;

    bool operator==(const ToolSettings&) const = default;
};

// The tip of the brush and the eraser as the brush style menu offers it: a hard disc, a hard square, or a disc whose
// edge fades (brushHardness below 100, antialiasing only). The two settings behind it stay the source of truth, so
// older settings files and the Pixel Art options keep working.
enum class BrushStyle { Round, Square, Soft };

inline BrushStyle brushStyleOf(const ToolSettings& s) noexcept {
    if (s.squareBrush)
        return BrushStyle::Square;
    return s.brushHardness < 100 ? BrushStyle::Soft : BrushStyle::Round;
}

// Picks a style. Soft starts at half hardness when the brush was hard, and keeps a hardness the user set before.
inline void setBrushStyle(ToolSettings& s, BrushStyle style) noexcept {
    s.squareBrush = style == BrushStyle::Square;
    if (style == BrushStyle::Soft)
        s.brushHardness = s.brushHardness >= 100 ? 50 : std::max(s.brushHardness, ToolSettings::kMinHardness);
    else
        s.brushHardness = 100;
}

// Whether the eraser, a lifted selection and Delete leave transparency on the active layer rather than the secondary
// color. Always so above the bottom layer: a solid color there would hide every layer under it.
inline bool clearsToTransparency(const ToolSettings& s, const Document& doc) noexcept {
    return s.eraseToTransparent || doc.activeLayer() > 0 || doc.endless();
}

// A pointer position in image coordinates (fractional; pixel (0, 0) covers [0, 1) x [0, 1)).
struct PointerEvent {
    Vec2f pos;
    bool shift = false;     // constrain: 45-degree lines, squares and circles
    bool secondary = false; // right button: swap primary and secondary colors
    // How far (in image pixels) shapes, the selection marquee and moved selections are pulled onto a guide;
    // 0 when snapping is off.
    float snapRadius = 0.0f;
};

// A tool receives one press, any number of drags and one release (or a cancel) per gesture. Tools edit
// the document directly and mark what they touched dirty.
class Tool {
public:
    virtual ~Tool() = default;

    virtual void press(Document& doc, ToolSettings& settings, const PointerEvent& ev) = 0;
    virtual void drag(Document& doc, ToolSettings& settings, const PointerEvent& ev) = 0;
    virtual void release(Document& doc, ToolSettings& settings, const PointerEvent& ev) = 0;

    // Escape pressed: undo any uncommitted preview. Freehand strokes are already committed pixel by
    // pixel, so for them this only ends the gesture.
    virtual void cancel(Document& doc) = 0;

    // The gesture's image was replaced: drop state without touching the document.
    virtual void abort() noexcept = 0;

    // Radius of the brush ring drawn under the cursor, in image pixels; 0 for none.
    virtual float cursorRadius(const ToolSettings& settings) const noexcept {
        (void)settings;
        return 0.0f;
    }
};

std::unique_ptr<Tool> createTool(ToolId id);

} // namespace trace
