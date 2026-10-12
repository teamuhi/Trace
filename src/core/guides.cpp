#include "core/guides.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace trace {

namespace {

bool usable(float v) noexcept {
    return std::isfinite(v);
}

// Keeps `list` inside [0, limit] and free of duplicates after its values were rewritten.
void normalize(std::vector<int>& list, int limit) {
    list.erase(std::remove_if(list.begin(), list.end(), [limit](int p) { return p < 0 || p > limit; }), list.end());
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
}

int clampedAdd(int a, int b) noexcept {
    constexpr std::int64_t limit = std::numeric_limits<int>::max() / 2;
    return static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(a) + b, -limit, limit));
}

} // namespace

bool Guides::add(GuideAxis axis, int position) {
    std::vector<int>& l = list(axis);
    const auto it = std::lower_bound(l.begin(), l.end(), position);
    if ((it != l.end() && *it == position) || l.size() >= kMaxPerAxis)
        return false;
    l.insert(it, position);
    return true;
}

bool Guides::remove(GuideAxis axis, int position) {
    std::vector<int>& l = list(axis);
    const auto it = std::lower_bound(l.begin(), l.end(), position);
    if (it == l.end() || *it != position)
        return false;
    l.erase(it);
    return true;
}

bool Guides::move(GuideAxis axis, int from, int to) {
    if (!remove(axis, from))
        return false;
    add(axis, to); // already taken: the two guides merge
    return true;
}

void Guides::clear() noexcept {
    vertical_.clear();
    horizontal_.clear();
}

std::optional<int> Guides::nearest(GuideAxis axis, float position, float radius) const noexcept {
    if (!usable(position) || !(radius >= 0.0f))
        return std::nullopt;
    std::optional<int> best;
    float bestDistance = radius;
    for (const int g : positions(axis)) {
        const float d = std::fabs(static_cast<float>(g) - position);
        if (d <= bestDistance) {
            best = g;
            bestDistance = d;
        }
    }
    return best;
}

float Guides::snapStart(GuideAxis axis, float value, float radius) const noexcept {
    const std::optional<int> g = nearest(axis, value, radius);
    return g ? static_cast<float>(*g) : value;
}

float Guides::snapEnd(GuideAxis axis, float value, float anchor, float radius) const noexcept {
    const std::optional<int> g = nearest(axis, value, radius);
    if (!g || !usable(anchor))
        return value;
    int pixel = *g; // dragging back past the anchor: the box edge is the guide's own side
    if (value >= anchor) {
        // The box grows to the guide, so its last pixel is the one before it, but never before the anchor pixel.
        pixel = std::max(*g - 1, static_cast<int>(std::floor(anchor)));
    }
    return static_cast<float>(pixel) + 0.5f;
}

int Guides::snapSpan(GuideAxis axis, int start, int length, float radius) const noexcept {
    int bestDelta = 0;
    float bestDistance = radius;
    bool found = false;
    for (const int g : positions(axis)) {
        for (const int edge : {start, clampedAdd(start, length)}) {
            const int delta = g - edge;
            const float d = std::fabs(static_cast<float>(delta));
            if (d <= bestDistance) {
                bestDistance = d;
                bestDelta = delta;
                found = true;
            }
        }
    }
    return found ? clampedAdd(start, bestDelta) : start;
}

void Guides::shift(int dx, int dy, int width, int height) {
    for (int& p : vertical_)
        p = clampedAdd(p, dx);
    for (int& p : horizontal_)
        p = clampedAdd(p, dy);
    normalize(vertical_, width);
    normalize(horizontal_, height);
}

void Guides::scale(int oldWidth, int oldHeight, int newWidth, int newHeight) {
    const auto rescale = [](std::vector<int>& l, int from, int to) {
        if (from <= 0)
            return;
        for (int& p : l)
            p = static_cast<int>(std::llround(static_cast<double>(p) * static_cast<double>(to) / static_cast<double>(from)));
    };
    rescale(vertical_, oldWidth, newWidth);
    rescale(horizontal_, oldHeight, newHeight);
    normalize(vertical_, newWidth);
    normalize(horizontal_, newHeight);
}

} // namespace trace
