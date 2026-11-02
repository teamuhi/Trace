#include "flow/flow_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace trace::flow {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTinyLen = 1.0e-6f;
constexpr float kBigDistance = 1.0e30f; // "no path": large but finite
constexpr float kMaxTolerance = 4.0f * kMaxCoord;

float clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

// A radius / tolerance / threshold: NaN and negatives become 0, huge values are capped.
float sanitizeTol(float v) noexcept {
    if (std::isnan(v) || v < 0.0f) {
        return 0.0f;
    }
    return std::min(v, kMaxTolerance);
}

Pt add(Pt a, Pt b) noexcept { return Pt{a.x + b.x, a.y + b.y}; }
Pt sub(Pt a, Pt b) noexcept { return Pt{a.x - b.x, a.y - b.y}; }
Pt mul(Pt a, float s) noexcept { return Pt{a.x * s, a.y * s}; }
float dot(Pt a, Pt b) noexcept { return a.x * b.x + a.y * b.y; }
float lenOf(Pt a) noexcept { return std::sqrt(a.x * a.x + a.y * a.y); }

Pt normalized(Pt v, Pt fallback) noexcept {
    const float l = lenOf(v);
    if (!(l > kTinyLen) || !std::isfinite(l)) {
        return fallback;
    }
    return Pt{v.x / l, v.y / l};
}

// Snap a direction to the nearest axis (ties go horizontal); a zero or non-finite vector is Right.
Pt cardinal(Pt n) noexcept {
    if (!std::isfinite(n.x) || !std::isfinite(n.y) || (n.x == 0.0f && n.y == 0.0f)) {
        return Pt{1.0f, 0.0f};
    }
    if (std::abs(n.x) >= std::abs(n.y)) {
        return Pt{n.x >= 0.0f ? 1.0f : -1.0f, 0.0f};
    }
    return Pt{0.0f, n.y >= 0.0f ? 1.0f : -1.0f};
}

Pt cardinalToward(Pt from, Pt to) noexcept { return cardinal(sub(to, from)); }

Pt faceNormal(Face f) noexcept {
    switch (f) {
    case Face::Top: return Pt{0.0f, -1.0f};
    case Face::Bottom: return Pt{0.0f, 1.0f};
    case Face::Left: return Pt{-1.0f, 0.0f};
    case Face::Right:
    case Face::Auto: break;
    }
    return Pt{1.0f, 0.0f};
}

// Quarter turns between the four real faces, in the order Top, Right, Bottom, Left (clockwise on screen).
Face turnCw(Face f) noexcept {
    switch (f) {
    case Face::Top: return Face::Right;
    case Face::Right: return Face::Bottom;
    case Face::Bottom: return Face::Left;
    case Face::Left:
    case Face::Auto: break;
    }
    return Face::Top;
}
Face turnCcw(Face f) noexcept {
    switch (f) {
    case Face::Top: return Face::Left;
    case Face::Left: return Face::Bottom;
    case Face::Bottom: return Face::Right;
    case Face::Right:
    case Face::Auto: break;
    }
    return Face::Top;
}
Face opposite(Face f) noexcept { return turnCw(turnCw(f)); }

constexpr Face kRealFaces[4] = {Face::Top, Face::Right, Face::Bottom, Face::Left};

// --- shape constants -----------------------------------------------------------------------------------
float parallelogramSkew(const Rect& r) noexcept { return std::min(r.w * 0.25f, r.h * 0.4f); }
float hexagonInset(const Rect& r) noexcept { return std::min(r.w * 0.25f, r.h * 0.5f); }
float documentWaveAmp(const Rect& r) noexcept { return clampf(r.h * 0.1f, 0.0f, 16.0f); }
float databaseCapRadius(const Rect& r) noexcept { return clampf(std::min(r.h * 0.15f, r.w * 0.25f), 0.0f, r.h * 0.5f); }

int arcSteps(float radius) noexcept { return static_cast<int>(clampf(std::ceil(radius / 4.0f), 3.0f, 12.0f)); }

// Points of an elliptical arc from angle a0 to a1 (inclusive) centred at c.
void appendArc(std::vector<Pt>& out, Pt c, float rx, float ry, float a0, float a1, int steps) {
    for (int i = 0; i <= steps; ++i) {
        const float a = a0 + (a1 - a0) * (static_cast<float>(i) / static_cast<float>(steps));
        out.push_back(Pt{c.x + rx * std::cos(a), c.y + ry * std::sin(a)});
    }
}

std::vector<Pt> rectPoints(const Rect& r) {
    return {Pt{r.x, r.y}, Pt{r.right(), r.y}, Pt{r.right(), r.bottom()}, Pt{r.x, r.bottom()}};
}

std::vector<Pt> roundedPoints(const Rect& r, float radius) {
    const float rad = clampf(radius, 0.0f, std::min(r.w, r.h) * 0.5f);
    if (rad < 0.01f) {
        return rectPoints(r);
    }
    const int steps = arcSteps(rad);
    std::vector<Pt> out;
    out.reserve(static_cast<std::size_t>(steps + 1) * 4);
    appendArc(out, Pt{r.x + rad, r.y + rad}, rad, rad, kPi, kPi * 1.5f, steps);                      // top-left
    appendArc(out, Pt{r.right() - rad, r.y + rad}, rad, rad, kPi * 1.5f, kPi * 2.0f, steps);         // top-right
    appendArc(out, Pt{r.right() - rad, r.bottom() - rad}, rad, rad, 0.0f, kPi * 0.5f, steps);        // bottom-right
    appendArc(out, Pt{r.x + rad, r.bottom() - rad}, rad, rad, kPi * 0.5f, kPi, steps);               // bottom-left
    return out;
}

