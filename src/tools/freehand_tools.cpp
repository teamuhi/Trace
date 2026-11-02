#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <vector>

#include "core/raster.hpp"
#include "tools/tool_factories.hpp"
#include "tools/tool_util.hpp"

namespace trace {

namespace {

using namespace tools;

// Pencil, brush and eraser: paints a segment from the previous pointer position on every drag. With symmetry on
// (mirror lines and rotation centers), every segment is also painted as its copies; all copies share the
// stroke's mask so overlaps (a stroke crossing a line, pixels on it, the center) composite once. Exact copies
// (mirrors at multiples of 45 degrees, quarter turns) map the original's pixels; the other angles (odd mirror
// angles, 3, 6 and 8 rotations) are drawn as a line between the mapped ends of each segment, which has no gaps
// however the pixels would have rounded.
class FreehandTool final : public Tool {
public:
    explicit FreehandTool(ToolId id) : id_(id) {}

    void press(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        if (doc.image().empty())
            return;
        active_ = true;
        // The symmetry plan is fixed for the whole stroke, so changing the list mid-drag cannot split it.
        plan_ = resolveSymmetry(s.symmetry, doc.image().width(), doc.image().height());
        trails_.assign(plan_.count(), {});
        held_.clear();
        // The brushes always composite through the mask; the pencil only needs it when copies can overlap.
        useMask_ = id_ != ToolId::Pencil || plan_.active();
        if (useMask_)
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
    // A pixel on one copy's pixel-perfect trail. `held` is the id of the Held entry it keeps alive (0: it owns
    // no paint, because it is off the image or an earlier step of the stroke already painted that pixel).
    struct Cell {
        int x;
        int y;
        unsigned held;
    };

    // A pixel the pixel-perfect pencil painted that a trail still references, and what was under it before the
    // stroke. With symmetry on, several cells (of different mirrored copies, or one copy crossing itself) can
    // share an entry: the pixel is painted once and given back only when the last of them lets go.
    struct Held {
        unsigned id;
        int x;
        int y;
        Rgba8 before;
        int refs;
        bool kept; // a cell that painted it left the trail for good: it stays, whoever else lets go
    };

    static int clampedSize(const ToolSettings& s) noexcept {
        return std::clamp(s.brushSize, ToolSettings::kMinSize, ToolSettings::kMaxSize);
    }

    static float hardnessOf(const ToolSettings& s) noexcept {
        return static_cast<float>(std::clamp(s.brushHardness, ToolSettings::kMinHardness, 100)) / 100.0f;
    }

    void finish() noexcept {
        active_ = false;
        trails_.clear();
        held_.clear();
        mask_.end();
        useMask_ = false;
    }

    bool maskUsable(const Image& img) const noexcept { return useMask_ && mask_.matches(img); }

    Held* findHeldAt(int x, int y) noexcept {
        for (Held& h : held_)
            if (h.x == x && h.y == y)
                return &h;
        return nullptr;
    }

    // Paints the pixel of a new trail cell, unless there is nothing to paint. Without symmetry every cell
    // paints its own pixel on top of whatever is there, as the single-trail pencil always did.
    IRect claim(Image& img, const Paint& paint, Cell& cell) {
        if (!img.contains(cell.x, cell.y))
            return {};
        const bool mirrored = plan_.active();
        if (mirrored) {
            if (Held* h = findHeldAt(cell.x, cell.y)) {
                ++h->refs; // another live cell painted it a moment ago: share it, never blend twice
                cell.held = h->id;
                return {};
            }
            if (maskUsable(img) && mask_.get(cell.x, cell.y) != 0)
                return {}; // painted by an earlier step of this stroke and no longer in any trail
        }
        const Held h{++nextHeld_, cell.x, cell.y, img.get(cell.x, cell.y), 1, false};
        held_.push_back(h);
        cell.held = h.id;
        img.set(cell.x, cell.y, blendOver(h.before, paint.color));
        const IRect pixel{cell.x, cell.y, cell.x + 1, cell.y + 1};
        if (mirrored && maskUsable(img)) {
            mask_.set(cell.x, cell.y, 255);
            mask_.markTouched(pixel);
        }
        return pixel;
    }

    // Lets go of a trail cell's pixel. A corner (`restore`) gives the pixel back to what was under it, but only
    // when no other cell still holds it and none that did has left its trail for good, so a copy never undoes a
    // pixel a different copy legitimately painted.
    IRect release(Image& img, unsigned id, bool restore) {
        if (id == 0)
            return {};
        const auto it = std::find_if(held_.begin(), held_.end(), [id](const Held& h) { return h.id == id; });
        if (it == held_.end())
            return {};
        IRect touched;
        if (!restore)
            it->kept = true; // pushed off the end of its trail: not a corner, so the pixel is part of the stroke
        if (--it->refs <= 0) {
            if (restore && !it->kept && img.set(it->x, it->y, it->before)) {
                touched = IRect{it->x, it->y, it->x + 1, it->y + 1};
                if (maskUsable(img))
                    mask_.set(it->x, it->y, 0); // a later copy may paint it again
            }
            held_.erase(it);
        }
        return touched;
    }

    // One pixel of a pixel-perfect stroke, on the trail of mirrored copy `k`. When the new pixel `c` and the
    // one two steps back `a` are diagonal neighbours and the pixel between them `b` sits at the corner of the
    // L they form, `b` is taken back (restored to what it was before the stroke) so the line stays one pixel
    // thick. Only the last few pixels are kept: that is all the test needs, and a long stroke stays cheap.
    // Exact copies are isometries of the pixel grid, so they find their corners at the same steps; a copy at
    // another angle runs the same test on its own mapped path, and whatever it removes
    // it gives back through the shared held-pixel bookkeeping.
    IRect addPerfectPixel(Image& img, const Paint& paint, std::size_t k, int x, int y) {
        std::vector<Cell>& trail = trails_[k];
        IRect touched;
        if (!trail.empty() && trail.back().x == x && trail.back().y == y)
            return touched; // still on the same pixel
        if (trail.size() >= 2) {
            const Cell a = trail[trail.size() - 2];
            const Cell b = trail.back();
            const bool diagonal = std::abs(a.x - x) == 1 && std::abs(a.y - y) == 1;
            const bool corner = (b.x == a.x && b.y == y) || (b.x == x && b.y == a.y);
            if (diagonal && corner) {
                touched = release(img, b.held, true);
                trail.pop_back();
            }
        }
        Cell cell{x, y, 0};
        touched = unionOf(touched, claim(img, paint, cell));
        trail.push_back(cell);
        if (trail.size() > kTrailKept) {
            release(img, trail.front().held, false);
            trail.erase(trail.begin());
        }
        return touched;
    }

    void paint(Document& doc, const ToolSettings& s, const PointerEvent& ev, Vec2f to, bool isPress) {
        Image& img = doc.image();
        IRect touched;
        const std::size_t copies = plan_.count();
        switch (id_) {
        case ToolId::Pencil: {
            const Paint stroke{activeColor(s, ev)};
            if (!s.pixelPerfect && !plan_.active()) {
                // The start pixel was already painted by the previous segment (or the press).
                touched = drawLine(img, toPixel(last_.x), toPixel(last_.y), toPixel(to.x), toPixel(to.y), stroke,
                                   isPress);
                break;
            }
            // Each copy is the original's pixels moved (see forEachCopyLinePixel), not a line drawn between
            // mirrored ends (whose rounding could differ), and the start pixel is already part of the stroke.
            for (std::size_t k = 0; k < copies; ++k) {
                bool first = true;
                forEachCopyLinePixel(plan_, k, toPixel(last_.x), toPixel(last_.y), toPixel(to.x), toPixel(to.y),
                                     [&](int x, int y) {
                                         if (isPress || !first) {
                                             touched = unionOf(touched,
                                                               s.pixelPerfect
                                                                   ? addPerfectPixel(img, stroke, k, x, y)
                                                                   : stampRect(img, IRect{x, y, x + 1, y + 1}, stroke, &mask_));
                                         }
                                         first = false;
                                     });
            }
            break;
        }
        case ToolId::Brush:
        case ToolId::Eraser: {
            const Paint paint = id_ == ToolId::Eraser && clearsToTransparency(s, doc)
                                    ? Paint{Rgba8{}, BlendMode::EraseAlpha}
                                    : Paint{id_ == ToolId::Eraser ? s.secondary : activeColor(s, ev)};
            const int size = clampedSize(s);
            if (s.squareBrush) {
                if (!plan_.active()) {
                    touched = stampSquareLine(img, toPixel(last_.x), toPixel(last_.y), toPixel(to.x), toPixel(to.y),
                                              size, paint, &mask_);
                    break;
                }
                // An exact copy moves each stamp's covered rectangle, not just its center pixel: an even size is
                // anchored off-center, so the copy must cover exactly the moved image of the original. Other
                // angles stamp the same axis-aligned square along a line between the ends mapped by the base
                // step, and move each stamp by the copy's exact part.
                const int lx = toPixel(last_.x);
                const int ly = toPixel(last_.y);
                const int tx = toPixel(to.x);
                const int ty = toPixel(to.y);
                for (std::size_t k = 0; k < copies; ++k) {
                    if (plan_.exact(k)) {
                        forEachLinePixel(lx, ly, tx, ty, [&](int x, int y) {
                            const IRect stamp = plan_.rect(k, squareStampRect(x, y, size));
                            touched = unionOf(touched, stampRect(img, stamp, paint, &mask_));
                        });
                        continue;
                    }
                    const PixelPos a = plan_.basePixel(k, lx, ly);
                    const PixelPos b = plan_.basePixel(k, tx, ty);
                    forEachLinePixel(a.x, a.y, b.x, b.y, [&](int x, int y) {
                        const IRect stamp = plan_.extraRect(k, squareStampRect(x, y, size));
                        touched = unionOf(touched, stampRect(img, stamp, paint, &mask_));
                    });
                }
                break;
            }
            // Without antialiasing the capsule snaps its ends to the center of the copy's pixel (see
            // copyCapsuleEnd), so a coordinate exactly on a pixel edge still copies like its pixel.
            for (std::size_t k = 0; k < copies; ++k)
                touched = unionOf(touched, drawCapsule(img, copyCapsuleEnd(plan_, k, last_, s.antialias),
                                                       copyCapsuleEnd(plan_, k, to, s.antialias),
                                                       static_cast<float>(size) * 0.5f, paint, s.antialias, &mask_,
                                                       hardnessOf(s)));
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
    SymmetryPlan plan_;
    bool useMask_ = false;
    StrokeMask mask_;
    std::vector<std::vector<Cell>> trails_; // pixel-perfect pencil only: one trail per copy
    std::vector<Held> held_;                // pixel-perfect pencil only
    unsigned nextHeld_ = 0;
};

} // namespace

std::unique_ptr<Tool> createFreehandTool(ToolId id) {
    return std::make_unique<FreehandTool>(id);
}

} // namespace trace
