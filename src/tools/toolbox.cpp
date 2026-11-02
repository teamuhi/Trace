#include "tools/toolbox.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include "core/raster.hpp"
#include "tools/tool_factories.hpp"
#include "tools/tool_util.hpp"

namespace trace {

std::unique_ptr<Tool> createTool(ToolId id) {
    switch (id) {
    case ToolId::Pencil:
    case ToolId::Brush:
    case ToolId::Eraser:
        return createFreehandTool(id);
    case ToolId::Line:
    case ToolId::Rectangle:
    case ToolId::Ellipse:
        return createShapeTool(id);
    case ToolId::Fill:
    case ToolId::Picker:
        return createClickTool(id);
    case ToolId::Select:
        return createSelectTool();
    case ToolId::Text:
        return createTextTool();
    }
    return createFreehandTool(ToolId::Pencil);
}

Toolbox::Toolbox() {
    for (int i = 0; i < kToolCount; ++i)
        tools_[static_cast<std::size_t>(i)] = createTool(static_cast<ToolId>(i));
}

bool Toolbox::select(ToolId id) noexcept {
    if (active_)
        return false;
    current_ = id;
    return true;
}

bool Toolbox::stillValid(const Document& doc) {
    if (doc.generation() == gestureGeneration_)
        return true;
    tool().abort();
    active_ = false;
    return false;
}

bool Toolbox::wouldEdit(const Document& doc, const PointerEvent& ev) const {
    switch (current_) {
    case ToolId::Picker:
        return false;
    case ToolId::Select: {
        // Only moving selected pixels edits; a marquee does not.
        const Vec2f p = tools::sanitize(ev.pos);
        return doc.selection().active() && doc.selection().contains(tools::toPixel(p.x), tools::toPixel(p.y));
    }
    default:
        return true;
    }
}

void Toolbox::press(Document& doc, const PointerEvent& ev) {
    if (active_)
        return;
    refusal_ = Refusal::None;
    if (wouldEdit(doc, ev)) {
        const LayerInfo& layer = doc.layer(doc.activeLayer());
        if (editingBlocked_)
            refusal_ = Refusal::Playing;
        else if (!layer.visible)
            refusal_ = Refusal::LayerHidden;
        else if (layer.locked)
            refusal_ = Refusal::LayerLocked;
        else if (current_ == ToolId::Fill && doc.endless()) {
            // On an endless board a fill that reaches the edge of what is allocated would flood the whole empty board.
            const Vec2f p = tools::sanitize(ev.pos);
            const int x = tools::toPixel(p.x);
            const int y = tools::toPixel(p.y);
            const Image& img = doc.image();
            if (settings_.fillGlobal ? img.get(x, y).a == 0 : floodTouchesEdge(img, x, y, settings_.fillTolerance))
                refusal_ = Refusal::FillUnbounded;
        }
        if (refusal_ != Refusal::None) {
            ++refusalCount_;
            return; // nothing has been touched, not even a floating selection
        }
    }
    active_ = true;
    gestureGeneration_ = doc.generation();
    if (current_ != ToolId::Select)
        doc.clearSelection(); // drawing must not run while pixels are floating above the image
    tool().press(doc, settings_, ev);
}

void Toolbox::drag(Document& doc, const PointerEvent& ev) {
    if (active_ && stillValid(doc))
        tool().drag(doc, settings_, ev);
}

void Toolbox::release(Document& doc, const PointerEvent& ev) {
    if (!active_)
        return;
    if (stillValid(doc)) {
        tool().release(doc, settings_, ev);
        active_ = false;
        doc.commit(); // one gesture, one undo step
    }
}

bool Toolbox::commitText(Document& doc) {
    TextTool& t = text();
    if (!t.editing())
        return false;
    const LayerInfo& layer = doc.layer(doc.activeLayer());
    if (t.stale(doc) || !layer.visible || layer.locked) {
        t.discard();
        return false;
    }
    const bool changed = !t.commit(doc, settings_).empty();
    doc.commit(); // the text is one undo step
    return changed;
}

void Toolbox::cancel(Document& doc) {
    if (!active_)
        return;
    if (stillValid(doc)) {
        tool().cancel(doc);
        active_ = false;
        doc.commit(); // a cancelled shape left no change and records nothing; a cut-short stroke stays undoable
    }
}

float Toolbox::cursorRadius() const noexcept {
    return tool().cursorRadius(settings_);
}

int Toolbox::cursorSquareSize() const noexcept {
    if (!settings_.squareBrush || (current_ != ToolId::Brush && current_ != ToolId::Eraser))
        return 0;
    return std::clamp(settings_.brushSize, ToolSettings::kMinSize, ToolSettings::kMaxSize);
}

const char* Toolbox::name(ToolId id) noexcept {
    switch (id) {
    case ToolId::Pencil:
        return "Pencil";
    case ToolId::Brush:
        return "Brush";
    case ToolId::Eraser:
        return "Eraser";
    case ToolId::Line:
        return "Line";
    case ToolId::Rectangle:
        return "Rectangle";
    case ToolId::Ellipse:
        return "Ellipse";
    case ToolId::Fill:
        return "Fill";
    case ToolId::Picker:
        return "Picker";
    case ToolId::Select:
        return "Select";
    case ToolId::Text:
        return "Text";
    }
    return "";
}

} // namespace trace
