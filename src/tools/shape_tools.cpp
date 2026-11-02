#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "core/raster.hpp"
#include "tools/tool_factories.hpp"
#include "tools/tool_util.hpp"

namespace trace {

namespace {

using namespace tools;

constexpr float kTan22_5 = 0.41421356f; // boundary between "horizontal" and "diagonal" when snapping

// Snaps the direction a->b to the nearest multiple of 45 degrees, keeping the larger extent.
Vec2f constrainAngle(Vec2f a, Vec2f b) noexcept {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    const float ax = std::fabs(dx);
    const float ay = std::fabs(dy);
    if (ay < kTan22_5 * ax) {
        dy = 0.0f;
    } else if (ax < kTan22_5 * ay) {
        dx = 0.0f;
    } else {
        const float m = std::max(ax, ay);
        dx = std::copysign(m, dx);
        dy = std::copysign(m, dy);
    }
    return Vec2f{a.x + dx, a.y + dy};
}

// Line, rectangle and ellipse. The shape is redrawn on every drag over a snapshot of the untouched image,
// so the preview is the real result and a cancel restores the original pixels exactly.
class ShapeTool final : public Tool {
public:
    explicit ShapeTool(ToolId id) : id_(id) {}

    void press(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        if (doc.image().empty())
            return;
        active_ = true;
        // Fixed for the whole gesture, so changing the list mid-drag cannot change a preview already drawn.
        plan_ = resolveSymmetry(s.symmetry, doc.image().width(), doc.image().height());
        start_ = snapStartPoint(doc, sanitize(ev.pos), ev.snapRadius);
    }

    void drag(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        if (active_)
            update(doc, s, ev);
    }

    void release(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        if (!active_)
            return;
        update(doc, s, ev);
        reset();
    }

    void cancel(Document& doc) override {
        if (active_ && hasSnapshot_) {
            restore(doc.image(), lastDirty_);
            doc.markDirty(lastDirty_);
        }
        reset();
    }

    void abort() noexcept override { reset(); }

private:
    void reset() noexcept {
        active_ = false;
        hasSnapshot_ = false;
        lastDirty_ = IRect{};
        snapshot_.clear(); // keeps the capacity, so repeated gestures do not reallocate
    }

    // The snapshot is taken on the first drag: until then the gesture has not touched the image.
    bool ensureSnapshot(const Image& img) {
        if (!hasSnapshot_) {
            snapshot_.assign(img.pixels().begin(), img.pixels().end());
            width_ = img.width();
            height_ = img.height();
            hasSnapshot_ = true;
        }
        return width_ == img.width() && height_ == img.height();
    }

    void restore(Image& img, const IRect& area) {
        const IRect r = intersectionOf(area, IRect{0, 0, img.width(), img.height()});
        for (int y = r.y0; y < r.y1; ++y) {
            const auto from = snapshot_.begin() + static_cast<std::ptrdiff_t>(y) * width_ + r.x0;
            std::copy_n(from, r.width(), img.row(y).begin() + r.x0);
        }
    }

    void update(Document& doc, const ToolSettings& s, const PointerEvent& ev) {
        Image& img = doc.image();
        if (!ensureSnapshot(img)) {
            reset();
            return;
        }
        const IRect previous = lastDirty_;
        restore(img, previous);
        const Vec2f end = snapEndPoint(doc, sanitize(ev.pos), start_, ev.snapRadius);
        lastDirty_ = render(img, s, ev, end);
        doc.markDirty(unionOf(previous, lastDirty_));
    }

    // Draws the shape and, with symmetry on, its copies. Copies are independent drawings over the same
    // snapshot: where two copies overlap (a shape crossing the axis or the center) a translucent color blends
    // once per copy, while an opaque one is exact. In Both mode every copy's interior goes down before any
    // outline, so an outline is never hidden by another copy's fill. Rectangles and ellipses stay axis-aligned
    // pixel boxes: a quarter turn swaps width and height exactly, any other angle keeps the size and only moves
    // the box's center around the rotation center (a rotated rectangle is not a pixel box); a reflection across
    // a diagonal swaps them too. Lines are true lines, so every angle maps them.
    IRect render(Image& img, const ToolSettings& s, const PointerEvent& ev, Vec2f end) const {
        const int width = std::clamp(s.strokeWidth, ToolSettings::kMinSize, ToolSettings::kMaxSize);
        const Paint stroke{activeColor(s, ev)};
        const Paint other{otherColor(s, ev)};
        const std::size_t copies = plan_.count();
        IRect touched;

        if (id_ == ToolId::Line) {
            const Vec2f a = pixelCenter(start_);
            Vec2f b = pixelCenter(end);
            if (ev.shift)
                b = constrainAngle(a, b);
            if (width == 1 && !s.antialias) {
                if (!plan_.active())
                    return drawLine(img, toPixel(a.x), toPixel(a.y), toPixel(b.x), toPixel(b.y), stroke);
                // Exact copies move the original's pixels (a line drawn between the moved ends can round
                // differently); other angles draw a line between the mapped ends.
                for (std::size_t k = 0; k < copies; ++k)
                    forEachCopyLinePixel(plan_, k, toPixel(a.x), toPixel(a.y), toPixel(b.x), toPixel(b.y),
                                         [&](int x, int y) {
                                             touched = unionOf(touched, fillRect(img, IRect{x, y, x + 1, y + 1}, stroke));
                                         });
                return touched;
            }
            for (std::size_t k = 0; k < copies; ++k)
                touched = unionOf(touched, drawCapsule(img, copyCapsuleEnd(plan_, k, a, s.antialias),
                                                       copyCapsuleEnd(plan_, k, b, s.antialias),
                                                       static_cast<float>(width) * 0.5f, stroke, s.antialias));
            return touched;
        }

        const IRect box = cornerBox(start_, end, ev.shift);
        const bool both = s.shapeMode == ShapeMode::Both;
        const bool rect = id_ == ToolId::Rectangle;

        // Interiors first (Both mode only), then the fill / outline of every copy.
        if (both) {
            for (std::size_t k = 0; k < copies; ++k) {
                const IRect b = plan_.rect(k, box);
                if (rect) {
                    // The interior stops where the outline starts, so translucent colors never overlap.
                    touched = unionOf(touched, fillRect(img, IRect{b.x0 + width, b.y0 + width, b.x1 - width, b.y1 - width}, other));
                } else {
                    touched = unionOf(touched, fillEllipse(img, b, other, width));
                }
            }
        }
        for (std::size_t k = 0; k < copies; ++k) {
            const IRect b = plan_.rect(k, box);
            if (rect)
                touched = unionOf(touched, s.shapeMode == ShapeMode::Filled ? fillRect(img, b, stroke)
                                                                            : strokeRect(img, b, width, stroke));
            else
                touched = unionOf(touched, s.shapeMode == ShapeMode::Filled ? fillEllipse(img, b, stroke)
                                                                            : strokeEllipse(img, b, width, stroke));
        }
        return touched;
    }

    ToolId id_;
    bool active_ = false;
    Vec2f start_;
    SymmetryPlan plan_;

    std::vector<Rgba8> snapshot_;
    bool hasSnapshot_ = false;
    int width_ = 0;
    int height_ = 0;
    IRect lastDirty_;
};

} // namespace

std::unique_ptr<Tool> createShapeTool(ToolId id) {
    return std::make_unique<ShapeTool>(id);
}

} // namespace trace
