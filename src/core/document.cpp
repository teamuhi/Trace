#include "core/document.hpp"

#include <algorithm>
#include <new>
#include <utility>
#include <vector>

namespace trace {

namespace {

const Image kNoImage;
const LayerInfo kNoLayer;
const FrameInfo kNoFrame;

using Rows = std::vector<std::vector<CelId>>; // [frame][layer]

Rows toRows(const Layout& layout) {
    Rows rows(static_cast<std::size_t>(layout.frameCount()));
    const auto stride = static_cast<std::size_t>(layout.layerCount());
    for (std::size_t f = 0; f < rows.size(); ++f)
        rows[f].assign(layout.grid.begin() + static_cast<std::ptrdiff_t>(f * stride),
                       layout.grid.begin() + static_cast<std::ptrdiff_t>((f + 1) * stride));
    return rows;
}

void fromRows(Layout& layout, const Rows& rows) {
    layout.grid.clear();
    layout.grid.reserve(rows.size() * static_cast<std::size_t>(layout.layerCount()));
    for (const auto& row : rows)
        layout.grid.insert(layout.grid.end(), row.begin(), row.end());
}

// Where the active index ends up when the item at `erased` is removed.
int indexAfterErase(int active, int erased, int newCount) noexcept {
    if (active > erased)
        --active;
    else if (active == erased)
        active = std::max(erased - 1, 0);
    return std::clamp(active, 0, std::max(newCount - 1, 0));
}

// Where the active index ends up when the item at `from` is moved to `to`.
int indexAfterMove(int active, int from, int to) noexcept {
    if (active == from)
        return to;
    if (from < active && active <= to)
        return active - 1;
    if (to <= active && active < from)
        return active + 1;
    return active;
}

std::string cleanName(std::string name) {
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7f; }),
               name.end());
    const auto first = name.find_first_not_of(' ');
    if (first == std::string::npos)
        return {};
    name.erase(0, first);
    name.erase(name.find_last_not_of(' ') + 1);
    if (name.size() > kMaxLayerNameBytes) {
        std::size_t n = kMaxLayerNameBytes;
        while (n > 0 && (static_cast<unsigned char>(name[n]) & 0xC0) == 0x80)
            --n; // do not cut a UTF-8 sequence in half
        name.resize(n);
    }
    return name;
}

struct Source {
    const Image* image;
    unsigned opacity;
};

// Blends the sources bottom-up into `out` inside `r`; `out` is overwritten there.
void blendRegion(Image& out, const std::vector<Source>& sources, const IRect& area) {
    const IRect r = intersectionOf(area, IRect{0, 0, out.width(), out.height()});
    for (int y = r.y0; y < r.y1; ++y) {
        const std::span<Rgba8> dst = out.row(y);
        std::fill(dst.begin() + r.x0, dst.begin() + r.x1, Rgba8{});
        for (const Source& s : sources) {
            const std::span<const Rgba8> src = s.image->row(y);
            if (src.size() != dst.size())
                continue;
            for (int x = r.x0; x < r.x1; ++x)
                dst[static_cast<std::size_t>(x)] = blendOver(dst[static_cast<std::size_t>(x)], src[static_cast<std::size_t>(x)], s.opacity);
        }
    }
}

} // namespace

Document::Document(int width, int height, Rgba8 fill) {
    load(Image(width, height, fill));
    generation_ = 0;
}

void Document::reset(int width, int height, Rgba8 fill) {
    load(Image(width, height, fill));
}

void Document::load(Image image) {
    // Everything that can throw happens before anything changes.
    CelStore fresh;
    fresh.layout.layers.push_back(LayerInfo{1, "Layer 1", true, 255, false});
    fresh.layout.frames.push_back(FrameInfo{});
    fresh.layout.grid.push_back(1);
    fresh.pool.emplace(1, std::move(image));
    Image reference = fresh.pool.begin()->second;

    store_ = std::move(fresh);
    committed_ = std::move(reference);
    nextCelId_ = 2;
    nextLayerId_ = 2;
    composite_ = Image();
    compositeDirty_ = DirtyRegion{};
    upload_ = DirtyRegion{};
    edits_ = DirtyRegion{};
    history_.clear();
    selection_.clear();
    guides_.clear();
    ++generation_;
    ++revision_;
    refreshDerived();
    markAllDirty();
}