std::vector<Pt> ellipsePoints(const Rect& r) {
    // A multiple of 4 so the four extremes are vertices.
    const int n = 4 * static_cast<int>(clampf(std::ceil((r.w + r.h) / 64.0f), 8.0f, 32.0f));
    const Pt c = r.center();
    std::vector<Pt> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float a = -kPi * 0.5f + 2.0f * kPi * (static_cast<float>(i) / static_cast<float>(n));
        out.push_back(Pt{c.x + r.w * 0.5f * std::cos(a), c.y + r.h * 0.5f * std::sin(a)});
    }
    return out;
}

// y of the wavy document base at fraction t along the width.
float documentWaveY(const Rect& r, float amp, float t) noexcept {
    return r.bottom() - amp - amp * std::sin(2.0f * kPi * t);
}

std::vector<Pt> documentPoints(const Rect& r) {
    const float amp = documentWaveAmp(r);
    std::vector<Pt> out{Pt{r.x, r.y}, Pt{r.right(), r.y}};
    constexpr int kSteps = 16;
    for (int i = 0; i <= kSteps; ++i) {
        const float t = 1.0f - static_cast<float>(i) / static_cast<float>(kSteps); // right to left
        out.push_back(Pt{r.x + r.w * t, documentWaveY(r, amp, t)});
    }
    return out;
}

std::vector<Pt> databasePoints(const Rect& r) {
    const float ry = databaseCapRadius(r);
    const float rx = r.w * 0.5f;
    const int steps = std::max(8, arcSteps(rx));
    std::vector<Pt> out;
    out.reserve(static_cast<std::size_t>(steps + 1) * 2);
    appendArc(out, Pt{r.x + rx, r.y + ry}, rx, ry, kPi, kPi * 2.0f, steps);                  // top cap, left over the top to right
    appendArc(out, Pt{r.x + rx, r.bottom() - ry}, rx, ry, 0.0f, kPi, steps);                 // bottom, right under to left
    return out;
}

// A regular polygon with `sides` corners inscribed in the rect (stretched with it), first corner at the top.
std::vector<Pt> polygonPoints(const Rect& r, int sides) {
    const int n = std::clamp(sides, kMinPolygonSides, kMaxPolygonSides);
    const Pt c = r.center();
    std::vector<Pt> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float a = -kPi * 0.5f + kPi * 2.0f * static_cast<float>(i) / static_cast<float>(n);
        out.push_back(Pt{c.x + std::cos(a) * r.w * 0.5f, c.y + std::sin(a) * r.h * 0.5f});
    }
    return out;
}

// Where the ray from `c` along `dir` leaves a convex polygon; `fallback` if it never does (degenerate input).
Pt rayExit(Pt c, Pt dir, const std::vector<Pt>& poly, Pt fallback) {
    float best = -1.0f;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Pt a = poly[i];
        const Pt b = poly[(i + 1) % poly.size()];
        const Pt e{b.x - a.x, b.y - a.y};
        const float denom = dir.x * e.y - dir.y * e.x;
        if (std::abs(denom) < 1.0e-9f) {
            continue;
        }
        const Pt ac{a.x - c.x, a.y - c.y};
        const float t = (ac.x * e.y - ac.y * e.x) / denom;
        const float u = (ac.x * dir.y - ac.y * dir.x) / denom;
        if (t >= 0.0f && u >= -1.0e-5f && u <= 1.0f + 1.0e-5f) {
            best = std::max(best, t);
        }
    }
    return best < 0.0f ? fallback : Pt{c.x + dir.x * best, c.y + dir.y * best};
}

bool isRectKind(NodeKind k) noexcept {
    switch (k) {
    case NodeKind::Rectangle:
    case NodeKind::Sticky:
    case NodeKind::Text:
    case NodeKind::Image:
    case NodeKind::Container: return true;
    default: break;
    }
    return false;
}

// --- polyline helpers ----------------------------------------------------------------------------------
float segmentDistance(Pt p, Pt a, Pt b) noexcept {
    const Pt ab = sub(b, a);
    const float l2 = dot(ab, ab);
    float t = 0.0f;
    if (l2 > 0.0f) {
        t = clampf(dot(sub(p, a), ab) / l2, 0.0f, 1.0f);
    }
    const Pt q{a.x + ab.x * t, a.y + ab.y * t};
    return lenOf(sub(p, q));
}

// Unit direction of the first (or last) non-degenerate segment.
Pt endDirection(const std::vector<Pt>& pts, bool atStart, Pt fallback) noexcept {
    const std::size_t n = pts.size();
    for (std::size_t i = 1; i < n; ++i) {
        const Pt a = atStart ? pts[i - 1] : pts[n - i];
        const Pt b = atStart ? pts[i] : pts[n - i - 1];
        const Pt d = atStart ? sub(b, a) : sub(a, b); // always in the direction of travel
        if (lenOf(d) > kTinyLen) {
            return normalized(d, fallback);
        }
    }
    return fallback;
}

std::vector<Pt> flattenCubic(Pt p0, Pt p1, Pt p2, Pt p3, int steps) {
    std::vector<Pt> out;
    out.reserve(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float u = 1.0f - t;
        const float b0 = u * u * u;
        const float b1 = 3.0f * u * u * t;
        const float b2 = 3.0f * u * t * t;
        const float b3 = t * t * t;
        out.push_back(Pt{b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x, b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y});
    }
    return out;
}

// Remove duplicate points and middle points of straight, same-direction runs (local-frame elbow points).
void simplifyOrtho(std::vector<Pt>& p) {
    constexpr float kEq = 1.0e-3f;
    std::vector<Pt> out;
    out.reserve(p.size());
    for (const Pt& q : p) {
        if (!out.empty() && std::abs(out.back().x - q.x) < kEq && std::abs(out.back().y - q.y) < kEq) {
            continue;
        }
        while (out.size() >= 2) {
            const Pt a = out[out.size() - 2];
            const Pt b = out.back();
            const bool horiz = std::abs(a.y - b.y) < kEq && std::abs(b.y - q.y) < kEq && (b.x - a.x) * (q.x - b.x) > 0.0f;
            const bool vert = std::abs(a.x - b.x) < kEq && std::abs(b.x - q.x) < kEq && (b.y - a.y) * (q.y - b.y) > 0.0f;
            if (!horiz && !vert) {
                break;
            }
            out.pop_back();
        }
        out.push_back(q);
    }
    p = std::move(out);
}

