#include <algorithm>
#include <cstdlib>
#include <vector>

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
        trail_.clear();
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
        if (id_ == ToolId::Pencil || s.squareBrush)
            return 0.0f; // a square stamp has its own outline, drawn by the canvas
        return static_cast<float>(clampedSize(s)) * 0.5f;
    }

private:
    // A pixel of a pixel-perfect pencil stroke, and what was under it before the stroke painted it.
    struct Cell {
        int x;
        int y;
        Rgba8 before;
    };

    static int clampedSize(const ToolSettings& s) noexcept {
        return std::clamp(s.brushSize, ToolSettings::kMinSize, ToolSettings::kMaxSize);
    }

    void finish() noexcept {
        active_ = false;
        trail_.clear();
        mask_.end();
    }

    // One pixel of a pixel-perfect stroke. When the new pixel `c` and the one two steps back `a` are diagonal
    // neighbours and the pixel between them `b` sits at the corner of the L they form, `b` is taken back
    // (restored to what it was before the stroke) so the line stays one pixel thick. Only the last few
    // pixels are kept: that is all the test needs, and a long stroke stays cheap.
    IRect addPerfectPixel(Image& img, const Paint& paint, int x, int y) {
        IRect touched;
        if (!trail_.empty() && trail_.back().x == x && trail_.back().y == y)
            return touched; // still on the same pixel
        if (trail_.size() >= 2) {
            const Cell& a = trail_[trail_.size() - 2];
            const Cell& b = trail_.back();
            const bool diagonal = std::abs(a.x - x) == 1 && std::abs(a.y - y) == 1;
            const bool corner = (b.x == a.x && b.y == y) || (b.x == x && b.y == a.y);
            if (diagonal && corner) {
                if (img.set(b.x, b.y, b.before))
                    touched = unionOf(touched, IRect{b.x, b.y, b.x + 1, b.y + 1});
                trail_.pop_back();
            }
        }
        Cell cell{x, y, img.get(x, y)};
        if (img.set(x, y, blendOver(cell.before, paint.color)))
            touched = unionOf(touched, IRect{x, y, x + 1, y + 1});
        trail_.push_back(cell);
        if (trail_.size() > kTrailKept)
            trail_.erase(trail_.begin());
        return touched;
    }

    void paint(Document& doc, const ToolSettings& s, const PointerEvent& ev, Vec2f to, bool isPress) {
        Image& img = doc.image();
        IRect touched;
        switch (id_) {
        case ToolId::Pencil:
            if (s.pixelPerfect) {
                const Paint stroke{activeColor(s, ev)};
                // The start pixel is already part of the stroke (the press, or the previous segment).
                bool first = true;
                forEachLinePixel(toPixel(last_.x), toPixel(last_.y), toPixel(to.x), toPixel(to.y), [&](int x, int y) {
                    if (isPress || !first)
                        touched = unionOf(touched, addPerfectPixel(img, stroke, x, y));
                    first = false;
                });
                break;
            }
            // The start pixel was already painted by the previous segment (or the press).
            touched = drawLine(img, toPixel(last_.x), toPixel(last_.y), toPixel(to.x), toPixel(to.y),
                               Paint{activeColor(s, ev)}, isPress);
            break;
        case ToolId::Brush:
        case ToolId::Eraser: {
            const Paint paint = id_ == ToolId::Eraser && s.eraseToTransparent
                                    ? Paint{Rgba8{}, BlendMode::EraseAlpha}
                                    : Paint{id_ == ToolId::Eraser ? s.secondary : activeColor(s, ev)};
            if (s.squareBrush)
                touched = stampSquareLine(img, toPixel(last_.x), toPixel(last_.y), toPixel(to.x), toPixel(to.y),
                                          clampedSize(s), paint, &mask_);
            else
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

    static constexpr std::size_t kTrailKept = 4;

    ToolId id_;
    bool active_ = false;
    Vec2f last_;
    StrokeMask mask_;
    std::vector<Cell> trail_; // pixel-perfect pencil only
};

} // namespace

std::unique_ptr<Tool> createFreehandTool(ToolId id) {
    return std::make_unique<FreehandTool>(id);
}

} // namespace trace