bool Document::loadStore(CelStore store) {
    Layout& layout = store.layout;
    const int layers = layout.layerCount();
    const int frames = layout.frameCount();
    if (layers < 1 || layers > kMaxLayers || frames < 1 || frames > kMaxFrames)
        return false;
    if (layout.grid.size() != static_cast<std::size_t>(layers) * static_cast<std::size_t>(frames) ||
        store.pool.size() != layout.grid.size())
        return false;
    if (layout.activeLayer < 0 || layout.activeLayer >= layers || layout.activeFrame < 0 || layout.activeFrame >= frames)
        return false;

    // Every grid slot names a different, existing, non-empty cel, and all cels share one size.
    std::vector<CelId> ids = layout.grid;
    std::sort(ids.begin(), ids.end());
    if (ids.front() == kNoCel || std::adjacent_find(ids.begin(), ids.end()) != ids.end())
        return false;
    int width = 0;
    int height = 0;
    for (const CelId id : ids) {
        const auto it = store.pool.find(id);
        if (it == store.pool.end() || it->second.empty())
            return false;
        if (width == 0) {
            width = it->second.width();
            height = it->second.height();
        } else if (it->second.width() != width || it->second.height() != height) {
            return false;
        }
    }

    std::uint32_t maxLayerId = 0;
    std::vector<std::uint32_t> layerIds;
    for (LayerInfo& info : layout.layers) {
        if (info.id == 0)
            return false;
        layerIds.push_back(info.id);
        maxLayerId = std::max(maxLayerId, info.id);
        info.name = cleanName(std::move(info.name));
        if (info.name.empty())
            info.name = "Layer " + std::to_string(info.id);
    }
    std::sort(layerIds.begin(), layerIds.end());
    if (std::adjacent_find(layerIds.begin(), layerIds.end()) != layerIds.end())
        return false;
    for (FrameInfo& frame : layout.frames)
        frame.durationMs = std::clamp(frame.durationMs, kMinFrameMs, kMaxFrameMs);
    if (ids.back() == UINT32_MAX || maxLayerId == UINT32_MAX)
        return false;

    Image reference;
    try {
        reference = store.pool.at(layout.activeCel()); // the only allocation; nothing has changed yet
    } catch (const std::bad_alloc&) {
        return false;
    }

    store_ = std::move(store);
    committed_ = std::move(reference);
    nextCelId_ = ids.back() + 1;
    nextLayerId_ = maxLayerId + 1;
    composite_ = Image();
    compositeDirty_ = DirtyRegion{};
    upload_ = DirtyRegion{};
    edits_ = DirtyRegion{};
    history_.clear();
    selection_.clear();
    guides_.clear();
    ++generation_;
    refreshDerived();
    markAllDirty();
    invalidateDisplay();
    return true;
}

void Document::refreshDerived() noexcept {
    active_ = store_.activeImage();
    activeCel_ = store_.layout.activeCel();

    displayFast_ = nullptr;
    int visible = 0;
    int shown = -1;
    for (int i = 0; i < store_.layout.layerCount(); ++i) {
        if (store_.layout.layers[static_cast<std::size_t>(i)].visible) {
            ++visible;
            shown = i;
        }
    }
    if (visible == 1 && store_.layout.layers[static_cast<std::size_t>(shown)].opacity == 255) {
        const auto it = store_.pool.find(store_.layout.cel(shown, store_.layout.activeFrame));
        if (it != store_.pool.end())
            displayFast_ = &it->second;
    }
    if (displayFast_) { // no copy is needed any more
        composite_ = Image();
        compositeDirty_ = DirtyRegion{};
    }
}

void Document::invalidateDisplay() noexcept {
    ++revision_;
    upload_.add(bounds());
    if (!displayFast_)
        compositeDirty_.add(bounds());
}