struct LocalBounds {
    float minU, maxU, minV, maxV;
};

// Orthogonal route. Works in a frame where A is the origin and A's normal is +u (a rotation by a multiple of
// 90 degrees, so every segment stays axis-aligned in world space).
// `via` (nullopt = automatic) moves the middle segment of a Z, U or detour route; a plain L has none and ignores it.
// `handle` reports that segment: its position, its automatic (via = 0) position and the unit direction it slides in.
struct ElbowHandle {
    bool valid = false;
    Pt pos;
    Pt base;
    Pt axis;
};

std::vector<Pt> elbowPoints(const RouteEnd& a, const RouteEnd& b, const std::optional<Pt>& via, ElbowHandle& handle) {
    const Pt nA = cardinal(a.anchor.normal);
    const Pt nB = cardinal(b.anchor.normal);
    const Pt perp{-nA.y, nA.x};
    const Pt A = a.anchor.point;
    const Pt B = b.anchor.point;
    auto toLocal = [&](Pt p) { return Pt{dot(sub(p, A), nA), dot(sub(p, A), perp)}; };
    auto toWorld = [&](Pt q) {
        return Pt{A.x + q.x * nA.x + q.y * perp.x, A.y + q.x * nA.y + q.y * perp.y};
    };
    auto boundsLocal = [&](const RouteEnd& e) {
        const Rect r = e.hasBounds ? e.bounds : Rect{e.anchor.point.x, e.anchor.point.y, 0.0f, 0.0f};
        const Pt corners[4] = {toLocal(Pt{r.x, r.y}), toLocal(Pt{r.right(), r.y}), toLocal(Pt{r.right(), r.bottom()}),
                               toLocal(Pt{r.x, r.bottom()})};
        LocalBounds lb{corners[0].x, corners[0].x, corners[0].y, corners[0].y};
        for (const Pt& c : corners) {
            lb.minU = std::min(lb.minU, c.x);
            lb.maxU = std::max(lb.maxU, c.x);
            lb.minV = std::min(lb.minV, c.y);
            lb.maxV = std::max(lb.maxV, c.y);
        }
        return lb;
    };

    constexpr float kAhead = 0.5f;
    const float m = kElbowMargin;
    const Pt Bl = toLocal(B);
    const Pt nb{dot(nB, nA), dot(nB, perp)}; // one of (+-1, 0), (0, +-1)
    const float viaU = via ? dot(*via, nA) : 0.0f;   // the manual offset along A's normal and across it
    const float viaV = via ? dot(*via, perp) : 0.0f;
    Pt hPos, hBase, hAxis; // the handle in the local frame
    bool hValid = false;
    const auto setHandle = [&](Pt pos, Pt base, Pt axis) {
        hPos = pos;
        hBase = base;
        hAxis = axis;
        hValid = true;
    };

    std::vector<Pt> L;
    L.push_back(Pt{0.0f, 0.0f});
    if (nb.y == 0.0f) {
        if (nb.x < 0.0f) { // the faces look at each other
            if (Bl.x > kAhead) {
                const float um0 = Bl.x * 0.5f;
                const float q = std::min(4.0f, Bl.x * 0.25f); // keep the jog between the two faces
                const float um = clampf(um0 + viaU, q, Bl.x - q);
                L.push_back(Pt{um, 0.0f});
                L.push_back(Pt{um, Bl.y});
                if (std::abs(Bl.y) >= 0.5f)
                    setHandle(Pt{um, Bl.y * 0.5f}, Pt{um0, Bl.y * 0.5f}, Pt{1.0f, 0.0f});
            } else { // B is behind or beside A: leave, run along a lane outside both nodes, come back in
                const LocalBounds la = boundsLocal(a);
                const LocalBounds lb = boundsLocal(b);
                const float hi = std::max(la.maxV, lb.maxV) + m;
                const float lo = std::min(la.minV, lb.minV) - m;
                const float costHi = std::abs(hi) + std::abs(Bl.y - hi);
                const float costLo = std::abs(lo) + std::abs(Bl.y - lo);
                const float lane0 = costHi <= costLo ? hi : lo;
                const float lane = lane0 + viaV;
                L.push_back(Pt{m, 0.0f});
                L.push_back(Pt{m, lane});
                L.push_back(Pt{Bl.x - m, lane});
                L.push_back(Pt{Bl.x - m, Bl.y});
                setHandle(Pt{Bl.x * 0.5f, lane}, Pt{Bl.x * 0.5f, lane0}, Pt{0.0f, 1.0f});
            }
        } else { // both leave the same way: a U around the far one
            const float um0 = std::max(0.0f, Bl.x) + m;
            const float um = std::max(um0 + viaU, std::max(0.0f, Bl.x) + 2.0f);
            L.push_back(Pt{um, 0.0f});
            L.push_back(Pt{um, Bl.y});
            if (std::abs(Bl.y) >= 0.5f)
                setHandle(Pt{um, Bl.y * 0.5f}, Pt{um0, Bl.y * 0.5f}, Pt{1.0f, 0.0f});
        }
    } else {
        const float s = nb.y;
        if (Bl.x > kAhead && s * Bl.y < -kAhead) { // a plain L
            L.push_back(Pt{Bl.x, 0.0f});
        } else {
            const float ev0 = Bl.y + s * m;
            const float ev = ev0 + viaV;
            L.push_back(Pt{m, 0.0f});
            L.push_back(Pt{m, ev});
            L.push_back(Pt{Bl.x, ev});
            setHandle(Pt{(m + Bl.x) * 0.5f, ev}, Pt{(m + Bl.x) * 0.5f, ev0}, Pt{0.0f, 1.0f});
        }
    }
    L.push_back(Bl);
    simplifyOrtho(L);

    std::vector<Pt> world;
    world.reserve(L.size());
    for (const Pt& q : L) {
        world.push_back(toWorld(q));
    }
    world.front() = A;
    world.back() = B;
    handle.valid = hValid;
    if (hValid) {
        handle.pos = toWorld(hPos);
        handle.base = toWorld(hBase);
        handle.axis = Pt{hAxis.x * nA.x + hAxis.y * perp.x, hAxis.x * nA.y + hAxis.y * perp.y};
    }
    return world;
}

} // namespace

