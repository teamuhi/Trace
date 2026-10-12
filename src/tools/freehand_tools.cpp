#include <algorithm>

#include "core/raster.hpp"
#include "tools/tool_factories.hpp"
#include "tools/tool_util.hpp"

namespace trace {

namespace {

using namespace tools;

// Pencil, brush and eraser: paints a segment from the previous pointer position on every drag.
class FreehandTool final : public Tool {
public:
    explicit FreehandTool(ToolId id) : id_(id) {}

    void press(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        if (doc.image().empty())
            return;
        active_ = true;
        if (id_ != ToolId::Pencil)
            mask_.begin(doc.image().width(), doc.image().height());
        last_ = sanitize(ev.pos);
        paint(doc, s, ev, last_, true);
    }

    void drag(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        if (!active_)
            return;
        paint(doc, s, ev, sanitize(ev.pos), false);
    }

    void release(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        drag(doc, s, ev);
        finish();
    }

    // Pixels are committed as the stroke goes, so there is nothing to undo here (history arrives in M3).
    void cancel(Document&) override { finish(); }
    void abort() noexcept override { finish(); }

    float cursorRadius(const ToolSettings& s) const noexcept override {
        return id_ == ToolId::Pencil ? 0.0f : static_cast<float>(clampedSize(s)) * 0.5f;
    }

private:
    static int clampedSize(const ToolSettings& s) noexcept {
        return std::clamp(s.brushSize, ToolSettings::kMinSize, ToolSettings::kMaxSize);
    }

    void finish() noexcept {
        active_ = false;
        mask_.end();
    }

    void paint(Document& doc, const ToolSettings& s, const PointerEvent& ev, Vec2f to, bool isPress) {
        Image& img = doc.image();
        IRect touched;
        switch (id_) {
        case ToolId::Pencil:
            // The start pixel was already painted by the previous segment (or the press).
            touched = drawLine(img, toPixel(last_.x), toPixel(last_.y), toPixel(to.x), toPixel(to.y),
                               Paint{activeColor(s, ev)}, isPress);
            break;
        case ToolId::Brush:
        case ToolId::Eraser: {
            const Paint paint = id_ == ToolId::Eraser && s.eraseToTransparent
                                    ? Paint{Rgba8{}, BlendMode::EraseAlpha}
                                    : Paint{id_ == ToolId::Eraser ? s.secondary : activeColor(s, ev)};
            touched = drawCapsule(img, last_, to, static_cast<float>(clampedSize(s)) * 0.5f, paint,
                                  s.antialias, &mask_);
            break;
        }
        default:
            break;
        }
        doc.markDirty(touched);
        last_ = to;
    }

    ToolId id_;
    bool active_ = false;
    Vec2f last_;
    StrokeMask mask_;
};

} // namespace

std::unique_ptr<Tool> createFreehandTool(ToolId id) {
    return std::make_unique<FreehandTool>(id);
}

} // namespace trace