void Document::touch(const IRect& r) noexcept {
    ++revision_;
    upload_.add(r);
    if (!displayFast_)
        compositeDirty_.add(r);
}

bool Document::flushComposite() {
    if (displayFast_)
        return true;
    const int w = active_->width();
    const int h = active_->height();
    try {
        if (composite_.width() != w || composite_.height() != h) {
            composite_ = Image(w, h);
            compositeDirty_.add(IRect{0, 0, w, h});
        }
        const IRect r = compositeDirty_.take(IRect{0, 0, w, h});
        if (r.empty())
            return true;

        std::vector<Source> sources;
        sources.reserve(store_.layout.layers.size());
        for (int i = 0; i < store_.layout.layerCount(); ++i) {
            const LayerInfo& info = store_.layout.layers[static_cast<std::size_t>(i)];
            const auto it = store_.pool.find(store_.layout.cel(i, store_.layout.activeFrame));
            if (info.visible && info.opacity > 0 && it != store_.pool.end())
                sources.push_back({&it->second, info.opacity});
        }
        blendRegion(composite_, sources, r);
        return true;
    } catch (const std::bad_alloc&) {
        composite_ = Image();
        compositeDirty_.add(IRect{0, 0, w, h}); // try again next time
        return false;
    }
}

const Image& Document::displayImage() {
    if (displayFast_)
        return *displayFast_;
    return flushComposite() ? composite_ : *active_; // out of memory: show the active cel rather than nothing
}

Image Document::compositeOfFrame(int frame) const {
    if (frame < 0 || frame >= frameCount())
        return {};
    const Image& base = cel(0, frame);
    Image out(base.width(), base.height());
    std::vector<Source> sources;
    for (int i = 0; i < layerCount(); ++i) {
        const LayerInfo& info = store_.layout.layers[static_cast<std::size_t>(i)];
        if (info.visible && info.opacity > 0)
            sources.push_back({&cel(i, frame), info.opacity});
    }
    blendRegion(out, sources, IRect{0, 0, out.width(), out.height()});
    return out;
}

const LayerInfo& Document::layer(int index) const noexcept {
    return index >= 0 && index < layerCount() ? store_.layout.layers[static_cast<std::size_t>(index)] : kNoLayer;
}

const FrameInfo& Document::frame(int index) const noexcept {
    return index >= 0 && index < frameCount() ? store_.layout.frames[static_cast<std::size_t>(index)] : kNoFrame;
}

const Image& Document::cel(int layer, int frame) const noexcept {
    const auto it = store_.pool.find(store_.layout.cel(layer, frame));
    return it == store_.pool.end() ? kNoImage : it->second;
}

bool Document::activeEditable() const noexcept {
    const LayerInfo& info = layer(activeLayer());
    return info.visible && !info.locked;
}

void Document::markDirty(const IRect& r) noexcept {
    touch(r);
    edits_.add(r);
}

void Document::markAllDirty() noexcept {
    upload_.add(bounds());
}

IRect Document::takeDirty() noexcept {
    flushComposite();
    return upload_.take(bounds());
}

bool Document::commit() {
    const IRect region = intersectionOf(edits_.bounds(), bounds());
    if (region.empty()) {
        edits_ = DirtyRegion{};
        return false;
    }
    try {
        const bool recorded = history_.commit(*active_, committed_, region, activeCel_);
        edits_ = DirtyRegion{};
        return recorded;
    } catch (const std::bad_alloc&) {
        return false; // out of memory: the edits stay pending and join the next step
    }
}

bool Document::activateCel(CelId id) {
    const auto [layer, frame] = store_.layout.find(id);
    return layer >= 0 && setActive(layer, frame) == OpResult::Applied;
}

