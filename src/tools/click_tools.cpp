#include <algorithm>

#include "core/raster.hpp"
#include "tools/tool_factories.hpp"
#include "tools/tool_util.hpp"

namespace trace {

namespace {

using namespace tools;

// Flood fill: one click fills the region under the pointer.
class FillTool final : public Tool {
public:
    void press(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        const Vec2f p = sanitize(ev.pos);
        doc.markDirty(floodFill(doc.image(), toPixel(p.x), toPixel(p.y), activeColor(s, ev), s.fillTolerance,
                                !s.fillGlobal));
    }
    void drag(Document&, ToolSettings&, const PointerEvent&) override {}
    void release(Document&, ToolSettings&, const PointerEvent&) override {}
    void cancel(Document&) override {}
    void abort() noexcept override {}
};

// Color picker: copies the pixel under the pointer into the primary (or, with the right button, the
// secondary) color. Dragging keeps sampling, so the swatch follows the pointer.
class PickerTool final : public Tool {
public:
    void press(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        active_ = true;
        pick(doc, s, ev);
    }
    void drag(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        if (active_)
            pick(doc, s, ev);
    }
    void release(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        drag(doc, s, ev);
        active_ = false;
    }
    void cancel(Document&) override { active_ = false; }
    void abort() noexcept override { active_ = false; }

private:
    static void pick(const Document& doc, ToolSettings& s, const PointerEvent& ev) {
        const Vec2f p = sanitize(ev.pos);
        const int x = toPixel(p.x);
        const int y = toPixel(p.y);
        if (!doc.image().contains(x, y))
            return; // keep the current color when the pointer is off the image
        (ev.secondary ? s.secondary : s.primary) = doc.image().get(x, y);
    }

    bool active_ = false;
};

} // namespace

std::unique_ptr<Tool> createClickTool(ToolId id) {
    if (id == ToolId::Fill)
        return std::make_unique<FillTool>();
    return std::make_unique<PickerTool>();
}

} // namespace trace
