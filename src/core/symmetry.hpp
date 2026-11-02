#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "core/geometry.hpp"
#include "core/symmetry_set.hpp"

namespace trace {

// Mirror and rotational drawing from a SymmetrySet (core/symmetry_set.hpp): a list of mirror lines and rotation
// centers. Positions are in *half pixels* from the image's top-left edge, so position p is the point p / 2 in
// image coordinates (pixel i covers [i, i + 1)). An even p lies on a pixel edge (two pixels swap across a
// vertical or horizontal mirror there), an odd p through a pixel center (that pixel maps to itself). The default,
// kSymmetryCenter, is the middle of whatever canvas is drawn on.
//
// The plan holds the copies as isometries (rigid motions) of the image plane. Copy 0 is the original and never
// moves. The enabled items are applied in list order, each to every copy made so far: a mirror appends the
// reflection of each one, a rotation of N appends the copies turned j * 360 / N degrees (j = 1 .. N - 1)
// clockwise on screen. A copy that coincides with an earlier one (the same mirror listed twice, a half turn that
// two mirrors already make) is dropped, and an item that would take the total past kMaxSymmetryCopies is ignored.
//
// Mirrors at multiples of 45 degrees and turns by multiples of 90 degrees are *exact*: they map pixels to
// pixels, one to one (swap, negate and shift by whole or half pixels). Every other copy (a 30 degree mirror, a
// 3, 6 or 8-fold turn) is approximate by nature: its pixels are the ones containing the mapped pixel centers, and
// tools draw it as a line or capsule between mapped ends instead (see tools/tool_util.hpp), so strokes have no
// gaps.
//
// Each copy is stored as a *base* step followed by an exact *post* step. The base is whatever is not a multiple of
// 90 degrees and gets rounded to pixels once; the post is the exact part (mirrors, quarter turns) applied on top. A
// Rotate item splits its turn j * 360 / N into a base angle and whole quarter turns, so the copies a quarter turn
// apart share one rounded picture and are exact images of each other, as are the copies of a lone 6 or 8-fold
// rotation. An exact copy has no base.

inline constexpr int kMaxSymmetryRotations = 8;

// The allowed copy counts: 1 (off), 2, 3, 4, 6, 8. Anything else means 1.
constexpr int sanitizeRotations(int n) noexcept {
    return (n == 2 || n == 3 || n == 4 || n == 6 || n == 8) ? n : 1;
}

struct PixelPos {
    int x = 0;
    int y = 0;
    constexpr bool operator==(const PixelPos&) const = default;
};

// A stored axis position made concrete for a canvas side `size`: the center (kSymmetryCenter or any negative
// value) is the middle, anything else is clamped to 0 .. 2 * size.
constexpr int resolveAxis(int axis, int size) noexcept {
    return axis < 0 ? size : (axis > 2 * size ? 2 * size : axis);
}

// What to store for an axis put at `halfPixels`: the exact middle is kept as "center" so it follows the canvas size.
constexpr int storedAxis(int halfPixels, int size) noexcept {
    return halfPixels == size ? kSymmetryCenter : halfPixels;
}

class SymmetryPlan {
public:
    // An isometry p' = Ap + t of the plane as real numbers (used for the rounded base step and for comparing).
    struct Affine {
        double a = 1.0, b = 0.0, c = 0.0, d = 1.0; // x' = a x + b y + tx, y' = c x + d y + ty
        double tx = 0.0, ty = 0.0;
    };

    // An exact isometry in *doubled* coordinates (u = 2 x), where pixel centers and half-pixel positions are
    // integers: the matrix entries are 0 or +-1 and (tx, ty) is the translation in half pixels. The arithmetic
    // is integer, so mapping a pixel is exact.
    struct Grid {
        int a = 1, b = 0, c = 0, d = 1;
        std::int64_t tx = 0, ty = 0;
    };

    // One copy: `base` (when !exact) is applied first, then `post`.
    struct Copy {
        Grid post;
        Affine base;
        bool exact = true;
    };