bool Document::stepHistory(bool undoing) {
    commit();
    selection_.clear();
    const std::optional<History::StepInfo> next = undoing ? history_.peekUndo() : history_.peekRedo();
    if (!next)
        return false;
    // A step on another cel: go there first, as the step expects its pixels under the tools.
    if (next->kind != History::StepKind::Structure && next->cel != kNoCel && next->cel != activeCel_ && !activateCel(next->cel))
        return false;

    const CelId before = activeCel_;
    try {
        const IRect changed = undoing ? history_.undo(store_, committed_) : history_.redo(store_, committed_);
        if (changed.empty())
            return false;
        refreshDerived();
        if (next->kind == History::StepKind::Structure || activeCel_ != before) {
            if (activeCel_ != before)
                ++generation_;
            invalidateDisplay();
        } else {
            touch(changed);
        }
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool Document::undo() {
    return stepHistory(true);
}

bool Document::redo() {
    return stepHistory(false);
}

bool Document::beginOp() {
    if (selection_.floating())
        clearSelection();
    else
        commit();
    return edits_.empty();
}

Document::OpResult Document::applyLayout(Layout next, CelPool added, CelId nextCelId, std::uint32_t nextLayerId) {
    const CelId before = activeCel_;
    try {
        if (!history_.commitStructure(store_, committed_, std::move(next), std::move(added)))
            return OpResult::Nothing;
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
    nextCelId_ = nextCelId;
    nextLayerId_ = nextLayerId;
    refreshDerived();
    if (activeCel_ != before)
        ++generation_; // a gesture that began on the old cel must not keep drawing
    invalidateDisplay();
    return OpResult::Applied;
}

Document::OpResult Document::replaceAll(const std::function<Image(const Image&)>& make) {
    if (image().empty())
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory; // the pending edits could not be recorded, and would be lost
    try {
        Layout next = store_.layout;
        CelPool added;
        CelId id = nextCelId_;
        for (CelId& slot : next.grid) {
            Image out = make(store_.pool.at(slot));
            if (out.empty())
                return OpResult::Nothing;
            added.emplace(id, std::move(out));
            slot = id++;
        }
        return applyLayout(std::move(next), std::move(added), id, nextLayerId_);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

Document::OpResult Document::resizeImage(int width, int height, ResizeFilter filter) {
    const int oldW = image().width();
    const int oldH = image().height();
    if (image().empty() || (width == oldW && height == oldH))
        return OpResult::Nothing;
    clearSelection();
    const OpResult result = replaceAll([&](const Image& src) { return resizedImage(src, width, height, filter); });
    if (result == OpResult::Applied)
        guides_.scale(oldW, oldH, width, height);
    return result;
}

Document::OpResult Document::resizeCanvas(int width, int height, Anchor anchor, Rgba8 fill) {
    const int oldW = image().width();
    const int oldH = image().height();
    if (image().empty() || (width == oldW && height == oldH))
        return OpResult::Nothing;
    clearSelection();
    const OpResult result = replaceAll([&](const Image& src) { return canvasResized(src, width, height, anchor, fill); });
    if (result == OpResult::Applied) {
        const auto [dx, dy] = anchorOffset(anchor, oldW, oldH, width, height);
        guides_.shift(dx, dy, width, height);
    }
    return result;
}

Document::OpResult Document::cropToSelection() {
    const IRect rect = intersectionOf(selection_.rect(), bounds());
    if (!selection_.active() || rect.empty())
        return OpResult::Nothing;
    clearSelection(); // floating pixels are committed into the image first, so the crop includes them
    const OpResult result = replaceAll([&](const Image& src) { return cropped(src, rect); });
    if (result == OpResult::Applied)
        guides_.shift(-rect.x0, -rect.y0, rect.width(), rect.height());
    return result;
}

// --- layers and frames -----------------------------------------------------------------------------

Document::OpResult Document::setActive(int layer, int frame) {
    Layout& layout = store_.layout;
    if (!layout.valid(layer, frame) || (layer == layout.activeLayer && frame == layout.activeFrame))
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    const auto target = store_.pool.find(layout.cel(layer, frame));
    if (target == store_.pool.end())
        return OpResult::Nothing;

    Image reference;
    try {
        reference = target->second; // the baseline for the new cel; the only step that can throw
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
    committed_ = std::move(reference);
    layout.activeLayer = layer;
    layout.activeFrame = frame;
    refreshDerived();
    ++generation_;
    invalidateDisplay();
    return OpResult::Applied;
}

Document::OpResult Document::addLayer(std::string name) {
    const Layout& cur = store_.layout;
    if (image().empty() || cur.layerCount() >= kMaxLayers || nextLayerId_ == UINT32_MAX)
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    try {
        Layout next = cur;
        Rows rows = toRows(cur);
        CelPool added;
        CelId id = nextCelId_;
        const int at = cur.activeLayer + 1;

        LayerInfo info;
        info.id = nextLayerId_;
        info.name = cleanName(std::move(name));
        if (info.name.empty())
            info.name = "Layer " + std::to_string(info.id);
        next.layers.insert(next.layers.begin() + at, std::move(info));
        for (auto& row : rows) {
            added.emplace(id, Image(image().width(), image().height()));
            row.insert(row.begin() + at, id++);
        }
        next.activeLayer = at;
        fromRows(next, rows);
        return applyLayout(std::move(next), std::move(added), id, nextLayerId_ + 1);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

Document::OpResult Document::deleteLayer(int layer) {
    const Layout& cur = store_.layout;
    if (image().empty() || cur.layerCount() <= 1 || layer < 0 || layer >= cur.layerCount())
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    try {
        Layout next = cur;
        Rows rows = toRows(cur);
        next.layers.erase(next.layers.begin() + layer);
        for (auto& row : rows)
            row.erase(row.begin() + layer);
        next.activeLayer = indexAfterErase(cur.activeLayer, layer, next.layerCount());
        fromRows(next, rows);
        return applyLayout(std::move(next), {}, nextCelId_, nextLayerId_);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

Document::OpResult Document::duplicateLayer(int layer, std::string name) {
    const Layout& cur = store_.layout;
    if (image().empty() || cur.layerCount() >= kMaxLayers || layer < 0 || layer >= cur.layerCount() || nextLayerId_ == UINT32_MAX)
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    try {
        Layout next = cur;
        Rows rows = toRows(cur);
        CelPool added;
        CelId id = nextCelId_;

        LayerInfo info = cur.layers[static_cast<std::size_t>(layer)];
        info.id = nextLayerId_;
        info.name = cleanName(name.empty() ? info.name + " copy" : std::move(name));
        if (info.name.empty())
            info.name = "Layer " + std::to_string(info.id);
        next.layers.insert(next.layers.begin() + layer + 1, std::move(info));
        for (std::size_t f = 0; f < rows.size(); ++f) {
            added.emplace(id, cel(layer, static_cast<int>(f)));
            rows[f].insert(rows[f].begin() + layer + 1, id++);
        }
        next.activeLayer = layer + 1;
        fromRows(next, rows);
        return applyLayout(std::move(next), std::move(added), id, nextLayerId_ + 1);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

Document::OpResult Document::moveLayer(int from, int to) {
    const Layout& cur = store_.layout;
    const int count = cur.layerCount();
    if (image().empty() || from < 0 || from >= count || to < 0 || to >= count || from == to)
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    try {
        Layout next = cur;
        Rows rows = toRows(cur);
        const auto move = [&](auto& v) {
            auto item = std::move(v[static_cast<std::size_t>(from)]);
            v.erase(v.begin() + from);
            v.insert(v.begin() + to, std::move(item));
        };
        move(next.layers);
        for (auto& row : rows)
            move(row);
        next.activeLayer = indexAfterMove(cur.activeLayer, from, to);
        fromRows(next, rows);
        return applyLayout(std::move(next), {}, nextCelId_, nextLayerId_);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

Document::OpResult Document::mergeDown(int layer) {
    const Layout& cur = store_.layout;
    if (image().empty() || layer < 1 || layer >= cur.layerCount())
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    try {
        Layout next = cur;
        Rows rows = toRows(cur);
        CelPool added;
        CelId id = nextCelId_;
        const LayerInfo& upper = cur.layers[static_cast<std::size_t>(layer)];
        const bool contributes = upper.visible && upper.opacity > 0;

        for (std::size_t f = 0; f < rows.size(); ++f) {
            if (contributes) {
                Image merged = cel(layer - 1, static_cast<int>(f));
                const Image& top = cel(layer, static_cast<int>(f));
                for (int y = 0; y < merged.height(); ++y) {
                    const std::span<Rgba8> dst = merged.row(y);
                    const std::span<const Rgba8> src = top.row(y);
                    for (std::size_t x = 0; x < dst.size(); ++x)
                        dst[x] = blendOver(dst[x], src[x], upper.opacity);
                }
                added.emplace(id, std::move(merged));
                rows[f][static_cast<std::size_t>(layer) - 1] = id++;
            } // else the lower cel stays exactly as it is
            rows[f].erase(rows[f].begin() + layer);
        }
        next.layers.erase(next.layers.begin() + layer);
        next.activeLayer = cur.activeLayer == layer ? layer - 1 : cur.activeLayer > layer ? cur.activeLayer - 1 : cur.activeLayer;
        fromRows(next, rows);
        return applyLayout(std::move(next), std::move(added), id, nextLayerId_);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

Document::OpResult Document::flatten() {
    const Layout& cur = store_.layout;
    if (image().empty() || cur.layerCount() <= 1)
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    try {
        Layout next = cur;
        CelPool added;
        CelId id = nextCelId_;
        Rows rows(static_cast<std::size_t>(cur.frameCount()));
        for (std::size_t f = 0; f < rows.size(); ++f) {
            added.emplace(id, compositeOfFrame(static_cast<int>(f)));
            rows[f] = {id++};
        }
        LayerInfo base = cur.layers.front();
        base.visible = true;
        base.opacity = 255;
        base.locked = false;
        next.layers.assign(1, std::move(base));
        next.activeLayer = 0;
        fromRows(next, rows);
        return applyLayout(std::move(next), std::move(added), id, nextLayerId_);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

Document::OpResult Document::setLayerProps(int layer, const LayerInfo& props) {
    const Layout& cur = store_.layout;
    if (image().empty() || layer < 0 || layer >= cur.layerCount())
        return OpResult::Nothing;
    LayerInfo updated = cur.layers[static_cast<std::size_t>(layer)];
    std::string name = cleanName(props.name);
    if (!name.empty())
        updated.name = std::move(name);
    updated.visible = props.visible;
    updated.opacity = props.opacity;
    updated.locked = props.locked;
    if (updated == cur.layers[static_cast<std::size_t>(layer)])
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    try {
        Layout next = cur;
        next.layers[static_cast<std::size_t>(layer)] = std::move(updated);
        return applyLayout(std::move(next), {}, nextCelId_, nextLayerId_);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

Document::OpResult Document::addFrame(FrameFill fill) {
    const Layout& cur = store_.layout;
    if (image().empty() || cur.frameCount() >= kMaxFrames)
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    try {
        Layout next = cur;
        Rows rows = toRows(cur);
        CelPool added;
        CelId id = nextCelId_;
        const int at = cur.activeFrame + 1;

        std::vector<CelId> row;
        for (int l = 0; l < cur.layerCount(); ++l) {
            added.emplace(id, fill == FrameFill::Duplicate ? cel(l, cur.activeFrame) : Image(image().width(), image().height()));
            row.push_back(id++);
        }
        rows.insert(rows.begin() + at, std::move(row));
        next.frames.insert(next.frames.begin() + at, cur.frames[static_cast<std::size_t>(cur.activeFrame)]);
        next.activeFrame = at;
        fromRows(next, rows);
        return applyLayout(std::move(next), std::move(added), id, nextLayerId_);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

Document::OpResult Document::deleteFrame(int frame) {
    const Layout& cur = store_.layout;
    if (image().empty() || cur.frameCount() <= 1 || frame < 0 || frame >= cur.frameCount())
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    try {
        Layout next = cur;
        Rows rows = toRows(cur);
        rows.erase(rows.begin() + frame);
        next.frames.erase(next.frames.begin() + frame);
        next.activeFrame = indexAfterErase(cur.activeFrame, frame, next.frameCount());
        fromRows(next, rows);
        return applyLayout(std::move(next), {}, nextCelId_, nextLayerId_);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

Document::OpResult Document::moveFrame(int from, int to) {
    const Layout& cur = store_.layout;
    const int count = cur.frameCount();
    if (image().empty() || from < 0 || from >= count || to < 0 || to >= count || from == to)
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    try {
        Layout next = cur;
        Rows rows = toRows(cur);
        const auto move = [&](auto& v) {
            auto item = std::move(v[static_cast<std::size_t>(from)]);
            v.erase(v.begin() + from);
            v.insert(v.begin() + to, std::move(item));
        };
        move(next.frames);
        move(rows);
        next.activeFrame = indexAfterMove(cur.activeFrame, from, to);
        fromRows(next, rows);
        return applyLayout(std::move(next), {}, nextCelId_, nextLayerId_);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

Document::OpResult Document::setFrameDuration(int frame, int durationMs) {
    const Layout& cur = store_.layout;
    if (image().empty() || frame < 0 || frame >= cur.frameCount())
        return OpResult::Nothing;
    const int clamped = std::clamp(durationMs, kMinFrameMs, kMaxFrameMs);
    if (cur.frames[static_cast<std::size_t>(frame)].durationMs == clamped)
        return OpResult::Nothing;
    if (!beginOp())
        return OpResult::NoMemory;
    try {
        Layout next = cur;
        next.frames[static_cast<std::size_t>(frame)].durationMs = clamped;
        return applyLayout(std::move(next), {}, nextCelId_, nextLayerId_);
    } catch (const std::bad_alloc&) {
        return OpResult::NoMemory;
    }
}

// --- selection ---------------------------------------------------------------------------------------

void Document::selectRect(const IRect& r) {
    if (selection_.floating())
        clearSelection();
    selection_.setRect(intersectionOf(r, bounds()));
}

void Document::selectAll() {
    clearSelection();
    selection_.setRect(bounds());
}

void Document::clearSelection() {
    commit(); // the floating pixels are already in the image; record them before forgetting the bookkeeping
    selection_.clear();
}

bool Document::liftSelection(Rgba8 clearColor) {
    try {
        markDirty(selection_.lift(image(), clearColor));
        return selection_.floating();
    } catch (const std::bad_alloc&) {
        return false;
    }
}

void Document::moveSelectionTo(int x, int y) {
    markDirty(selection_.moveTo(image(), x, y));
}

void Document::deleteSelection(Rgba8 clearColor) {
    markDirty(selection_.erase(image(), clearColor));
}

bool Document::paste(Image pixels, int x, int y) {
    commit();
    try {
        markDirty(selection_.paste(image(), std::move(pixels), x, y));
        return selection_.floating();
    } catch (const std::bad_alloc&) {
        return false;
    }
}

void Document::fillTestPattern() {
    Image& img = image();
    const int w = img.width();
    const int h = img.height();
    if (w == 0 || h == 0)
        return;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Rgba8 c{static_cast<std::uint8_t>(x * 255 / std::max(w - 1, 1)),
                    static_cast<std::uint8_t>(y * 255 / std::max(h - 1, 1)), 160, 255};
            if (x % 64 == 0 || y % 64 == 0)
                c = Rgba8{20, 20, 20, 255};
            else if (x % 8 == 0 && y % 8 == 0)
                c = Rgba8{255, 255, 255, 255};
            img.set(x, y, c);
        }
    }

    // Edge markers: any off-by-one at the borders shows up immediately when zoomed in.
    const Rgba8 red{230, 30, 30, 255};
    for (int x = 0; x < w; ++x) {
        img.set(x, 0, red);
        img.set(x, h - 1, red);
    }
    for (int y = 0; y < h; ++y) {
        img.set(0, y, red);
        img.set(w - 1, y, red);
    }

    // Transparent window so the checkerboard shows through.
    const int side = std::min({128, w / 4, h / 4});
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            img.set(w - 1 - 16 - x, 16 + y, Rgba8{});

    markDirty(bounds());
}

} // namespace trace