// --- sanitising --------------------------------------------------------------------------------------
float sanitizeCoord(float v) noexcept {
    if (std::isnan(v)) {
        return 0.0f;
    }
    return clampf(v, -kMaxCoord, kMaxCoord);
}

Pt sanitize(Pt p) noexcept { return Pt{sanitizeCoord(p.x), sanitizeCoord(p.y)}; }

Rect sanitize(Rect r) noexcept {
    r.x = sanitizeCoord(r.x);
    r.y = sanitizeCoord(r.y);
    r.w = std::isnan(r.w) ? 0.0f : clampf(r.w, 0.0f, kMaxCoord);
    r.h = std::isnan(r.h) ? 0.0f : clampf(r.h, 0.0f, kMaxCoord);
    return r;
}

// --- outlines ----------------------------------------------------------------------------------------
std::vector<Pt> outlineFor(NodeKind kind, Rect rect, float cornerRadius, int sides) {
    const Rect r = sanitize(rect);
    const float cr = std::isnan(cornerRadius) ? 0.0f : cornerRadius;
    const Pt c = r.center();
    switch (kind) {
    case NodeKind::Rounded: return roundedPoints(r, cr);
    case NodeKind::Terminator: return roundedPoints(r, std::min(r.w, r.h) * 0.5f);
    case NodeKind::Ellipse:
    case NodeKind::Circle: return ellipsePoints(r);
    case NodeKind::Diamond: return {Pt{c.x, r.y}, Pt{r.right(), c.y}, Pt{c.x, r.bottom()}, Pt{r.x, c.y}};
    case NodeKind::Parallelogram: {
        const float k = parallelogramSkew(r);
        return {Pt{r.x + k, r.y}, Pt{r.right(), r.y}, Pt{r.right() - k, r.bottom()}, Pt{r.x, r.bottom()}};
    }
    case NodeKind::Hexagon: {
        const float k = hexagonInset(r);
        return {Pt{r.x + k, r.y}, Pt{r.right() - k, r.y}, Pt{r.right(), c.y},
                Pt{r.right() - k, r.bottom()}, Pt{r.x + k, r.bottom()}, Pt{r.x, c.y}};
    }
    case NodeKind::Document: return documentPoints(r);
    case NodeKind::Database: return databasePoints(r);
    case NodeKind::Polygon: return polygonPoints(r, sides);
    case NodeKind::Rectangle:
    case NodeKind::Sticky:
    case NodeKind::Text:
    case NodeKind::Image:
    case NodeKind::Container: break;
    }
    return rectPoints(r);
}

std::vector<Pt> nodeOutline(const Node& node) { return outlineFor(node.kind, node.rect, node.style.cornerRadius, node.sides); }

std::vector<Pt> databaseRim(Rect rect) {
    const Rect r = sanitize(rect);
    const float ry = databaseCapRadius(r);
    const float rx = r.w * 0.5f;
    std::vector<Pt> out;
    // Angles pi -> 0 sweep through pi/2, the lower half of the cap (y grows downward), left to right.
    appendArc(out, Pt{r.x + rx, r.y + ry}, rx, ry, kPi, 0.0f, std::max(8, arcSteps(rx)));
    return out;
}

Rect containerTitleBand(Rect rect) {
    const Rect r = sanitize(rect);
    return Rect{r.x, r.y, r.w, std::min(kContainerTitleHeight, r.h)};
}

// --- faces -------------------------------------------------------------------------------------------
Anchor faceAnchor(NodeKind kind, Rect rect, Face face, int sides) {
    const Rect r = sanitize(rect);
    const Pt c = r.center();
    Pt p = c;
    switch (face) {
    case Face::Top: p = Pt{c.x, r.y}; break;
    case Face::Bottom: p = Pt{c.x, r.bottom()}; break;
    case Face::Left: p = Pt{r.x, c.y}; break;
    case Face::Right:
    case Face::Auto: p = Pt{r.right(), c.y}; break;
    }
    if (kind == NodeKind::Parallelogram) { // the slanted sides pass through their midpoints, not the bbox's
        const float k = parallelogramSkew(r) * 0.5f;
        if (face == Face::Left) {
            p.x = r.x + k;
        } else if (face == Face::Right || face == Face::Auto) {
            p.x = r.right() - k;
        }
    } else if (kind == NodeKind::Document && face == Face::Bottom) {
        p.y = documentWaveY(r, documentWaveAmp(r), 0.5f);
    } else if (kind == NodeKind::Polygon) { // where the axis ray leaves the corners' outline
        const Anchor n = Anchor{p, faceNormal(face == Face::Auto ? Face::Right : face)};
        p = rayExit(c, n.normal, polygonPoints(r, sides), p);
    }
    return Anchor{p, faceNormal(face)};
}

Anchor faceAnchor(const Node& node, Face face) { return faceAnchor(node.kind, node.rect, face, node.sides); }

// --- routing -----------------------------------------------------------------------------------------
float polylineLength(std::span<const Pt> points) {
    float total = 0.0f;
    for (std::size_t i = 1; i < points.size(); ++i) {
        total += lenOf(sub(points[i], points[i - 1]));
    }
    return total;
}

Pt pointAtFraction(std::span<const Pt> points, float fraction) {
    if (points.empty()) {
        return Pt{};
    }
    const float f = std::isnan(fraction) ? 0.0f : clampf(fraction, 0.0f, 1.0f);
    const float target = polylineLength(points) * f;
    float walked = 0.0f;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const float seg = lenOf(sub(points[i], points[i - 1]));
        if (walked + seg >= target && seg > 0.0f) {
            const float t = (target - walked) / seg;
            return Pt{points[i - 1].x + (points[i].x - points[i - 1].x) * t, points[i - 1].y + (points[i].y - points[i - 1].y) * t};
        }
        walked += seg;
    }
    return points.back();
}