    // The copies of `set` on a `width` x `height` canvas (an empty canvas has only the original).
    static SymmetryPlan build(const SymmetrySet& set, int width, int height) noexcept {
        SymmetryPlan plan;
        if (width <= 0 || height <= 0)
            return plan;
        for (int i = 0; i < std::clamp(set.count, 0, kMaxSymmetryItems); ++i) {
            const SymmetryItem& item = set.items[static_cast<std::size_t>(i)];
            if (!item.enabled)
                continue;
            const int px = resolveAxis(item.posX, width);
            const int py = resolveAxis(item.posY, height);
            if (item.kind == SymmetryKind::Mirror)
                plan.addMirror(normalizeMirrorAngle(item.angle), px, py);
            else
                plan.addRotate(sanitizeRotateCount(item.count), px, py);
        }
        return plan;
    }

    // Number of copies, the original included (always at least 1), and whether there is any besides the original.
    std::size_t count() const noexcept { return count_; }
    bool active() const noexcept { return count_ > 1; }

    // True when copy `k` maps pixels to pixels exactly (no rounding): then a Bresenham line or a stamp can be
    // mapped pixel for pixel. Otherwise draw a line or capsule between the mapped ends.
    bool exact(std::size_t k) const noexcept { return at(k).exact; }
    // The copy as one isometry on continuous coordinates (for drawing the guides and for tests).
    Affine transform(std::size_t k) const noexcept { return total(at(k)); }

    // Copy `k` of a continuous point.
    Vec2f point(std::size_t k, Vec2f v) const noexcept {
        const Copy& c = at(k);
        double x = static_cast<double>(v.x);
        double y = static_cast<double>(v.y);
        if (!c.exact) {
            const Affine& m = c.base;
            const double nx = m.a * x + m.b * y + m.tx;
            y = m.c * x + m.d * y + m.ty;
            x = nx;
        }
        if (isIdentity(c.post))
            return Vec2f{static_cast<float>(x), static_cast<float>(y)};
        const Grid& g = c.post;
        return Vec2f{static_cast<float>(g.a * x + g.b * y + static_cast<double>(g.tx) * 0.5),
                     static_cast<float>(g.c * x + g.d * y + static_cast<double>(g.ty) * 0.5)};
    }
    // Copy `k` of a pixel: its center is mapped and the pixel containing the result is returned.
    PixelPos pixel(std::size_t k, int i, int j) const noexcept { return extraPixel(k, basePixel(k, i, j)); }
    // Copy `k` of a pixel rectangle (half-open, stays normalized). An exact copy maps the box exactly (width and
    // height swap when it turns a quarter or reflects across a diagonal); another angle keeps the width and
    // height and moves the box's center (a rotated rectangle is not a pixel box, so shapes stay axis-aligned).
    IRect rect(std::size_t k, const IRect& r) const noexcept { return extraRect(k, baseRect(k, r)); }

