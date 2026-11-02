#include "flow/flow_layout.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace trace::flow {

namespace {

// One thing the user sees as a single block: the selected nodes that move together. `roots` are what is handed to
// moveNodes (it brings children and group members along); `bounds` is the union of the unit's own nodes (the roots
// and their group mates). Children of a container are not part of the bounds: a container aligns by its own frame.
struct Unit {
    std::vector<NodeId> roots;
    Rect bounds;
};

// Positions below this are treated as equal, so aligning twice is a no-op even after float rounding at big coordinates.
float tolerance(float v) noexcept { return 1e-4f + 4.8e-7f * std::fabs(v); }

// Distinct ids of `ids` that exist, in first-seen order.
std::vector<NodeId> existingUnique(const FlowDocument& doc, std::span<const NodeId> ids) {
    std::vector<NodeId> out;
    std::unordered_set<NodeId> seen;
    for (const NodeId id : ids) {
        if (doc.node(id) && seen.insert(id).second) out.push_back(id);
    }
    return out;
}

struct Dsu {
    std::vector<std::size_t> parent;
    explicit Dsu(std::size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), std::size_t{0}); }
    std::size_t find(std::size_t a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    }
    void unite(std::size_t a, std::size_t b) { parent[find(a)] = find(b); }
};

Rect unionRect(const Rect& a, const Rect& b) {
    const float x0 = std::min(a.x, b.x);
    const float y0 = std::min(a.y, b.y);
    const float x1 = std::max(a.right(), b.right());
    const float y1 = std::max(a.bottom(), b.bottom());
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

constexpr std::size_t kNoUnit = static_cast<std::size_t>(-1);

// Builds the units of a selection (see flow_layout.hpp). Units whose move closures overlap (a selected container
// whose child is in a group with another selected node, say) are merged so nothing is ever moved twice.
// `nodeUnit`, when given, receives for every index of doc.nodes() the unit that moves it (kNoUnit when none).
std::vector<Unit> buildUnits(const FlowDocument& doc, std::span<const NodeId> ids,
                             std::vector<std::size_t>* nodeUnit = nullptr) {
    const std::vector<NodeId> sel = existingUnique(doc, ids);
    if (sel.empty()) return {};
    const std::unordered_set<NodeId> selSet(sel.begin(), sel.end());

    const auto& nodes = doc.nodes();
    std::unordered_map<NodeId, std::size_t> index;
    std::unordered_map<NodeId, std::vector<std::size_t>> byParent;
    std::unordered_map<GroupId, std::vector<std::size_t>> byGroup;
    index.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        index[nodes[i].id] = i;
        if (nodes[i].parentId != kNoNode) byParent[nodes[i].parentId].push_back(i);
        if (nodes[i].groupId != kNoGroup) byGroup[nodes[i].groupId].push_back(i);
    }

    // A node under a selected container follows that container.
    const auto hasSelectedAncestor = [&](const Node& n) {
        NodeId p = n.parentId;
        for (std::size_t guard = 0; p != kNoNode && guard <= nodes.size(); ++guard) {
            if (selSet.count(p)) return true;
            const auto it = index.find(p);
            if (it == index.end()) break;
            p = nodes[it->second].parentId;
        }
        return false;
    };

    // Initial units: one per group, one per remaining free node.
    std::vector<std::vector<NodeId>> unitRoots;
    std::unordered_map<GroupId, std::size_t> groupUnit;
    for (const NodeId id : sel) {
        const Node& n = nodes[index[id]];
        if (hasSelectedAncestor(n)) continue;
        if (n.groupId != kNoGroup) {
            const auto [it, fresh] = groupUnit.try_emplace(n.groupId, unitRoots.size());
            if (fresh) unitRoots.emplace_back();
            unitRoots[it->second].push_back(id);
        } else {
            unitRoots.push_back({id});
        }
    }
    if (unitRoots.empty()) return {};

    // Claim each unit's move closure; a node claimed twice merges the two units.
    Dsu dsu(unitRoots.size());
    std::vector<std::ptrdiff_t> claimed(nodes.size(), -1);
    std::vector<std::size_t> work;
    for (std::size_t u = 0; u < unitRoots.size(); ++u) {
        work.clear();
        const auto claim = [&](std::size_t i) {
            if (claimed[i] >= 0) {
                dsu.unite(u, static_cast<std::size_t>(claimed[i])); // already traversed by that unit
            } else {
                claimed[i] = static_cast<std::ptrdiff_t>(u);
                work.push_back(i);
            }
        };
        for (const NodeId id : unitRoots[u]) claim(index[id]);
        for (std::size_t k = 0; k < work.size(); ++k) {
            const Node& n = nodes[work[k]];
            if (const auto p = byParent.find(n.id); p != byParent.end()) {
                for (const std::size_t c : p->second) claim(c);
            }
            if (n.groupId != kNoGroup) {
                for (const std::size_t m : byGroup[n.groupId]) claim(m);
            }
        }
    }

    // Collect the merged units in order of first appearance.
    std::vector<Unit> units;
    std::unordered_map<std::size_t, std::size_t> slot;
    std::vector<std::unordered_set<NodeId>> own;
    for (std::size_t u = 0; u < unitRoots.size(); ++u) {
        const std::size_t r = dsu.find(u);
        const auto [it, fresh] = slot.try_emplace(r, units.size());
        if (fresh) {
            units.emplace_back();
            own.emplace_back();
        }
        Unit& unit = units[it->second];
        for (const NodeId id : unitRoots[u]) {
            unit.roots.push_back(id);
            own[it->second].insert(id);
            const Node& n = nodes[index[id]];
            if (n.groupId != kNoGroup) {
                for (const std::size_t m : byGroup[n.groupId]) own[it->second].insert(nodes[m].id);
            }
        }
    }
    for (std::size_t k = 0; k < units.size(); ++k) {
        bool first = true;
        for (const NodeId id : own[k]) {
            const Rect& r = nodes[index[id]].rect;
            units[k].bounds = first ? r : unionRect(units[k].bounds, r);
            first = false;
        }
    }
    if (nodeUnit) {
        nodeUnit->assign(nodes.size(), kNoUnit);
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (claimed[i] >= 0) (*nodeUnit)[i] = slot[dsu.find(static_cast<std::size_t>(claimed[i]))];
        }
    }
    return units;
}