Pt tangentAtFraction(std::span<const Pt> points, float fraction) {
    if (points.size() < 2) {
        return Pt{1.0f, 0.0f};
    }
    const float f = std::isnan(fraction) ? 0.0f : clampf(fraction, 0.0f, 1.0f);
    const float target = polylineLength(points) * f;
    float walked = 0.0f;
    Pt last{1.0f, 0.0f};
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Pt d = sub(points[i], points[i - 1]);
        const float seg = lenOf(d);
        if (seg <= 0.0f) {
            continue;
        }
        last = Pt{d.x / seg, d.y / seg};
        if (walked + seg >= target) {
            return last;
        }
        walked += seg;
    }
    return last;
}

namespace {

// The unit normal of a tangent that faces screen-up (to the right for a vertical line).
Pt aboveNormal(Pt tangent) noexcept {
    Pt n{tangent.y, -tangent.x};
    if (n.y > 1.0e-6f || (std::abs(n.y) <= 1.0e-6f && n.x < 0.0f)) {
        n = Pt{-n.x, -n.y};
    }
    return n;
}

} // namespace

float labelExtent(float angle, float halfW, float halfH, Pt dir) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const Pt ax{c, s};
    const Pt ay{-s, c};
    return std::abs(dot(dir, ax)) * halfW + std::abs(dot(dir, ay)) * halfH;
}

LabelPlacement placeLabel(std::span<const Pt> points, float t, bool along, LabelSide side, float halfW, float halfH) {
    LabelPlacement out;
    if (points.empty()) {
        return out;
    }
    const float f = std::isnan(t) ? 0.5f : clampf(t, 0.0f, 1.0f);
    const Pt tan = tangentAtFraction(points, f);
    out.base = pointAtFraction(points, f);
    out.center = out.base;
    float angle = 0.0f;
    if (along) {
        angle = std::atan2(tan.y, tan.x);
        if (angle > kPi * 0.5f) {
            angle -= kPi;
        } else if (angle <= -kPi * 0.5f) {
            angle += kPi;
        }
    }
    out.angle = angle;
    if (side != LabelSide::On) {
        constexpr float kGap = 6.0f;
        const Pt n = aboveNormal(tan);
        const float d = labelExtent(angle, std::max(halfW, 0.0f), std::max(halfH, 0.0f), n) + kGap;
        const float sign = side == LabelSide::Above ? 1.0f : -1.0f;
        out.center = Pt{out.base.x + n.x * d * sign, out.base.y + n.y * d * sign};
    }
    return out;
}

PathProjection projectOnPath(std::span<const Pt> points, Pt p) {
    PathProjection out;
    if (points.empty() || std::isnan(p.x) || std::isnan(p.y)) {
        return out;
    }
    if (points.size() == 1) {
        out.t = 0.0f;
        out.nearest = points[0];
        out.distance = lenOf(sub(p, points[0]));
        return out;
    }
    const float total = polylineLength(points);
    float best = std::numeric_limits<float>::max();
    float walked = 0.0f;
    float bestArc = 0.0f;
    Pt bestTan{1.0f, 0.0f};
    for (std::size_t i = 1; i < points.size(); ++i) {
        const Pt a = points[i - 1];
        const Pt b = points[i];
        const Pt ab = sub(b, a);
        const float l2 = dot(ab, ab);
        const float seg = std::sqrt(l2);
        float u = 0.0f;
        if (l2 > 0.0f) {
            u = clampf(dot(sub(p, a), ab) / l2, 0.0f, 1.0f);
        }
        const Pt q{a.x + ab.x * u, a.y + ab.y * u};
        const float d = lenOf(sub(p, q));
        if (d < best) {
            best = d;
            out.nearest = q;
            bestArc = walked + seg * u;
            if (seg > 0.0f) {
                bestTan = Pt{ab.x / seg, ab.y / seg};
            }
        }
        walked += seg;
    }
    out.distance = best;
    out.t = total > 0.0f ? clampf(bestArc / total, 0.0f, 1.0f) : 0.5f;
    const Pt n = aboveNormal(bestTan);
    out.side = dot(sub(p, out.nearest), n);
    return out;
}

