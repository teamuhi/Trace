#include "tools/tool_factories.hpp"
#include "tools/tool_util.hpp"

namespace trace {

namespace {

using namespace tools;

// Rectangle select and move. Drag on empty canvas to draw a selection; drag inside an existing selection to
// move its pixels (they are lifted off the image on the first move). Shift makes the marquee square, a plain
// click deselects, and Escape undoes the current drag.
class SelectTool final : public Tool {
public:
    void press(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        if (doc.image().empty())
            return;
        const Vec2f p = sanitize(ev.pos);
        const int px = toPixel(p.x);
        const int py = toPixel(p.y);

        if (doc.selection().active() && doc.selection().contains(px, py)) {
            // Lifting leaves the secondary color (or transparency) behind, like the eraser.
            if (!doc.liftSelection(s.eraseToTransparent ? Rgba8{} : s.secondary))
                return;
            mode_ = Mode::Move;
            grab_ = {px, py};
            origin_ = {doc.selection().rect().x0, doc.selection().rect().y0};
        } else {
            previous_ = doc.selection().rect();
            doc.clearSelection();
            mode_ = Mode::Marquee;
            anchor_ = snapStartPoint(doc, p, ev.snapRadius);
        }
    }

    void drag(Document& doc, ToolSettings&, const PointerEvent& ev) override {
        const Vec2f p = sanitize(ev.pos);
        switch (mode_) {
        case Mode::Move: {
            // A moved selection lines up whichever of its edges is nearest to a guide.
            const IRect r = doc.selection().rect();
            int x = origin_.x + toPixel(p.x) - grab_.x;
            int y = origin_.y + toPixel(p.y) - grab_.y;
            if (ev.snapRadius > 0.0f) {
                x = doc.guides().snapSpan(GuideAxis::Vertical, x, r.width(), ev.snapRadius);
                y = doc.guides().snapSpan(GuideAxis::Horizontal, y, r.height(), ev.snapRadius);
            }
            doc.moveSelectionTo(x, y);
            break;
        }
        case Mode::Marquee:
            doc.selectRect(cornerBox(anchor_, snapEndPoint(doc, p, anchor_, ev.snapRadius), ev.shift));
            break;
        case Mode::None:
            break;
        }
    }

    void release(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        drag(doc, s, ev);
        if (mode_ == Mode::Marquee) {
            const Vec2f p = snapEndPoint(doc, sanitize(ev.pos), anchor_, ev.snapRadius);
            if (toPixel(p.x) == toPixel(anchor_.x) && toPixel(p.y) == toPixel(anchor_.y))
                doc.clearSelection(); // a click, not a drag
        }
        mode_ = Mode::None;
    }

    void cancel(Document& doc) override {
        if (mode_ == Mode::Marquee)
            doc.selectRect(previous_);
        else if (mode_ == Mode::Move)
            doc.moveSelectionTo(origin_.x, origin_.y);
        mode_ = Mode::None;
    }

    void abort() noexcept override { mode_ = Mode::None; }

private:
    enum class Mode { None, Marquee, Move };
    struct Point {
        int x = 0;
        int y = 0;
    };

    Mode mode_ = Mode::None;
    Vec2f anchor_;
    IRect previous_;
    Point grab_;   // pixel under the pointer when the move began
    Point origin_; // selection's top-left corner at that moment
};

} // namespace

std::unique_ptr<Tool> createSelectTool() {
    return std::make_unique<SelectTool>();
}

} // namespace trace