// Moves a unit by (dx, dy); the tolerance guards against float noise. Returns whether anything changed.
bool moveUnit(FlowDocument& doc, const Unit& unit, float dx, float dy, float refX, float refY) {
    if (std::fabs(dx) <= tolerance(refX)) dx = 0.0f;
    if (std::fabs(dy) <= tolerance(refY)) dy = 0.0f;
    if (dx == 0.0f && dy == 0.0f) return false;
    return doc.moveNodes(unit.roots, dx, dy);
}

} // namespace

// Clamping: moveNodes clamps coordinates to +-kMaxCoord, so a unit pushed against that limit can end up short of its
// target (a squashed result), and the alignment is then only approximate for that unit.
bool alignNodes(FlowDocument& doc, std::span<const NodeId> ids, AlignMode mode) {
    const std::vector<Unit> units = buildUnits(doc, ids);
    if (units.size() < 2) return false;

    Rect all = units.front().bounds;
    for (const Unit& u : units) all = unionRect(all, u.bounds);

    // Deltas first (units are independent, so order does not matter), then the moves.
    std::vector<Pt> deltas(units.size());
    for (std::size_t i = 0; i < units.size(); ++i) {
        const Rect& b = units[i].bounds;
        switch (mode) {
        case AlignMode::Left: deltas[i].x = all.x - b.x; break;
        case AlignMode::Center: deltas[i].x = all.center().x - b.center().x; break;
        case AlignMode::Right: deltas[i].x = all.right() - b.right(); break;
        case AlignMode::Top: deltas[i].y = all.y - b.y; break;
        case AlignMode::Middle: deltas[i].y = all.center().y - b.center().y; break;
        case AlignMode::Bottom: deltas[i].y = all.bottom() - b.bottom(); break;
        }
    }
    bool changed = false;
    for (std::size_t i = 0; i < units.size(); ++i) {
        changed |= moveUnit(doc, units[i], deltas[i].x, deltas[i].y, units[i].bounds.x, units[i].bounds.y);
    }
    return changed;
}

