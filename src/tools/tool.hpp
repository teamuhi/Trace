#pragma once

#include <memory>

#include "core/color.hpp"
#include "core/document.hpp"
#include "core/geometry.hpp"

namespace trace {

enum class ToolId { Pencil, Brush, Eraser, Line, Rectangle, Ellipse, Fill, Picker, Select };
inline constexpr int kToolCount = 9;

enum class ShapeMode {
    Outline, // stroke in the active color
    Filled,  // interior in the active color, no stroke
    Both,    // stroke in the active color, interior in the other one
};

struct ToolSettings {
    static constexpr int kMinSize = 1;
    static constexpr int kMaxSize = 200;
    static constexpr int kMaxStrokeWidth = 50; // line, rectangle and ellipse outline

    Rgba8 primary{0, 0, 0, 255};
    Rgba8 secondary{255, 255, 255, 255};

    int brushSize = 6;    // brush and eraser diameter in pixels
    int strokeWidth = 2;  // line, rectangle and ellipse outline width in pixels
    bool antialias = true;
    bool eraseToTransparent = false; // eraser paints transparency instead of the secondary color
    ShapeMode shapeMode = ShapeMode::Outline;
    int fillTolerance = 0; // 0..255
};

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
