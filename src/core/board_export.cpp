#include "core/board_export.hpp"

#include <algorithm>
#include <cmath>
#include <span>

#include "core/cel_grid.hpp"
#include "core/document.hpp"
#include "core/transform.hpp"

namespace trace {

namespace {

constexpr long long kMaxSourceSide = 1'000'000;
constexpr int kMaxExportSide = 16384; // the picture formats' limit (io/image_io.hpp kMaxImageDimension)

int scaled(long long value, long long numerator, long long denominator) {
    if (denominator < 1)
        return 1;
    const double v = static_cast<double>(value) * static_cast<double>(numerator) / static_cast<double>(denominator);
    return static_cast<int>(std::clamp(std::llround(v), 1LL, static_cast<long long>(BoardExportOptions::kMaxTarget) + 1));
}

} // namespace

BoardExportPlan planBoardExport(const IRect& region, const BoardExportOptions& o) {
    BoardExportPlan plan;
    if (region.empty())
        return plan; // Problem::Empty

    const auto clampPad = [](int v) { return std::clamp(v, 0, BoardExportOptions::kMaxPad); };
    const int left = clampPad(o.uniformPad ? o.padAll : o.padLeft);
    const int top = clampPad(o.uniformPad ? o.padAll : o.padTop);
    const int right = clampPad(o.uniformPad ? o.padAll : o.padRight);
    const int bottom = clampPad(o.uniformPad ? o.padAll : o.padBottom);

    plan.source = IRect{region.x0 - left, region.y0 - top, region.x1 + right, region.y1 + bottom};
    const long long srcW = plan.source.width();
    const long long srcH = plan.source.height();
    plan.problem = BoardExportPlan::Problem::TooLarge;
    if (srcW < 1 || srcH < 1 || srcW > kMaxSourceSide || srcH > kMaxSourceSide ||
        srcW * srcH > static_cast<long long>(kMaxDocumentPixels))
        return plan;

    int w = 1;
    int h = 1;
    const int target = std::clamp(o.target, 1, BoardExportOptions::kMaxTarget);
    switch (o.size) {
    case BoardSizeMode::Scale: {
        const int pct = std::clamp(o.scalePct, BoardExportOptions::kMinScalePct, BoardExportOptions::kMaxScalePct);
        w = scaled(srcW, pct, 100);
        h = scaled(srcH, pct, 100);
        break;
    }
    case BoardSizeMode::Width:
        w = target;
        h = scaled(srcH, target, srcW);
        break;
    case BoardSizeMode::Height:
        h = target;
        w = scaled(srcW, target, srcH);
        break;
    case BoardSizeMode::LongEdge:
        if (srcW >= srcH) {
            w = target;
            h = scaled(srcH, target, srcW);
        } else {
            h = target;
            w = scaled(srcW, target, srcH);
        }
        break;
    }
    if (w > kMaxExportSide || h > kMaxExportSide || static_cast<long long>(w) * h > static_cast<long long>(kMaxDocumentPixels))
        return plan;

    plan.width = w;
    plan.height = h;
    plan.problem = BoardExportPlan::Problem::None;
    return plan;
}

Image renderBoardExport(const Document& doc, const BoardExportPlan& plan, Rgba8 background, bool smooth) {
    if (!plan.ok())
        return {};
    const int srcW = plan.source.width();
    const int srcH = plan.source.height();
    Image out(srcW, srcH, background);

    const int frame = doc.activeFrame();
    const IRect imageRect{0, 0, doc.image().width(), doc.image().height()};
    const IRect area = intersectionOf(plan.source, imageRect);
    if (!area.empty()) {
        for (int layer = 0; layer < doc.layerCount(); ++layer) {
            const LayerInfo& info = doc.layer(layer);
            if (!info.visible || info.opacity == 0)
                continue;
            const Image& cel = doc.cel(layer, frame);
            if (cel.empty())
                continue;
            for (int y = area.y0; y < area.y1; ++y) {
                const std::span<const Rgba8> from = cel.row(y);
                const std::span<Rgba8> to = out.row(y - plan.source.y0);
                for (int x = area.x0; x < area.x1; ++x) {
                    Rgba8& dst = to[static_cast<std::size_t>(x - plan.source.x0)];
                    dst = blendOver(dst, from[static_cast<std::size_t>(x)], info.opacity);
                }
            }
        }
    }
    if (plan.width == srcW && plan.height == srcH)
        return out;
    return resizedImage(out, plan.width, plan.height, smooth ? ResizeFilter::Bilinear : ResizeFilter::Nearest);
}

} // namespace trace
