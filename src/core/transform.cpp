#include "core/transform.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace trace {

namespace {

bool validSide(int side) noexcept {
    return side >= 1 && side <= kMaxImageSide;
}

// The source pixels one destination pixel along an axis is made of, and their weights (summing to 1).
struct Taps {
    int first = 0;
    std::vector<float> weights;
};

// Triangle filter whose half-width grows with the shrink factor, so downscaling averages the pixels it
// covers instead of skipping most of them.
std::vector<Taps> axisTaps(int srcSize, int dstSize) {
    std::vector<Taps> result(static_cast<std::size_t>(dstSize));
    const double scale = static_cast<double>(srcSize) / static_cast<double>(dstSize);
    const double support = std::max(1.0, scale);
    for (int i = 0; i < dstSize; ++i) {
        const double center = (static_cast<double>(i) + 0.5) * scale;
        const int first = std::max(0, static_cast<int>(std::ceil(center - support - 0.5)));
        const int last = std::min(srcSize - 1, static_cast<int>(std::floor(center + support - 0.5)));
        Taps& taps = result[static_cast<std::size_t>(i)];
        taps.first = std::min(first, srcSize - 1);
        double sum = 0.0;
        for (int j = first; j <= last; ++j) {
            const double w = std::max(0.0, 1.0 - std::abs(static_cast<double>(j) + 0.5 - center) / support);
            taps.weights.push_back(static_cast<float>(w));
            sum += w;
        }
        if (sum <= 0.0) { // cannot happen for a sane filter; fall back to the closest pixel
            taps.first = std::clamp(static_cast<int>(center), 0, srcSize - 1);
            taps.weights.assign(1, 1.0f);
            continue;
        }
        for (float& w : taps.weights)
            w = static_cast<float>(w / sum);
    }
    return result;
}

std::uint8_t toByte(float v) noexcept {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 255));
}

Image resizeNearest(const Image& src, int width, int height) {
    Image out(width, height);
    std::vector<int> columns(static_cast<std::size_t>(width));
    for (int x = 0; x < width; ++x) {
        const std::int64_t sx = (static_cast<std::int64_t>(x) * 2 + 1) * src.width() / (static_cast<std::int64_t>(width) * 2);
        columns[static_cast<std::size_t>(x)] = static_cast<int>(std::min<std::int64_t>(sx, src.width() - 1));
    }
    for (int y = 0; y < height; ++y) {
        const std::int64_t sy = (static_cast<std::int64_t>(y) * 2 + 1) * src.height() / (static_cast<std::int64_t>(height) * 2);
        const std::span<const Rgba8> from = src.row(static_cast<int>(std::min<std::int64_t>(sy, src.height() - 1)));
        const std::span<Rgba8> to = out.row(y);
        for (int x = 0; x < width; ++x)
            to[static_cast<std::size_t>(x)] = from[static_cast<std::size_t>(columns[static_cast<std::size_t>(x)])];
    }
    return out;
}

Image resizeBilinear(const Image& src, int width, int height) {
    Image out(width, height);
    const std::vector<Taps> xs = axisTaps(src.width(), width);
    const std::vector<Taps> ys = axisTaps(src.height(), height);
    for (int y = 0; y < height; ++y) {
        const Taps& ty = ys[static_cast<std::size_t>(y)];
        const std::span<Rgba8> to = out.row(y);
        for (int x = 0; x < width; ++x) {
            const Taps& tx = xs[static_cast<std::size_t>(x)];
            // Colors are averaged weighted by alpha, so transparent pixels do not darken or tint their neighbours.
            float a = 0.0f;
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            for (std::size_t j = 0; j < ty.weights.size(); ++j) {
                const std::span<const Rgba8> row = src.row(ty.first + static_cast<int>(j));
                for (std::size_t i = 0; i < tx.weights.size(); ++i) {
                    const Rgba8 p = row[static_cast<std::size_t>(tx.first) + i];
                    const float w = ty.weights[j] * tx.weights[i] * static_cast<float>(p.a);
                    a += w;
                    r += w * static_cast<float>(p.r);
                    g += w * static_cast<float>(p.g);
                    b += w * static_cast<float>(p.b);
                }
            }
            to[static_cast<std::size_t>(x)] = a > 0.0f ? Rgba8{toByte(r / a), toByte(g / a), toByte(b / a), toByte(a)} : Rgba8{};
        }
    }
    return out;
}

int floorHalf(int d) noexcept {
    return (d - (d < 0 ? 1 : 0)) / 2;
}

} // namespace