namespace {

// coarse = fewer curve samples, for comparing candidate face pairs.
EdgePath buildPath(Route route, const RouteEnd& from, const RouteEnd& to, bool coarse, const std::optional<Pt>& rawVia) {
    std::optional<Pt> via;
    if (rawVia)
        via = sanitize(*rawVia);
    RouteEnd a = from;
    RouteEnd b = to;
    a.anchor.point = sanitize(a.anchor.point);
    b.anchor.point = sanitize(b.anchor.point);
    a.bounds = sanitize(a.bounds);
    b.bounds = sanitize(b.bounds);
    a.anchor.normal = cardinal(a.anchor.normal);
    b.anchor.normal = cardinal(b.anchor.normal);

    EdgePath out;
    const Pt A = a.anchor.point;
    const Pt B = b.anchor.point;
    switch (route) {
    case Route::Curved: {
        const float k = clampf(lenOf(sub(B, A)) * 0.4f, 24.0f, 160.0f);
        const Pt p1 = add(A, mul(a.anchor.normal, k));
        const Pt p2 = add(B, mul(b.anchor.normal, k));
        const float arms = lenOf(sub(p1, A)) + lenOf(sub(p2, p1)) + lenOf(sub(B, p2));
        const int steps = coarse ? 8 : static_cast<int>(clampf(std::ceil(arms / 10.0f), 12.0f, 96.0f));
        // The curve is split in two at its middle (de Casteljau). A manual offset moves that middle point and the two
        // controls beside it, so the curve stays smooth there and is the automatic curve while the offset is zero.
        const Pt q0 = mul(add(A, p1), 0.5f);
        const Pt q1 = mul(add(add(A, mul(p1, 2.0f)), p2), 0.25f);
        const Pt q2 = mul(add(add(p1, mul(p2, 2.0f)), B), 0.25f);
        const Pt q3 = mul(add(p2, B), 0.5f);
        const Pt mid = mul(add(q1, q2), 0.5f);
        const Pt shift = via.value_or(Pt{});
        if (via) {
            const int half = std::max(steps / 2, 4);
            out.points = flattenCubic(A, q0, add(q1, shift), add(mid, shift), half);
            const std::vector<Pt> second = flattenCubic(add(mid, shift), add(q2, shift), q3, B, half);
            out.points.insert(out.points.end(), second.begin() + 1, second.end());
        } else {
            out.points = flattenCubic(A, p1, p2, B, steps);
        }
        out.startTangent = a.anchor.normal;
        out.endTangent = mul(b.anchor.normal, -1.0f);
        out.hasHandle = true;
        out.handleBase = mid;
        out.handle = add(mid, shift);
        break;
    }
    case Route::Elbow: {
        ElbowHandle h;
        out.points = elbowPoints(a, b, via, h);
        out.startTangent = endDirection(out.points, true, a.anchor.normal);
        out.endTangent = endDirection(out.points, false, mul(b.anchor.normal, -1.0f));
        out.hasHandle = h.valid;
        out.handle = h.pos;
        out.handleBase = h.base;
        out.handleAxis = h.axis;
        break;
    }
    case Route::Straight: {
        const Pt mid = mul(add(A, B), 0.5f);
        const Pt w = add(mid, via.value_or(Pt{}));
        if (via)
            out.points = {A, w, B};
        else
            out.points = {A, B};
        out.startTangent = normalized(sub(via ? w : B, A), a.anchor.normal);
        out.endTangent = normalized(sub(B, via ? w : A), out.startTangent);
        out.hasHandle = true;
        out.handleBase = mid;
        out.handle = w;
        break;
    }
    }
    out.length = polylineLength(out.points);
    if (!coarse) {
        out.labelPos = pointAtFraction(out.points, 0.5f);
    }
    return out;
}

} // namespace

EdgePath routePath(Route route, const RouteEnd& from, const RouteEnd& to, const std::optional<Pt>& via) {
    return buildPath(route, from, to, false, via);
}

Pt viaFromPoint(const EdgePath& path, Pt world) {
    const Pt d = sub(sanitize(world), path.handleBase);
    const float axisLen = lenOf(path.handleAxis);
    if (axisLen > kTinyLen) {
        const Pt u = mul(path.handleAxis, 1.0f / axisLen);
        return sanitize(mul(u, dot(d, u)));
    }
    return sanitize(d);
}

namespace {

RouteEnd dockedEnd(const Node& n, Face face) {
    RouteEnd e;
    e.anchor = faceAnchor(n, face);
    e.bounds = sanitize(n.rect);
    e.hasBounds = true;
    return e;
}

} // namespace

std::optional<EdgePath> routeEdge(const Edge& edge, const NodeLookup& lookup) {
    const Node* na = nullptr;
    const Node* nb = nullptr;
    if (edge.from.node != kNoNode) {
        na = lookup ? lookup(edge.from.node) : nullptr;
        if (!na) {
            return std::nullopt;
        }
    }
    if (edge.to.node != kNoNode) {
        nb = lookup ? lookup(edge.to.node) : nullptr;
        if (!nb) {
            return std::nullopt;
        }
    }

    // Candidate faces per end (a free end has one pseudo candidate).
    std::vector<Face> candA;
    std::vector<Face> candB;
    const bool selfLoop = na && nb && edge.from.node == edge.to.node;
    if (selfLoop) {
        Face fa = edge.from.face;
        Face fb = edge.to.face;
        if (fa == Face::Auto && fb == Face::Auto) {
            fa = Face::Right;
            fb = Face::Top;
        } else if (fa == Face::Auto) {
            fa = turnCw(fb);
        } else if (fb == Face::Auto) {
            fb = turnCcw(fa);
        } else if (fb == fa || fb == opposite(fa)) {
            fb = turnCcw(fa);
        }
        candA = {fa};
        candB = {fb};
    } else {
        auto fill = [](std::vector<Face>& c, const Node* n, Face f) {
            if (!n) {
                c = {Face::Auto};
            } else if (f == Face::Auto) {
                c.assign(std::begin(kRealFaces), std::end(kRealFaces));
            } else {
                c = {f};
            }
        };
        fill(candA, na, edge.from.face);
        fill(candB, nb, edge.to.face);
    }

    auto makeEnds = [&](Face fa, Face fb, RouteEnd& ea, RouteEnd& eb) {
        if (na) {
            ea = dockedEnd(*na, fa);
        } else {
            ea = RouteEnd{};
            ea.anchor.point = sanitize(edge.from.point);
        }
        if (nb) {
            eb = dockedEnd(*nb, fb);
        } else {
            eb = RouteEnd{};
            eb.anchor.point = sanitize(edge.to.point);
        }
        if (!na) {
            ea.anchor.normal = cardinalToward(ea.anchor.point, eb.anchor.point);
        }
        if (!nb) {
            eb.anchor.normal = cardinalToward(eb.anchor.point, ea.anchor.point);
        }
    };

    Face bestA = candA.front();
    Face bestB = candB.front();
    if (candA.size() * candB.size() > 1) {
        float bestCost = std::numeric_limits<float>::max();
        for (Face fa : candA) {
            for (Face fb : candB) {
                RouteEnd ea;
                RouteEnd eb;
                makeEnds(fa, fb, ea, eb);
                const float cost = buildPath(edge.route, ea, eb, true, edge.via).length;
                if (cost < bestCost - 1.0e-3f) { // strict, so ties keep the first (stable, no flicker)
                    bestCost = cost;
                    bestA = fa;
                    bestB = fb;
                }
            }
        }
    }

    RouteEnd ea;
    RouteEnd eb;
    makeEnds(bestA, bestB, ea, eb);
    EdgePath path = buildPath(edge.route, ea, eb, false, edge.via);
    path.fromFace = na ? bestA : Face::Auto;
    path.toFace = nb ? bestB : Face::Auto;
    return path;
}