    // The two halves of the above, for tools that draw a line per copy: the base step rounds (an exact copy has
    // none and these return their input), the extra step adds the exact part. Drawing the base copy and then
    // mapping it with the extra step gives the picture its quarter-turn symmetry.
    PixelPos basePixel(std::size_t k, int i, int j) const noexcept {
        const Copy& c = at(k);
        if (c.exact)
            return PixelPos{i, j};
        const Affine& m = c.base;
        const double x = static_cast<double>(i) + 0.5;
        const double y = static_cast<double>(j) + 0.5;
        return PixelPos{toIndex(std::floor(m.a * x + m.b * y + m.tx)), toIndex(std::floor(m.c * x + m.d * y + m.ty))};
    }
    // Exact step on a pixel, in doubled coordinates so the arithmetic is integer: the pixel's center is
    // (2 i + 1, 2 j + 1), and a doubled coordinate u lies in pixel floor(u / 2).
    PixelPos extraPixel(std::size_t k, PixelPos p) const noexcept {
        const Grid& g = at(k).post;
        if (isIdentity(g))
            return PixelPos{clampIndex(p.x), clampIndex(p.y)};
        const std::int64_t x = 2 * std::int64_t{p.x} + 1;
        const std::int64_t y = 2 * std::int64_t{p.y} + 1;
        return PixelPos{clampIndex((g.a * x + g.b * y + g.tx) >> 1), clampIndex((g.c * x + g.d * y + g.ty) >> 1)};
    }
    IRect baseRect(std::size_t k, const IRect& r) const noexcept {
        const Copy& c = at(k);
        if (c.exact)
            return r;
        const Affine& m = c.base;
        const double cx = (static_cast<double>(r.x0) + static_cast<double>(r.x1)) * 0.5;
        const double cy = (static_cast<double>(r.y0) + static_cast<double>(r.y1)) * 0.5;
        const double w = static_cast<double>(r.x1) - static_cast<double>(r.x0);
        const double h = static_cast<double>(r.y1) - static_cast<double>(r.y0);
        const int x0 = roundEdge(m.a * cx + m.b * cy + m.tx - w * 0.5);
        const int y0 = roundEdge(m.c * cx + m.d * cy + m.ty - h * 0.5);
        return IRect{x0, y0, x0 + static_cast<int>(w), y0 + static_cast<int>(h)};
    }
    // Pixel edges are integers; an edge e is the doubled coordinate 2 e, and a result u / 2 rounds half up.
    IRect extraRect(std::size_t k, const IRect& r) const noexcept {
        const Grid& g = at(k).post;
        if (isIdentity(g))
            return IRect{clampIndex(r.x0), clampIndex(r.y0), clampIndex(r.x1), clampIndex(r.y1)};
        const auto edgeX = [&](std::int64_t x, std::int64_t y) { return clampIndex((g.a * 2 * x + g.b * 2 * y + g.tx + 1) >> 1); };
        const auto edgeY = [&](std::int64_t x, std::int64_t y) { return clampIndex((g.c * 2 * x + g.d * 2 * y + g.ty + 1) >> 1); };
        const int ax = edgeX(r.x0, r.y0);
        const int ay = edgeY(r.x0, r.y0);
        const int bx = edgeX(r.x1, r.y1);
        const int by = edgeY(r.x1, r.y1);
        return IRect{std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by)};
    }

private:
    // One item as a transform to apply to a copy: T = q o rb (rb first). `plain` means rb is the identity, so the
    // item is exact and only changes a copy's post step.
    struct Step {
        Grid q;
        Affine rb;
        bool plain = true;
    };

    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kLinearEps = 1.0e-6;
    static constexpr double kShiftEps = 1.0e-4;

    // Float to int without undefined behavior: a rotated or hostile value is clamped (NaN becomes 0).
    static constexpr double kIndexLimit = 1.0e9;
    static int toIndex(double v) noexcept {
        return v == v ? static_cast<int>(std::clamp(v, -kIndexLimit, kIndexLimit)) : 0;
    }
    static int clampIndex(std::int64_t v) noexcept {
        return static_cast<int>(std::clamp<std::int64_t>(v, -1000000000, 1000000000));
    }
    static int roundEdge(double v) noexcept { return toIndex(std::floor(v + 0.5)); }

    const Copy& at(std::size_t k) const noexcept { return copies_[k < count_ ? k : 0]; }

