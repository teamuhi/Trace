#include "core/raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

namespace trace {

namespace {

IRect imageBounds(const Image& img) noexcept {
    return IRect{0, 0, img.width(), img.height()};
}

void applyPaint(Rgba8& pixel, const Paint& paint, unsigned coverage) noexcept {
    pixel = paint.mode == BlendMode::EraseAlpha ? eraseAlpha(pixel, coverage)
                                                : blendOver(pixel, paint.color, coverage);
}

// Paints the horizontal run [xa, xb] on row y at full coverage. The run is clipped to the image.
void paintSpan(Image& img, int y, int xa, int xb, const Paint& paint) noexcept {
    xa = std::max(xa, 0);
    xb = std::min(xb, img.width() - 1);
    if (xa > xb)
        return;
    const std::span<Rgba8> row = img.row(y);
    if (row.empty())
        return;
    for (int x = xa; x <= xb; ++x)
        applyPaint(row[static_cast<std::size_t>(x)], paint, 255);
}

// Horizontal extent [xl, xr] of the ellipse inscribed in `box` (shrunk by `inset`) on pixel row y, using
// pixel centers. False when the row misses the ellipse.
bool ellipseSpan(const IRect& box, int inset, int y, int& xl, int& xr) noexcept {
    const double a = static_cast<double>(box.width()) * 0.5 - inset;
    const double b = static_cast<double>(box.height()) * 0.5 - inset;
    if (a <= 0.0 || b <= 0.0)
        return false;

    const double cx = static_cast<double>(box.x0) + static_cast<double>(box.width()) * 0.5;
    const double cy = static_cast<double>(box.y0) + static_cast<double>(box.height()) * 0.5;
    const double t = (static_cast<double>(y) + 0.5 - cy) / b;
    const double q = 1.0 - t * t;
    if (q < 0.0)
        return false;

    const double dx = a * std::sqrt(q);
    xl = static_cast<int>(std::ceil(cx - dx - 0.5));
    xr = static_cast<int>(std::floor(cx + dx - 0.5));
    return xr >= xl;
}

bool finite(Vec2f p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

} // namespace

IRect drawLine(Image& img, int x0, int y0, int x1, int y1, const Paint& paint, bool includeStart) {
    if (img.empty())
        return {};

    IRect touched;
    bool first = true;
    forEachLinePixel(x0, y0, x1, y1, [&](int x, int y) {
        if ((includeStart || !first) && img.contains(x, y)) {
            applyPaint(img.row(y)[static_cast<std::size_t>(x)], paint, 255);
            touched = unionOf(touched, IRect{x, y, x + 1, y + 1});
        }
        first = false;
    });
    return touched;
}

IRect stampSquareLine(Image& img, int x0, int y0, int x1, int y1, int size, const Paint& paint, StrokeMask* mask) {
    if (img.empty() || size < 1)
        return {};
    if (mask && !mask->matches(img))
        mask = nullptr;

    const int before = size / 2; // pixels of the square to the left of / above the center pixel
    const IRect bounds = imageBounds(img);
    IRect touched;
    forEachLinePixel(x0, y0, x1, y1, [&](int x, int y) {
        // 64-bit so a stamp at an extreme coordinate cannot overflow while it is being rejected.
        const std::int64_t left = std::int64_t{x} - before;
        const std::int64_t top = std::int64_t{y} - before;
        if (left >= bounds.x1 || top >= bounds.y1 || left + size <= bounds.x0 || top + size <= bounds.y0)
            return; // entirely off the image
        const IRect box = intersectionOf(
            IRect{static_cast<int>(left), static_cast<int>(top), static_cast<int>(left) + size, static_cast<int>(top) + size},
            bounds);
        for (int py = box.y0; py < box.y1; ++py) {
            const std::span<Rgba8> row = img.row(py);
            for (int px = box.x0; px < box.x1; ++px) {
                if (mask) {
                    if (mask->get(px, py) != 0)
                        continue; // this stroke already painted it
                    mask->set(px, py, 255);
                }
                applyPaint(row[static_cast<std::size_t>(px)], paint, 255);
            }
        }
        touched = unionOf(touched, box);
    });
    if (mask)
        mask->markTouched(touched);
    return touched;
}

void StrokeMask::begin(int width, int height) {
    end();
    width = std::max(width, 0);
    height = std::max(height, 0);
    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        data_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    }
}

void StrokeMask::end() noexcept {
    const IRect r = intersectionOf(touched_, IRect{0, 0, width_, height_});
    for (int y = r.y0; y < r.y1; ++y)
        std::fill_n(data_.begin() + static_cast<std::ptrdiff_t>(index(r.x0, y)), r.width(), std::uint8_t{0});
    touched_ = IRect{};
}

IRect drawCapsule(Image& img, Vec2f a, Vec2f b, float radius, const Paint& paint, bool antialias,
                  StrokeMask* mask) {
    if (img.empty() || !(radius > 0.0f) || !std::isfinite(radius) || !finite(a) || !finite(b))
        return {};

    if (!antialias) {
        a = Vec2f{std::floor(a.x) + 0.5f, std::floor(a.y) + 0.5f};
        b = Vec2f{std::floor(b.x) + 0.5f, std::floor(b.y) + 0.5f};
    }

    // Clamp in float before converting so huge coordinates cannot overflow int.
    const float pad = radius + 1.0f;
    const float w = static_cast<float>(img.width());
    const float h = static_cast<float>(img.height());
    const int x0 = static_cast<int>(std::clamp(std::floor(std::min(a.x, b.x) - pad), 0.0f, w));
    const int x1 = static_cast<int>(std::clamp(std::ceil(std::max(a.x, b.x) + pad), 0.0f, w));
    const int y0 = static_cast<int>(std::clamp(std::floor(std::min(a.y, b.y) - pad), 0.0f, h));
    const int y1 = static_cast<int>(std::clamp(std::ceil(std::max(a.y, b.y) + pad), 0.0f, h));
    if (x0 >= x1 || y0 >= y1)
        return {};

    if (mask && !mask->matches(img))
        mask = nullptr;
    // Opacity of the paint itself: a mask update must account for it (see the compositing note below).
    const unsigned paintAlpha = paint.mode == BlendMode::EraseAlpha ? 255u : paint.color.a;

    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float len2 = abx * abx + aby * aby;
    const float r2 = radius * radius;

    for (int y = y0; y < y1; ++y) {
        const std::span<Rgba8> row = img.row(y);
        const float py = static_cast<float>(y) + 0.5f;
        for (int x = x0; x < x1; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            float t = 0.0f;
            if (len2 > 0.0f)
                t = std::clamp(((px - a.x) * abx + (py - a.y) * aby) / len2, 0.0f, 1.0f);
            const float cx = a.x + t * abx - px;
            const float cy = a.y + t * aby - py;
            const float d2 = cx * cx + cy * cy;

            unsigned coverage = 255;
            if (antialias) {
                const float c = std::clamp(radius + 0.5f - std::sqrt(d2), 0.0f, 1.0f);
                coverage = static_cast<unsigned>(c * 255.0f + 0.5f);
                if (coverage == 0)
                    continue;
            } else if (d2 > r2) {
                continue;
            }

            if (mask) {
                // The pixel already carries `done` coverage from earlier capsules of this stroke. Applying
                // the extra (coverage - done) / (1 - paintAlpha * done) on top yields exactly the result of
                // applying `coverage` once, so overlaps never compound.
                const unsigned done = mask->get(x, y);
                if (coverage <= done)
                    continue;
                const unsigned denom = 255u - detail::mul255(paintAlpha, done);
                const unsigned extra = std::min(255u, ((coverage - done) * 255u + denom / 2u) / denom);
                mask->set(x, y, static_cast<std::uint8_t>(coverage));
                coverage = extra;
            }
            applyPaint(row[static_cast<std::size_t>(x)], paint, coverage);
        }
    }
    const IRect touched{x0, y0, x1, y1};
    if (mask)
        mask->markTouched(touched);
    return touched;
}

IRect fillRect(Image& img, const IRect& rect, const Paint& paint) {
    const IRect r = intersectionOf(rect, imageBounds(img));
    for (int y = r.y0; y < r.y1; ++y)
        paintSpan(img, y, r.x0, r.x1 - 1, paint);
    return r;
}

IRect strokeRect(Image& img, const IRect& rect, int thickness, const Paint& paint) {
    if (rect.empty() || thickness < 1)
        return {};
    if (thickness * 2 >= rect.width() || thickness * 2 >= rect.height())
        return fillRect(img, rect, paint);

    const int t = thickness;
    IRect touched = fillRect(img, IRect{rect.x0, rect.y0, rect.x1, rect.y0 + t}, paint);
    touched = unionOf(touched, fillRect(img, IRect{rect.x0, rect.y1 - t, rect.x1, rect.y1}, paint));
    touched = unionOf(touched, fillRect(img, IRect{rect.x0, rect.y0 + t, rect.x0 + t, rect.y1 - t}, paint));
    touched = unionOf(touched, fillRect(img, IRect{rect.x1 - t, rect.y0 + t, rect.x1, rect.y1 - t}, paint));
    return touched;
}

IRect fillEllipse(Image& img, const IRect& box, const Paint& paint, int inset) {
    if (box.empty())
        return {};

    const IRect rows = intersectionOf(box, imageBounds(img));
    IRect touched;
    for (int y = rows.y0; y < rows.y1; ++y) {
        int xl = 0;
        int xr = 0;
        if (!ellipseSpan(box, inset, y, xl, xr))
            continue;
        paintSpan(img, y, xl, xr, paint);
        touched = unionOf(touched, intersectionOf(IRect{xl, y, xr + 1, y + 1}, imageBounds(img)));
    }
    return touched;
}

IRect strokeEllipse(Image& img, const IRect& box, int thickness, const Paint& paint) {
    if (box.empty() || thickness < 1)
        return {};

    const IRect rows = intersectionOf(box, imageBounds(img));
    IRect touched;
    for (int y = rows.y0; y < rows.y1; ++y) {
        int ol = 0;
        int orr = 0;
        if (!ellipseSpan(box, 0, y, ol, orr))
            continue;

        int il = 0;
        int ir = 0;
        if (ellipseSpan(box, thickness, y, il, ir)) {
            paintSpan(img, y, ol, il - 1, paint);
            paintSpan(img, y, ir + 1, orr, paint);
        } else {
            paintSpan(img, y, ol, orr, paint);
        }
        touched = unionOf(touched, intersectionOf(IRect{ol, y, orr + 1, y + 1}, imageBounds(img)));
    }
    return touched;
}

IRect floodFill(Image& img, int x, int y, Rgba8 color, int tolerance, bool contiguous) {
    if (!img.contains(x, y))
        return {};

    const int tol = std::clamp(tolerance, 0, 255);
    const Rgba8 target = img.get(x, y);
    if (tol == 0 && target == color)
        return {}; // already that color; nothing to do

    const auto matches = [&](Rgba8 p) {
        return std::abs(static_cast<int>(p.r) - target.r) <= tol && std::abs(static_cast<int>(p.g) - target.g) <= tol &&
               std::abs(static_cast<int>(p.b) - target.b) <= tol && std::abs(static_cast<int>(p.a) - target.a) <= tol;
    };

    if (!contiguous) {
        IRect touched;
        for (int py = 0; py < img.height(); ++py) {
            const std::span<Rgba8> row = img.row(py);
            int first = -1;
            int last = -1;
            for (int px = 0; px < img.width(); ++px) {
                Rgba8& pixel = row[static_cast<std::size_t>(px)];
                if (!matches(pixel))
                    continue;
                pixel = color;
                if (first < 0)
                    first = px;
                last = px;
            }
            if (first >= 0)
                touched = unionOf(touched, IRect{first, py, last + 1, py + 1});
        }
        return touched;
    }

    const int w = img.width();
    const int h = img.height();
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    const auto visitedAt = [&](int px, int py) -> std::uint8_t& {
        return visited[static_cast<std::size_t>(py) * static_cast<std::size_t>(w) + static_cast<std::size_t>(px)];
    };

    std::vector<std::pair<int, int>> stack;
    stack.emplace_back(x, y);
    IRect touched;

    while (!stack.empty()) {
        const auto [sx, sy] = stack.back();
        stack.pop_back();
        if (visitedAt(sx, sy) || !matches(img.get(sx, sy)))
            continue;

        // Grow the span left and right from the seed.
        int left = sx;
        int right = sx;
        while (left > 0 && !visitedAt(left - 1, sy) && matches(img.get(left - 1, sy)))
            --left;
        while (right < w - 1 && !visitedAt(right + 1, sy) && matches(img.get(right + 1, sy)))
            ++right;

        const std::span<Rgba8> row = img.row(sy);
        for (int px = left; px <= right; ++px) {
            visitedAt(px, sy) = 1;
            row[static_cast<std::size_t>(px)] = color;
        }
        touched = unionOf(touched, IRect{left, sy, right + 1, sy + 1});

        // Queue one seed per run of fillable pixels on the rows above and below.
        for (const int ny : {sy - 1, sy + 1}) {
            if (ny < 0 || ny >= h)
                continue;
            bool inRun = false;
            for (int px = left; px <= right; ++px) {
                const bool fillable = !visitedAt(px, ny) && matches(img.get(px, ny));
                if (fillable && !inRun)
                    stack.emplace_back(px, ny);
                inRun = fillable;
            }
        }
    }
    return touched;
}

} // namespace trace