// --- arrowheads --------------------------------------------------------------------------------------
ArrowHead arrowHead(Arrow type, Pt tip, Pt direction, float strokeWidth) {
    ArrowHead h;
    if (type == Arrow::None) {
        return h;
    }
    const Pt t = sanitize(tip);
    const Pt d = normalized(std::isfinite(direction.x) && std::isfinite(direction.y) ? direction : Pt{1.0f, 0.0f}, Pt{1.0f, 0.0f});
    const Pt n{-d.y, d.x};
    const float w = std::isnan(strokeWidth) ? 2.0f : clampf(strokeWidth, 0.5f, 32.0f);
    const float len = clampf(8.0f + 2.5f * w, 10.0f, 48.0f);
    const float half = len * 0.4f;
    switch (type) {
    case Arrow::Arrow: {
        const Pt base = sub(t, mul(d, len));
        h.points = {t, add(base, mul(n, half)), sub(base, mul(n, half))};
        h.filled = true;
        h.closed = true;
        h.inset = len * 0.9f;
        break;
    }
    case Arrow::Open: {
        const Pt base = sub(t, mul(d, len * 0.9f));
        h.points = {add(base, mul(n, half * 1.1f)), t, sub(base, mul(n, half * 1.1f))};
        break;
    }
    case Arrow::Dot: {
        const float r = len * 0.35f;
        const Pt c = sub(t, mul(d, r));
        constexpr int kN = 16;
        for (int i = 0; i < kN; ++i) {
            const float a = 2.0f * kPi * (static_cast<float>(i) / static_cast<float>(kN));
            h.points.push_back(Pt{c.x + r * std::cos(a), c.y + r * std::sin(a)});
        }
        h.filled = true;
        h.closed = true;
        h.inset = 2.0f * r;
        break;
    }
    case Arrow::None: break;
    }
    return h;
}

ArrowHead arrowHeadFor(const EdgePath& path, bool atStart, Arrow type, float strokeWidth) {
    if (path.points.empty()) {
        return ArrowHead{};
    }
    if (atStart) {
        return arrowHead(type, path.points.front(), mul(path.startTangent, -1.0f), strokeWidth);
    }
    return arrowHead(type, path.points.back(), path.endTangent, strokeWidth);
}

// --- hit tests ---------------------------------------------------------------------------------------
bool pointInPolygon(std::span<const Pt> polygon, Pt p) {
    if (polygon.size() < 3) {
        return false;
    }
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Pt a = polygon[i];
        const Pt b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y)) {
            const float xCross = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
            if (p.x < xCross) {
                inside = !inside;
            }
        }
    }
    return inside;
}

bool hitNode(const Node& node, Pt p, float tolerance) {
    const Pt q = sanitize(p);
    const float tol = sanitizeTol(tolerance);
    const Rect r = sanitize(node.rect);
    if (isRectKind(node.kind)) {
        return q.x >= r.x - tol && q.x <= r.right() + tol && q.y >= r.y - tol && q.y <= r.bottom() + tol;
    }
    // Cheap reject before building the outline.
    if (q.x < r.x - tol || q.x > r.right() + tol || q.y < r.y - tol || q.y > r.bottom() + tol) {
        return false;
    }
    const std::vector<Pt> poly = nodeOutline(node);
    if (pointInPolygon(poly, q)) {
        return true;
    }
    if (tol > 0.0f) {
        for (std::size_t i = 0; i < poly.size(); ++i) {
            if (segmentDistance(q, poly[i], poly[(i + 1) % poly.size()]) <= tol) {
                return true;
            }
        }
    }
    return false;
}

bool hitContainerTitle(const Node& node, Pt p) {
    if (node.kind != NodeKind::Container) {
        return false;
    }
    const Rect b = containerTitleBand(node.rect);
    const Pt q = sanitize(p);
    return q.x >= b.x && q.x <= b.right() && q.y >= b.y && q.y <= b.bottom();
}

Pt resizeHandlePos(Rect rect, ResizeHandle handle) {
    const Rect r = sanitize(rect);
    const Pt c = r.center();
    switch (handle) {
    case ResizeHandle::TopLeft: return Pt{r.x, r.y};
    case ResizeHandle::Top: return Pt{c.x, r.y};
    case ResizeHandle::TopRight: return Pt{r.right(), r.y};
    case ResizeHandle::Right: return Pt{r.right(), c.y};
    case ResizeHandle::BottomRight: return Pt{r.right(), r.bottom()};
    case ResizeHandle::Bottom: return Pt{c.x, r.bottom()};
    case ResizeHandle::BottomLeft: return Pt{r.x, r.bottom()};
    case ResizeHandle::Left: return Pt{r.x, c.y};
    case ResizeHandle::None: break;
    }
    return c;
}

ResizeHandle hitResizeHandle(Rect rect, Pt p, float radius) {
    // Corners first so they win ties on tiny rects.
    static constexpr ResizeHandle kOrder[8] = {ResizeHandle::TopLeft,     ResizeHandle::TopRight, ResizeHandle::BottomRight,
                                               ResizeHandle::BottomLeft,  ResizeHandle::Top,      ResizeHandle::Right,
                                               ResizeHandle::Bottom,      ResizeHandle::Left};
    const Pt q = sanitize(p);
    const float rad = sanitizeTol(radius);
    ResizeHandle best = ResizeHandle::None;
    float bestDist = std::numeric_limits<float>::max();
    for (ResizeHandle h : kOrder) {
        const float d = lenOf(sub(q, resizeHandlePos(rect, h)));
        if (d <= rad && d < bestDist) {
            bestDist = d;
            best = h;
        }
    }
    return best;
}

