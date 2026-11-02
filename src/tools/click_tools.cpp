#include <algorithm>
#include <cstddef>
#include <vector>

#include "core/raster.hpp"
#include "tools/tool_factories.hpp"
#include "tools/tool_util.hpp"

namespace trace {

namespace {

using namespace tools;

// Flood fill: one click fills the region under the pointer. With symmetry on, each copy of the seed (mirrored
// and turned) on the image is flooded too (a global fill is a single pass from the first seed that lands on the image).
class FillTool final : public Tool {
public:
    void press(Document& doc, ToolSettings& s, const PointerEvent& ev) override {
        const Vec2f p = sanitize(ev.pos);
        const int x = toPixel(p.x);
        const int y = toPixel(p.y);
        Image& img = doc.image();
        const Rgba8 color = activeColor(s, ev);
        const SymmetryPlan plan = resolveSymmetry(s.symmetry, img.width(), img.height());
        if (!plan.active()) {
            doc.markDirty(floodFill(img, x, y, color, s.fillTolerance, !s.fillGlobal));
            return;
        }

        // Seeds on the image, each with the color it had before any copy filled.
        struct Seed {
            int x;
            int y;
            Rgba8 original;
        };
        std::vector<Seed> seeds;
        for (std::size_t k = 0; k < plan.count(); ++k) {
            const PixelPos at = plan.pixel(k, x, y);
            if (!img.contains(at.x, at.y))
                continue;
            const bool duplicate = std::any_of(seeds.begin(), seeds.end(), [&](const Seed& e) { return e.x == at.x && e.y == at.y; });
            if (!duplicate)
                seeds.push_back(Seed{at.x, at.y, img.get(at.x, at.y)});
        }

        IRect touched;
        for (const Seed& seed : seeds) {
            // A seed an earlier copy's fill already recolored lies in a region that is done; flooding from it
            // again would compare against the new color and could spread a tolerance fill into neighbours.
            if (img.get(seed.x, seed.y) != seed.original)
                continue;
            touched = unionOf(touched, floodFill(img, seed.x, seed.y, color, s.fillTolerance, !s.fillGlobal));
            if (s.fillGlobal)
                break; // every matching pixel is already done
        }
        doc.markDirty(touched);
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