bool distributeNodes(FlowDocument& doc, std::span<const NodeId> ids, DistributeAxis axis) {
    std::vector<Unit> units = buildUnits(doc, ids);
    if (units.size() < 3) return false;

    const bool horiz = axis == DistributeAxis::Horizontal;
    const auto lo = [&](const Rect& r) { return horiz ? r.x : r.y; };
    const auto size = [&](const Rect& r) { return horiz ? r.w : r.h; };
    const auto mid = [&](const Rect& r) { return lo(r) + size(r) * 0.5f; };

    // By center, ties keep selection order. The first and last units are the fixed ends.
    std::stable_sort(units.begin(), units.end(),
                     [&](const Unit& a, const Unit& b) { return mid(a.bounds) < mid(b.bounds); });

    const float start = lo(units.front().bounds);
    const float end = lo(units.back().bounds) + size(units.back().bounds);
    float total = 0.0f;
    for (const Unit& u : units) total += size(u.bounds);
    // Equal gaps; negative when the units overlap, in which case they overlap evenly.
    const float gap = (end - start - total) / static_cast<float>(units.size() - 1);

    bool changed = false;
    float cursor = start + size(units.front().bounds) + gap;
    for (std::size_t i = 1; i + 1 < units.size(); ++i) {
        const float d = cursor - lo(units[i].bounds);
        changed |= moveUnit(doc, units[i], horiz ? d : 0.0f, horiz ? 0.0f : d, lo(units[i].bounds), lo(units[i].bounds));
        cursor += size(units[i].bounds) + gap;
    }
    return changed;
}

std::size_t layoutUnitCount(const FlowDocument& doc, std::span<const NodeId> ids) {
    return buildUnits(doc, ids).size();
}

bool matchSize(FlowDocument& doc, std::span<const NodeId> ids, MatchMode mode) {
    const std::vector<NodeId> sel = existingUnique(doc, ids);
    if (sel.size() < 2) return false;

    const Rect ref = doc.node(sel.front())->rect;
    bool changed = false;
    for (std::size_t i = 1; i < sel.size(); ++i) {
        Rect r = doc.node(sel[i])->rect;
        if (mode == MatchMode::Width) r.w = ref.w;
        else r.h = ref.h;
        changed |= doc.setNodeRect(sel[i], r);
    }
    return changed;
}

namespace {

// A connector between two different units, as indices into the unit list. `back` marks DFS back edges, which the
// layering sees reversed.
struct UnitEdge {
    std::size_t from;
    std::size_t to;
    bool back = false;
};

// Components wider than this wrap onto a new row so a huge set of small components stays inside +-kMaxCoord.
constexpr float kRowLimit = 1.0e6f;

// Breaks cycles by reversing DFS back edges. Units and edges are visited in index/document order, so the result is
// deterministic. Returns the units in a topological order of the resulting graph (reverse post-order).
std::vector<std::size_t> breakCycles(std::size_t n, std::vector<UnitEdge>& edges) {
    std::vector<std::vector<std::size_t>> out(n); // edge indices
    for (std::size_t k = 0; k < edges.size(); ++k) out[edges[k].from].push_back(k);

    enum : unsigned char { White, Gray, Black };
    std::vector<unsigned char> color(n, White);
    std::vector<std::size_t> post;
    post.reserve(n);
    std::vector<std::pair<std::size_t, std::size_t>> stack; // unit, next edge slot
    for (std::size_t root = 0; root < n; ++root) {
        if (color[root] != White) continue;
        color[root] = Gray;
        stack.emplace_back(root, 0);
        while (!stack.empty()) {
            const std::size_t u = stack.back().first;
            if (stack.back().second < out[u].size()) {
                UnitEdge& e = edges[out[u][stack.back().second++]];
                if (color[e.to] == Gray) {
                    e.back = true;
                } else if (color[e.to] == White) {
                    color[e.to] = Gray;
                    stack.emplace_back(e.to, 0);
                }
            } else {
                color[u] = Black;
                post.push_back(u);
                stack.pop_back();
            }
        }
    }
    std::reverse(post.begin(), post.end());
    return post;
}

} // namespace