    static bool isIdentity(const Grid& g) noexcept {
        return g.a == 1 && g.b == 0 && g.c == 0 && g.d == 1 && g.tx == 0 && g.ty == 0;
    }
    static Affine toAffine(const Grid& g) noexcept {
        return Affine{static_cast<double>(g.a), static_cast<double>(g.b), static_cast<double>(g.c),
                      static_cast<double>(g.d), static_cast<double>(g.tx) * 0.5, static_cast<double>(g.ty) * 0.5};
    }
    // f after g.
    static Grid compose(const Grid& f, const Grid& g) noexcept {
        return Grid{f.a * g.a + f.b * g.c,
                    f.a * g.b + f.b * g.d,
                    f.c * g.a + f.d * g.c,
                    f.c * g.b + f.d * g.d,
                    f.a * g.tx + f.b * g.ty + f.tx,
                    f.c * g.tx + f.d * g.ty + f.ty};
    }
    static Affine compose(const Affine& f, const Affine& g) noexcept {
        return Affine{f.a * g.a + f.b * g.c,
                      f.a * g.b + f.b * g.d,
                      f.c * g.a + f.d * g.c,
                      f.c * g.b + f.d * g.d,
                      f.a * g.tx + f.b * g.ty + f.tx,
                      f.c * g.tx + f.d * g.ty + f.ty};
    }
    static Affine total(const Copy& c) noexcept {
        return c.exact ? toAffine(c.post) : compose(toAffine(c.post), c.base);
    }
    static bool same(const Affine& p, const Affine& q) noexcept {
        return std::fabs(p.a - q.a) < kLinearEps && std::fabs(p.b - q.b) < kLinearEps &&
               std::fabs(p.c - q.c) < kLinearEps && std::fabs(p.d - q.d) < kLinearEps &&
               std::fabs(p.tx - q.tx) < kShiftEps && std::fabs(p.ty - q.ty) < kShiftEps;
    }
    // The exact form of `t` when its matrix is axis-aligned (entries 0 or +-1) and it moves by whole half pixels.
    static bool snapToGrid(const Affine& t, Grid& out) noexcept {
        const double v[6] = {t.a, t.b, t.c, t.d, t.tx * 2.0, t.ty * 2.0};
        long long r[6];
        for (int i = 0; i < 6; ++i) {
            if (!std::isfinite(v[i]) || std::fabs(v[i]) > 1.0e12)
                return false;
            r[i] = std::llround(v[i]);
            if (std::fabs(v[i] - static_cast<double>(r[i])) > (i < 4 ? 1.0e-9 : 1.0e-6))
                return false;
        }
        out = Grid{static_cast<int>(r[0]), static_cast<int>(r[1]), static_cast<int>(r[2]), static_cast<int>(r[3]), r[4], r[5]};
        return true;
    }

    // Mirror across the line through the half-pixel point (px, py) that flips points along (cos a, sin a).
    static Step mirrorStep(int angle, int px, int py) noexcept {
        Step s;
        const std::int64_t x = px;
        const std::int64_t y = py;
        switch (angle) {
        case 0: s.q = Grid{-1, 0, 0, 1, 2 * x, 0}; return s;
        case 45: s.q = Grid{0, -1, -1, 0, x + y, x + y}; return s;
        case 90: s.q = Grid{1, 0, 0, -1, 0, 2 * y}; return s;
        case 135: s.q = Grid{0, 1, 1, 0, x - y, y - x}; return s;
        default: break;
        }
        const double a = static_cast<double>(angle) * kPi / 180.0;
        const double c = std::cos(a);
        const double sn = std::sin(a);
        const double along = static_cast<double>(px) * c + static_cast<double>(py) * sn; // 2 (P . n), P = p / 2
        s.plain = false;
        s.rb = Affine{1.0 - 2.0 * c * c, -2.0 * c * sn, -2.0 * c * sn, 1.0 - 2.0 * sn * sn, along * c, along * sn};
        return s;
    }