std::optional<Face> hitFaceHandle(const Node& node, Pt p, float radius) {
    const Pt q = sanitize(p);
    const float rad = sanitizeTol(radius);
    std::optional<Face> best;
    float bestDist = std::numeric_limits<float>::max();
    for (Face f : kRealFaces) {
        const float d = lenOf(sub(q, faceAnchor(node, f).point));
        if (d <= rad && d < bestDist) {
            bestDist = d;
            best = f;
        }
    }
    return best;
}

float distanceToPath(std::span<const Pt> points, Pt p) {
    if (points.empty()) {
        return kBigDistance;
    }
    const Pt q = sanitize(p);
    if (points.size() == 1) {
        return lenOf(sub(q, points[0]));
    }
    float best = kBigDistance;
    for (std::size_t i = 1; i < points.size(); ++i) {
        best = std::min(best, segmentDistance(q, points[i - 1], points[i]));
    }
    return best;
}

bool hitEdge(const EdgePath& path, Pt p, float tolerance) {
    return distanceToPath(path.points, p) <= sanitizeTol(tolerance);
}

EdgeEnd hitEdgeEndpoint(const EdgePath& path, Pt p, float radius) {
    if (path.points.empty()) {
        return EdgeEnd::None;
    }
    const Pt q = sanitize(p);
    const float rad = sanitizeTol(radius);
    const float dFrom = lenOf(sub(q, path.points.front()));
    const float dTo = lenOf(sub(q, path.points.back()));
    if (dTo <= rad && dTo <= dFrom) {
        return EdgeEnd::To;
    }
    if (dFrom <= rad) {
        return EdgeEnd::From;
    }
    return EdgeEnd::None;
}

// --- snapping ----------------------------------------------------------------------------------------
float snapToGrid(float v, float grid) {
    const float s = sanitizeCoord(v);
    if (!std::isfinite(grid) || grid < 1.0e-3f) {
        return s;
    }
    const double g = static_cast<double>(grid);
    return sanitizeCoord(static_cast<float>(std::round(static_cast<double>(s) / g) * g));
}

Pt snapToGrid(Pt p, float grid) { return Pt{snapToGrid(p.x, grid), snapToGrid(p.y, grid)}; }

Rect snapRectPosition(Rect r, float grid) {
    r = sanitize(r);
    r.x = snapToGrid(r.x, grid);
    r.y = snapToGrid(r.y, grid);
    return r;
}

Rect snapRectEdges(Rect r, float grid) {
    r = sanitize(r);
    const float x0 = snapToGrid(r.x, grid);
    const float y0 = snapToGrid(r.y, grid);
    const float x1 = snapToGrid(r.right(), grid);
    const float y1 = snapToGrid(r.bottom(), grid);
    return Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

namespace {

void addGuide(std::vector<Guide>& guides, bool vertical, float pos, float from, float to) {
    const float eps = 0.01f + std::abs(pos) * 5.0e-7f;
    for (Guide& g : guides) {
        if (g.vertical == vertical && std::abs(g.pos - pos) <= eps) {
            g.from = std::min(g.from, from);
            g.to = std::max(g.to, to);
            return;
        }
    }
    guides.push_back(Guide{vertical, pos, from, to});
}

} // namespace

SnapResult snapToGuides(Rect moving, std::span<const Rect> others, float threshold) {
    SnapResult res;
    const Rect m = sanitize(moving);
    const float thr = sanitizeTol(threshold);
    const float mx[3] = {m.x, m.x + m.w * 0.5f, m.right()};
    const float my[3] = {m.y, m.y + m.h * 0.5f, m.bottom()};

    float bestX = 0.0f;
    float bestY = 0.0f;
    float bestAbsX = std::numeric_limits<float>::max();
    float bestAbsY = std::numeric_limits<float>::max();
    for (const Rect& raw : others) {
        const Rect o = sanitize(raw);
        const float ox[3] = {o.x, o.x + o.w * 0.5f, o.right()};
        const float oy[3] = {o.y, o.y + o.h * 0.5f, o.bottom()};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                const float dx = ox[j] - mx[i];
                if (std::abs(dx) <= thr && std::abs(dx) < bestAbsX) {
                    bestAbsX = std::abs(dx);
                    bestX = dx;
                }
                const float dy = oy[j] - my[i];
                if (std::abs(dy) <= thr && std::abs(dy) < bestAbsY) {
                    bestAbsY = std::abs(dy);
                    bestY = dy;
                }
            }
        }
    }
    res.snappedX = bestAbsX != std::numeric_limits<float>::max();
    res.snappedY = bestAbsY != std::numeric_limits<float>::max();
    res.delta = Pt{res.snappedX ? bestX : 0.0f, res.snappedY ? bestY : 0.0f};

    // Guides at the snapped position: every alignment that now holds, spanning both rects.
    const Rect s{m.x + res.delta.x, m.y + res.delta.y, m.w, m.h};
    const float sx[3] = {s.x, s.x + s.w * 0.5f, s.right()};
    const float sy[3] = {s.y, s.y + s.h * 0.5f, s.bottom()};
    for (const Rect& raw : others) {
        const Rect o = sanitize(raw);
        const float ox[3] = {o.x, o.x + o.w * 0.5f, o.right()};
        const float oy[3] = {o.y, o.y + o.h * 0.5f, o.bottom()};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                if (res.snappedX && std::abs(ox[j] - sx[i]) <= 0.01f + std::abs(ox[j]) * 5.0e-7f) {
                    addGuide(res.guides, true, ox[j], std::min(s.y, o.y), std::max(s.bottom(), o.bottom()));
                }
                if (res.snappedY && std::abs(oy[j] - sy[i]) <= 0.01f + std::abs(oy[j]) * 5.0e-7f) {
                    addGuide(res.guides, false, oy[j], std::min(s.x, o.x), std::max(s.right(), o.right()));
                }
            }
        }
    }
    return res;
}

} // namespace trace::flow