bool autoLayout(FlowDocument& doc, std::span<const NodeId> ids, LayoutDirection direction, float spacing) {
    if (!std::isfinite(spacing)) return false;
    spacing = std::clamp(spacing, 0.0f, 2000.0f);

    std::vector<NodeId> every;
    if (ids.empty()) {
        every.reserve(doc.nodes().size());
        for (const Node& n : doc.nodes()) every.push_back(n.id);
        ids = every;
    }
    std::vector<std::size_t> nodeUnit;
    const std::vector<Unit> units = buildUnits(doc, ids, &nodeUnit);
    const std::size_t n = units.size();
    if (n < 2) return false;

    // The footprint of a unit is every node that moves with it (children of containers included), so the result
    // cannot overlap even when a child sticks out of its container.
    const auto& nodes = doc.nodes();
    std::vector<Rect> box(n);
    std::vector<bool> boxed(n, false);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const std::size_t u = nodeUnit[i];
        if (u == kNoUnit) continue;
        box[u] = boxed[u] ? unionRect(box[u], nodes[i].rect) : nodes[i].rect;
        boxed[u] = true;
    }

    // Connectors between two different units; everything else is ignored by the layering.
    const auto unitOf = [&](const End& end) -> std::size_t {
        const Node* p = end.node == kNoNode ? nullptr : doc.node(end.node);
        return p ? nodeUnit[static_cast<std::size_t>(p - nodes.data())] : kNoUnit;
    };
    std::vector<UnitEdge> edges;
    std::vector<EdgeId> inner; // the connectors in `edges`, same order
    for (const Edge& e : doc.edges()) {
        const std::size_t a = unitOf(e.from);
        const std::size_t b = unitOf(e.to);
        if (a == kNoUnit || b == kNoUnit || a == b) continue;
        edges.push_back({a, b});
        inner.push_back(e.id);
    }

    // 1-2. Cycle breaking and longest-path layering.
    const std::vector<std::size_t> topo = breakCycles(n, edges);
    std::vector<std::vector<std::size_t>> succ(n), pred(n); // as reversed
    for (const UnitEdge& e : edges) {
        const std::size_t from = e.back ? e.to : e.from;
        const std::size_t to = e.back ? e.from : e.to;
        succ[from].push_back(to);
        pred[to].push_back(from);
    }
    std::vector<std::size_t> layer(n, 0);
    for (const std::size_t u : topo) {
        for (const std::size_t w : succ[u]) layer[w] = std::max(layer[w], layer[u] + 1);
    }

    // Components of the connector graph, numbered by their first unit.
    Dsu dsu(n);
    for (const UnitEdge& e : edges) dsu.unite(e.from, e.to);
    std::vector<std::vector<std::size_t>> comps;
    {
        std::unordered_map<std::size_t, std::size_t> slot;
        for (std::size_t u = 0; u < n; ++u) {
            const auto [it, fresh] = slot.try_emplace(dsu.find(u), comps.size());
            if (fresh) comps.emplace_back();
            comps[it->second].push_back(u);
        }
    }

    const bool topDown = direction == LayoutDirection::TopDown;
    const auto mainSize = [&](std::size_t u) { return topDown ? box[u].h : box[u].w; };
    const auto crossSize = [&](std::size_t u) { return topDown ? box[u].w : box[u].h; };
    const float gap = spacing * 2.0f / 3.0f;

    // Target top-left of every unit in layout space (components packed in rows from 0,0).
    std::vector<Pt> target(n);
    std::vector<std::vector<std::size_t>> byLayer;
    std::vector<float> pos(n, 0.0f); // fractional place inside its layer, comparable across layers
    std::vector<std::pair<float, std::size_t>> keyed;
    float cursor = 0.0f;  // cross offset of the next component in the current row
    float rowMain = 0.0f; // main offset of the current row
    float rowExtent = 0.0f;
    const auto refresh = [&](const std::vector<std::size_t>& row) {
        for (std::size_t k = 0; k < row.size(); ++k) {
            pos[row[k]] = (static_cast<float>(k) + 0.5f) / static_cast<float>(row.size());
        }
    };
    // A unit without neighbors on the guiding side keeps its place.
    const auto reorder = [&](std::vector<std::size_t>& row, const std::vector<std::vector<std::size_t>>& guide) {
        keyed.clear();
        for (const std::size_t u : row) {
            float sum = 0.0f;
            for (const std::size_t g : guide[u]) sum += pos[g];
            keyed.emplace_back(guide[u].empty() ? pos[u] : sum / static_cast<float>(guide[u].size()), u);
        }
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t k = 0; k < row.size(); ++k) row[k] = keyed[k].second;
        refresh(row);
    };
    for (const std::vector<std::size_t>& members : comps) {
        std::size_t depth = 0;
        for (const std::size_t u : members) depth = std::max(depth, layer[u]);
        byLayer.assign(depth + 1, {});
        for (const std::size_t u : members) byLayer[layer[u]].push_back(u); // document order
        for (const auto& row : byLayer) refresh(row);

        // 3. Barycenter sweeps: down, up, down, up.
        for (int sweep = 0; sweep < 4 && depth > 0; ++sweep) {
            if (sweep % 2 == 0) {
                for (std::size_t l = 1; l <= depth; ++l) reorder(byLayer[l], pred);
            } else {
                for (std::size_t l = depth; l-- > 0;) reorder(byLayer[l], succ);
            }
        }

        // 4. Coordinates: layers are bands along the main axis; each layer is centered across the component.
        std::vector<float> extent(depth + 1, 0.0f), width(depth + 1, 0.0f), start(depth + 1, 0.0f);
        float compWidth = 0.0f;
        for (std::size_t l = 0; l <= depth; ++l) {
            for (const std::size_t u : byLayer[l]) {
                extent[l] = std::max(extent[l], mainSize(u));
                width[l] += crossSize(u);
            }
            width[l] += gap * static_cast<float>(byLayer[l].size() - 1);
            compWidth = std::max(compWidth, width[l]);
            if (l > 0) start[l] = start[l - 1] + extent[l - 1] + spacing;
        }

        // 5. Components sit side by side across the layer axis, wrapping onto a new row at the limit.
        if (cursor > 0.0f && cursor + compWidth > kRowLimit) {
            rowMain += rowExtent + spacing;
            rowExtent = 0.0f;
            cursor = 0.0f;
        }
        for (std::size_t l = 0; l <= depth; ++l) {
            float across = cursor + (compWidth - width[l]) * 0.5f;
            for (const std::size_t u : byLayer[l]) {
                const float along = rowMain + start[l] + (extent[l] - mainSize(u)) * 0.5f;
                target[u] = topDown ? Pt{across, along} : Pt{along, across};
                across += crossSize(u) + gap;
            }
        }
        rowExtent = std::max(rowExtent, start[depth] + extent[depth]);
        cursor += compWidth + spacing;
    }

    // Anchor: the top-left of the bounds of all the units stays where it was.
    Rect before = box.front();
    Pt low = target.front();
    for (std::size_t u = 1; u < n; ++u) {
        before = unionRect(before, box[u]);
        low.x = std::min(low.x, target[u].x);
        low.y = std::min(low.y, target[u].y);
    }
    bool changed = false;
    for (std::size_t u = 0; u < n; ++u) {
        const float dx = before.x + (target[u].x - low.x) - box[u].x;
        const float dy = before.y + (target[u].y - low.y) - box[u].y;
        changed |= moveUnit(doc, units[u], dx, dy, box[u].x, box[u].y);
    }

    // Connectors among the laid-out units follow their new places (and are routed automatically again).
    for (const EdgeId id : inner) {
        changed |= doc.setEdgeVia(id, std::nullopt);
        const Edge* e = doc.edge(id);
        if (e && e->from.face != Face::Auto) changed |= doc.reconnect(id, true, End{e->from.node, Face::Auto, e->from.point});
        e = doc.edge(id);
        if (e && e->to.face != Face::Auto) changed |= doc.reconnect(id, false, End{e->to.node, Face::Auto, e->to.point});
    }
    return changed;
}

} // namespace trace::flow