    // Turn number `j` (1 .. n - 1) of an n-fold rotation about (px, py): a base angle plus whole quarter turns.
    static Step rotateStep(int n, int j, int px, int py) noexcept {
        const int group = n % 4 == 0 ? 4 : (n % 2 == 0 ? 2 : 1); // turns in the 90 degree subgroup
        const int stride = n / group;
        const int quarters = ((j / stride) * (4 / group)) & 3;
        const int rest = j % stride;
        Step s;
        const int m[4][4] = {{1, 0, 0, 1}, {0, -1, 1, 0}, {-1, 0, 0, -1}, {0, 1, -1, 0}}; // clockwise on screen
        const int* q = m[quarters];
        s.q = Grid{q[0], q[1], q[2], q[3], px - (q[0] * std::int64_t{px} + q[1] * std::int64_t{py}),
                   py - (q[2] * std::int64_t{px} + q[3] * std::int64_t{py})};
        if (rest != 0) {
            const double a = 2.0 * kPi * static_cast<double>(rest) / static_cast<double>(n);
            const double c = std::cos(a);
            const double sn = std::sin(a);
            const double cx = static_cast<double>(px) * 0.5;
            const double cy = static_cast<double>(py) * 0.5;
            s.plain = false;
            s.rb = Affine{c, -sn, sn, c, cx - (c * cx - sn * cy), cy - (sn * cx + c * cy)};
        }
        return s;
    }

    // `s` applied to copy `c`.
    static Copy apply(const Step& s, const Copy& c) noexcept {
        Copy out;
        if (s.plain) {
            out.post = compose(s.q, c.post);
            out.base = c.base;
            out.exact = c.exact;
        } else {
            out.post = s.q;
            out.base = compose(s.rb, compose(toAffine(c.post), c.base));
            out.exact = false;
        }
        Grid g;
        if (!out.exact && snapToGrid(total(out), g)) // e.g. two odd-angle mirrors that make a half turn
            out = Copy{g, Affine{}, true};
        return out;
    }

    // Appends `c` unless it coincides with a copy already there.
    void append(const Copy& c) noexcept {
        const Affine t = total(c);
        for (std::size_t i = 0; i < count_; ++i)
            if (same(t, total(copies_[i])))
                return;
        copies_[count_++] = c;
    }

    void addMirror(int angle, int px, int py) noexcept {
        if (count_ * 2 > kMaxSymmetryCopies)
            return;
        const Step s = mirrorStep(angle, px, py);
        const std::size_t existing = count_;
        for (std::size_t i = 0; i < existing; ++i)
            append(apply(s, copies_[i]));
    }

    void addRotate(int n, int px, int py) noexcept {
        if (count_ * static_cast<std::size_t>(n) > kMaxSymmetryCopies)
            return;
        const std::size_t existing = count_;
        for (int j = 1; j < n; ++j) {
            const Step s = rotateStep(n, j, px, py);
            for (std::size_t i = 0; i < existing; ++i)
                append(apply(s, copies_[i]));
        }
    }

    std::array<Copy, kMaxSymmetryCopies> copies_{};
    std::size_t count_ = 1; // copies_[0] is the identity
};

// The plan for a `width` x `height` canvas.
inline SymmetryPlan resolveSymmetry(const SymmetrySet& set, int width, int height) noexcept {
    return SymmetryPlan::build(set, width, height);
}

// Axis position <-> what the user types. Pixels: p / 2 (so 31.5 is a pixel center). Percent of the canvas side:
// 0 at the left or top edge, 50 in the middle, 100 at the right or bottom edge.
constexpr double axisToPixels(int axis, int size) noexcept {
    return static_cast<double>(resolveAxis(axis, size)) * 0.5;
}
inline double axisToPercent(int axis, int size) noexcept {
    return size > 0 ? static_cast<double>(resolveAxis(axis, size)) * 50.0 / static_cast<double>(size) : 50.0;
}
// Typed values become the nearest half pixel inside the canvas; NaN and infinity give the center (kSymmetryCenter).
inline int axisFromPixels(double pixels, int size) noexcept {
    if (!std::isfinite(pixels) || size <= 0)
        return kSymmetryCenter;
    return static_cast<int>(std::clamp(std::floor(pixels * 2.0 + 0.5), 0.0, 2.0 * static_cast<double>(size)));
}
inline int axisFromPercent(double percent, int size) noexcept {
    if (!std::isfinite(percent) || size <= 0)
        return kSymmetryCenter;
    return axisFromPixels(percent * 0.01 * static_cast<double>(size), size);
}

} // namespace trace
