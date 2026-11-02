#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Layout rules of the Paint and Pixel Art Tools panel that need no ImGui, so they can be tested: how many icon
// columns fit, whether the panel is a wide bar or a narrow column, and what size a panel gets when it is docked
// somewhere new.

namespace trace::toolslayout {

// Columns of an icon grid that fills `avail` pixels with cells `cell` wide and `gap` apart, never more than `count`
// and never fewer than one (a panel narrower than a single cell still shows its icons, one per row).
inline int gridColumns(float avail, float cell, float gap, int count) noexcept {
    if (count <= 0)
        return 1;
    if (!(cell > 0.0f) || !std::isfinite(avail))
        return 1;
    const float perCell = cell + std::max(0.0f, gap);
    const int fit = static_cast<int>(std::floor((std::max(0.0f, avail) + std::max(0.0f, gap)) / perCell));
    return std::clamp(fit, 1, count);
}

// True when the panel is a wide, short band (docked along the top or bottom, or a floating window pulled wide) and
// lays its sections side by side. A column, or anything not clearly wider than tall, stacks them. `em` is the font
// size, so a bar must also be wide enough to hold more than one section.
inline bool barShape(float width, float height, float em) noexcept {
    return width > height * 1.5f && width > em * 28.0f;
}

// Where a docked panel sits: in a column along the left or right (the split is side by side), in a bar along the top
// or bottom (the split is one above the other and the node spans wider than it is tall), or somewhere else
// (stacked in a narrow column, tabbed with another window), which the dock logic leaves alone.
enum class DockKind { Side, Bar, Other };

inline DockKind classifyDock(bool splitsVertically, float width, float height) noexcept {
    if (!splitsVertically)
        return DockKind::Side;
    return width > height ? DockKind::Bar : DockKind::Other;
}

// What the panel looks at each frame while docked: the node it is in, where that node sits, and its size along the
// split (the width of a Side node, the height of a Bar one).
struct DockObservation {
    bool docked = false; // false while the window floats, is tabbed with another window or has no split to resize
    std::uint32_t node = 0;
    DockKind kind = DockKind::Other;
    float size = 0.0f;
};

struct DockCommand {
    bool apply = false;
    float size = 0.0f; // the size along the split the node should take
};

// Makes a panel keep its size when it is docked somewhere else. ImGui gives a newly docked window half of the space,
// which for a panel dropped on the top or bottom is a huge bar. The tracker remembers the size the panel had in
// each kind of place and asks for it back when the panel lands in a new node or a new kind of place; the first time
// it lands somewhere with no remembered size it asks for a default width (Side) or, for a Bar, the height its
// content needs (`barFit`, measured by the caller; asked for over the first few frames, as the content reflows to
// the new width). The very first sighting (a layout restored from disk) is adopted untouched.
class DockSizeTracker {
public:
    static constexpr int kFitFrames = 4;

    DockCommand update(const DockObservation& obs, float defaultSide, float barFit) noexcept {
        if (!obs.docked || obs.kind == DockKind::Other) {
            node_ = 0;
            frames_ = 0;
            fitting_ = false;
            return {};
        }
        const std::size_t slot = obs.kind == DockKind::Side ? 0 : 1;
        if (obs.node != node_ || obs.kind != kind_) {
            const bool restored = !seen_; // a layout loaded from disk is the user's own: leave it
            seen_ = true;
            node_ = obs.node;
            kind_ = obs.kind;
            frames_ = 1;
            fitting_ = false;
            if (restored)
                return {};
            float target = saved_[slot];
            if (!(target > 0.0f)) {
                target = obs.kind == DockKind::Side ? defaultSide : 0.0f;
                fitting_ = obs.kind == DockKind::Bar;
            }
            return target > 0.0f ? DockCommand{true, target} : DockCommand{};
        }

        ++frames_;
        if (fitting_) {
            if (barFit > 0.0f && std::fabs(barFit - obs.size) > 1.0f && frames_ <= kFitFrames + 1)
                return DockCommand{true, barFit};
            if (frames_ > kFitFrames)
                fitting_ = false;
            return {};
        }
        // Settled in this node: whatever size it has now (the user may be dragging the splitter) is the one to keep.
        if (frames_ > kFitFrames && obs.size > 0.0f)
            saved_[slot] = obs.size;
        return {};
    }

    // The remembered size for a kind of place, 0 when none.
    float saved(DockKind kind) const noexcept {
        return kind == DockKind::Side ? saved_[0] : kind == DockKind::Bar ? saved_[1] : 0.0f;
    }

private:
    bool seen_ = false;
    std::uint32_t node_ = 0;
    DockKind kind_ = DockKind::Other;
    int frames_ = 0;
    bool fitting_ = false;
    float saved_[2] = {0.0f, 0.0f};
};

} // namespace trace::toolslayout