Image resizedImage(const Image& src, int width, int height, ResizeFilter filter) {
    if (src.empty() || !validSide(width) || !validSide(height))
        return {};
    if (width == src.width() && height == src.height())
        return src;
    return filter == ResizeFilter::Nearest ? resizeNearest(src, width, height) : resizeBilinear(src, width, height);
}

std::pair<int, int> anchorOffset(Anchor anchor, int oldWidth, int oldHeight, int newWidth, int newHeight) noexcept {
    const int column = static_cast<int>(anchor) % 3; // 0 left, 1 center, 2 right
    const int row = static_cast<int>(anchor) / 3;
    const int dx = newWidth - oldWidth;
    const int dy = newHeight - oldHeight;
    return {column == 0 ? 0 : column == 1 ? floorHalf(dx) : dx, row == 0 ? 0 : row == 1 ? floorHalf(dy) : dy};
}

Image canvasResized(const Image& src, int width, int height, Anchor anchor, Rgba8 fill) {
    if (!validSide(width) || !validSide(height))
        return {};
    Image out(width, height, fill);
    if (src.empty())
        return out;

    const auto [ox, oy] = anchorOffset(anchor, src.width(), src.height(), width, height);
    const int x0 = std::max(0, ox); // overlap in destination coordinates
    const int x1 = std::min(width, ox + src.width());
    const int y0 = std::max(0, oy);
    const int y1 = std::min(height, oy + src.height());
    for (int y = y0; y < y1; ++y) {
        const std::span<const Rgba8> from = src.row(y - oy);
        const std::span<Rgba8> to = out.row(y);
        std::copy(from.begin() + (x0 - ox), from.begin() + (x1 - ox), to.begin() + x0);
    }
    return out;
}

Image cropped(const Image& src, const IRect& rect) {
    const IRect r = intersectionOf(rect, IRect{0, 0, src.width(), src.height()});
    if (r.empty())
        return {};
    Image out(r.width(), r.height());
    for (int y = r.y0; y < r.y1; ++y) {
        const std::span<const Rgba8> from = src.row(y);
        std::copy(from.begin() + r.x0, from.begin() + r.x1, out.row(y - r.y0).begin());
    }
    return out;
}

int scaledSide(int side, double factor) noexcept {
    if (!std::isfinite(factor) || factor <= 0.0)
        return std::clamp(side, 1, kMaxImageSide);
    const double v = std::round(static_cast<double>(side) * factor);
    return static_cast<int>(std::clamp(v, 1.0, static_cast<double>(kMaxImageSide)));
}

Image thumbnailOf(const Image& src, int maxSide) {
    if (src.empty())
        return {};
    maxSide = std::max(maxSide, 1);
    if (src.width() <= maxSide && src.height() <= maxSide)
        return src;

    const double scale = static_cast<double>(maxSide) / static_cast<double>(std::max(src.width(), src.height()));
    const int width = std::max(1, static_cast<int>(std::lround(src.width() * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(src.height() * scale)));
    Image out(width, height);

    constexpr int kSamples = 3; // per axis and output pixel
    for (int y = 0; y < height; ++y) {
        const std::int64_t y0 = static_cast<std::int64_t>(y) * src.height() / height;
        const std::int64_t y1 = std::max(y0 + 1, static_cast<std::int64_t>(y + 1) * src.height() / height);
        for (int x = 0; x < width; ++x) {
            const std::int64_t x0 = static_cast<std::int64_t>(x) * src.width() / width;
            const std::int64_t x1 = std::max(x0 + 1, static_cast<std::int64_t>(x + 1) * src.width() / width);
            std::uint64_t r = 0;
            std::uint64_t g = 0;
            std::uint64_t b = 0;
            std::uint64_t a = 0;
            int taken = 0;
            for (int sy = 0; sy < kSamples; ++sy) {
                const int py = static_cast<int>(y0 + (y1 - y0) * (2 * sy + 1) / (2 * kSamples));
                for (int sx = 0; sx < kSamples; ++sx) {
                    const int px = static_cast<int>(x0 + (x1 - x0) * (2 * sx + 1) / (2 * kSamples));
                    const Rgba8 p = src.get(px, py);
                    r += static_cast<std::uint64_t>(p.r) * p.a;
                    g += static_cast<std::uint64_t>(p.g) * p.a;
                    b += static_cast<std::uint64_t>(p.b) * p.a;
                    a += p.a;
                    ++taken;
                }
            }
            if (a == 0)
                continue; // all samples transparent
            out.set(x, y,
                    Rgba8{static_cast<std::uint8_t>((r + a / 2) / a), static_cast<std::uint8_t>((g + a / 2) / a),
                          static_cast<std::uint8_t>((b + a / 2) / a), static_cast<std::uint8_t>((a + taken / 2) / taken)});
        }
    }
    return out;
}

} // namespace trace
