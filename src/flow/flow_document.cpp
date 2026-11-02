#include "flow/flow_document.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace trace::flow {

namespace {

// NaN becomes 0, everything else is clamped to +-kMaxCoord (so infinity becomes the limit).
float sanitizeCoord(float v) noexcept {
    if (std::isnan(v)) return 0.0f;
    return std::clamp(v, -kMaxCoord, kMaxCoord);
}

float sanitizeSize(float v) noexcept {
    if (std::isnan(v)) return kMinNodeSize;
    return std::clamp(v, kMinNodeSize, kMaxCoord);
}

float clampOr(float v, float lo, float hi, float fallback) noexcept {
    if (std::isnan(v)) return fallback;
    return std::clamp(v, lo, hi);
}

Rect sanitizeRect(const Rect& r) noexcept {
    return Rect{sanitizeCoord(r.x), sanitizeCoord(r.y), sanitizeSize(r.w), sanitizeSize(r.h)};
}

Style sanitizeStyle(Style s) noexcept {
    constexpr Style kDefault{};
    s.strokeWidth = clampOr(s.strokeWidth, 0.5f, 32.0f, kDefault.strokeWidth);
    s.cornerRadius = clampOr(s.cornerRadius, 0.0f, 200.0f, kDefault.cornerRadius);
    s.fontSize = clampOr(s.fontSize, 6.0f, 200.0f, kDefault.fontSize);
    if (static_cast<unsigned>(s.dash) > static_cast<unsigned>(Dash::Dotted)) s.dash = kDefault.dash;
    if (static_cast<unsigned>(s.align) > static_cast<unsigned>(TextAlign::Right)) s.align = kDefault.align;
    if (static_cast<unsigned>(s.valign) > static_cast<unsigned>(VAlign::Bottom)) s.valign = kDefault.valign;
    return s;
}

// Cuts to kMaxTextBytes without splitting a UTF-8 sequence: the cut backs up to a lead byte.
std::string sanitizeText(std::string s) {
    if (s.size() <= kMaxTextBytes) return s;
    std::size_t cut = kMaxTextBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0u) == 0x80u) --cut;
    s.resize(cut);
    return s;
}

bool validKind(NodeKind k) noexcept { return static_cast<int>(k) >= 0 && static_cast<int>(k) < kNodeKindCount; }
int clampSides(int sides) noexcept { return sides < kMinPolygonSides ? kMinPolygonSides : (sides > kMaxPolygonSides ? kMaxPolygonSides : sides); }
bool validRoute(Route r) noexcept { return static_cast<unsigned>(r) <= static_cast<unsigned>(Route::Straight); }
bool validArrow(Arrow a) noexcept { return static_cast<unsigned>(a) <= static_cast<unsigned>(Arrow::Dot); }

// A free end keeps only its (finite, clamped) point; a docked end keeps only node and face.
End canonicalEnd(End e) noexcept {
    if (e.node == kNoNode) {
        e.face = Face::Auto;
        e.point = Pt{sanitizeCoord(e.point.x), sanitizeCoord(e.point.y)};
    } else {
        if (static_cast<unsigned>(e.face) > static_cast<unsigned>(Face::Auto)) e.face = Face::Auto;
        e.point = Pt{};
    }
    return e;
}

bool overlaps(const Rect& a, const Rect& b) noexcept {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

} // namespace

// --- read access -------------------------------------------------------------------------------------

const Node* FlowDocument::node(NodeId id) const noexcept {
    const auto it = nodeIndex_.find(id);
    return it == nodeIndex_.end() ? nullptr : &nodes_[it->second];
}

const Edge* FlowDocument::edge(EdgeId id) const noexcept {
    // Edge ids only grow and removal keeps order, so edges_ is sorted by id.
    const auto it = std::lower_bound(edges_.begin(), edges_.end(), id,
                                     [](const Edge& e, EdgeId v) { return e.id < v; });
    return (it != edges_.end() && it->id == id) ? &*it : nullptr;
}

ImagePtr FlowDocument::image(ImageId id) const noexcept {
    const auto it = images_.find(id);
    return it == images_.end() ? ImagePtr{} : it->second;
}

std::vector<NodeId> FlowDocument::nodesByZ() const {
    std::vector<const Node*> order;
    order.reserve(nodes_.size());
    for (const Node& n : nodes_) order.push_back(&n);
    std::sort(order.begin(), order.end(), [](const Node* a, const Node* b) { return a->z < b->z; });
    std::vector<NodeId> out;
    out.reserve(order.size());
    for (const Node* n : order) out.push_back(n->id);
    return out;
}

std::vector<NodeId> FlowDocument::childrenOf(NodeId container) const {
    std::vector<NodeId> out;
    if (container == kNoNode) return out;
    for (const Node& n : nodes_) {
        if (n.parentId == container) out.push_back(n.id);
    }
    return out;
}

std::vector<NodeId> FlowDocument::groupMembers(GroupId group) const {
    std::vector<NodeId> out;
    if (group == kNoGroup) return out;
    for (const Node& n : nodes_) {
        if (n.groupId == group) out.push_back(n.id);
    }
    return out;
}

Rect FlowDocument::boundsOf(std::span<const NodeId> ids) const {
    bool any = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (const NodeId id : ids) {
        const Node* n = node(id);
        if (!n) continue;
        if (!any) {
            x0 = n->rect.x;
            y0 = n->rect.y;
            x1 = n->rect.right();
            y1 = n->rect.bottom();
            any = true;
        } else {
            x0 = std::min(x0, n->rect.x);
            y0 = std::min(y0, n->rect.y);
            x1 = std::max(x1, n->rect.right());
            y1 = std::max(y1, n->rect.bottom());
        }
    }
    return any ? Rect{x0, y0, x1 - x0, y1 - y0} : Rect{};
}

std::size_t FlowDocument::approxBytes() const noexcept {
    std::size_t bytes = sizeof(*this);
    bytes += nodes_.capacity() * sizeof(Node) + edges_.capacity() * sizeof(Edge);
    for (const Node& n : nodes_) bytes += n.text.size();
    for (const Edge& e : edges_) bytes += e.label.size();
    bytes += nodeIndex_.size() * 40; // bucket + node overhead per entry
    bytes += images_.size() * 64;    // map entry + shared_ptr control block, pixels are shared and excluded
    return bytes;
}

void FlowDocument::reindex() {
    nodeIndex_.clear();
    nodeIndex_.reserve(nodes_.size());
    for (std::size_t i = 0; i < nodes_.size(); ++i) nodeIndex_[nodes_[i].id] = i;
}

// --- nodes -------------------------------------------------------------------------------------------

NodeId FlowDocument::addNode(NodeKind kind, Rect rect, std::string text, Style style, int sides) {
    if (!validKind(kind) || nodes_.size() >= kMaxNodes) return kNoNode;
    Node n;
    n.id = nextNode_++;
    n.kind = kind;
    n.rect = sanitizeRect(rect);
    n.text = sanitizeText(std::move(text));
    n.style = sanitizeStyle(style);
    n.sides = clampSides(sides);
    n.z = nextZ_++;
    nodeIndex_[n.id] = nodes_.size();
    nodes_.push_back(std::move(n));
    ++revision_;
    return nodes_.back().id;
}

ImageId FlowDocument::addImage(ImagePtr image) {
    if (!image || image->empty() || images_.size() >= kMaxImages) return kNoImage;
    const ImageId id = nextImage_++;
    images_.emplace(id, std::move(image));
    ++revision_;
    return id;
}

bool FlowDocument::setNodeImage(NodeId id, ImageId image) {
    const auto it = nodeIndex_.find(id);
    if (it == nodeIndex_.end()) return false;
    Node& n = nodes_[it->second];
    if (n.kind != NodeKind::Image) return false;
    if (image != kNoImage && images_.find(image) == images_.end()) return false;
    if (n.imageId == image) return false;
    n.imageId = image;
    ++revision_;
    return true;
}

std::size_t FlowDocument::removeNodes(std::span<const NodeId> ids) {
    std::unordered_set<NodeId> doomed;
    for (const NodeId id : ids) {
        if (nodeIndex_.count(id)) doomed.insert(id);
    }
    if (doomed.empty()) return 0;

    std::erase_if(edges_, [&](const Edge& e) { return doomed.count(e.from.node) || doomed.count(e.to.node); });

    std::erase_if(nodes_, [&](const Node& n) { return doomed.count(n.id) != 0; });
    std::unordered_map<GroupId, std::size_t> groupSizes;
    for (Node& n : nodes_) {
        if (n.parentId != kNoNode && doomed.count(n.parentId)) n.parentId = kNoNode;
        if (n.groupId != kNoGroup) ++groupSizes[n.groupId];
    }
    for (Node& n : nodes_) {
        if (n.groupId != kNoGroup && groupSizes[n.groupId] < 2) n.groupId = kNoGroup;
    }
    reindex();
    ++revision_;
    return doomed.size();
}

bool FlowDocument::moveNodes(std::span<const NodeId> ids, float dx, float dy) {
    if (std::isnan(dx) || std::isnan(dy)) return false;
    dx = std::clamp(dx, -2.0f * kMaxCoord, 2.0f * kMaxCoord);
    dy = std::clamp(dy, -2.0f * kMaxCoord, 2.0f * kMaxCoord);
    if (dx == 0.0f && dy == 0.0f) return false;

    // Closure over children and group members, each node once.
    std::unordered_map<NodeId, std::vector<std::size_t>> byParent;
    std::unordered_map<GroupId, std::vector<std::size_t>> byGroup;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].parentId != kNoNode) byParent[nodes_[i].parentId].push_back(i);
        if (nodes_[i].groupId != kNoGroup) byGroup[nodes_[i].groupId].push_back(i);
    }
    std::vector<char> seen(nodes_.size(), 0);
    std::vector<std::size_t> work;
    const auto push = [&](std::size_t i) {
        if (!seen[i]) {
            seen[i] = 1;
            work.push_back(i);
        }
    };
    for (const NodeId id : ids) {
        const auto it = nodeIndex_.find(id);
        if (it != nodeIndex_.end()) push(it->second);
    }
    for (std::size_t k = 0; k < work.size(); ++k) {
        const Node& n = nodes_[work[k]];
        if (const auto p = byParent.find(n.id); p != byParent.end()) {
            for (const std::size_t c : p->second) push(c);
        }
        if (n.groupId != kNoGroup) {
            for (const std::size_t m : byGroup[n.groupId]) push(m);
        }
    }

    bool changed = false;
    for (const std::size_t i : work) {
        Rect& r = nodes_[i].rect;
        const float nx = sanitizeCoord(r.x + dx);
        const float ny = sanitizeCoord(r.y + dy);
        if (nx != r.x || ny != r.y) changed = true;
        r.x = nx;
        r.y = ny;
    }
    if (changed) ++revision_;
    return changed;
}

bool FlowDocument::setNodeRect(NodeId id, Rect rect) {
    const auto it = nodeIndex_.find(id);
    if (it == nodeIndex_.end()) return false;
    Node& n = nodes_[it->second];
    const Rect r = sanitizeRect(rect);
    if (r == n.rect) return false;
    n.rect = r;
    ++revision_;
    return true;
}

bool FlowDocument::setText(NodeId id, std::string text) {
    const auto it = nodeIndex_.find(id);
    if (it == nodeIndex_.end()) return false;
    Node& n = nodes_[it->second];
    text = sanitizeText(std::move(text));
    if (text == n.text) return false;
    n.text = std::move(text);
    ++revision_;
    return true;
}

bool FlowDocument::setStyle(NodeId id, const Style& style) {
    const auto it = nodeIndex_.find(id);
    if (it == nodeIndex_.end()) return false;
    Node& n = nodes_[it->second];
    const Style s = sanitizeStyle(style);
    if (s == n.style) return false;
    n.style = s;
    ++revision_;
    return true;
}

bool FlowDocument::setKind(NodeId id, NodeKind kind) {
    if (!validKind(kind)) return false;
    const auto it = nodeIndex_.find(id);
    if (it == nodeIndex_.end()) return false;
    Node& n = nodes_[it->second];
    if (n.kind == kind) return false;
    if (n.kind == NodeKind::Image) n.imageId = kNoImage;
    if (n.kind == NodeKind::Container) {
        for (Node& c : nodes_) {
            if (c.parentId == id) c.parentId = kNoNode; // only a Container can be a parent
        }
    }
    n.kind = kind;
    ++revision_;
    return true;
}

bool FlowDocument::setSides(NodeId id, int sides) {
    const auto it = nodeIndex_.find(id);
    if (it == nodeIndex_.end()) return false;
    Node& n = nodes_[it->second];
    sides = clampSides(sides);
    if (n.kind != NodeKind::Polygon || n.sides == sides) return false;
    n.sides = sides;
    ++revision_;
    return true;
}

// --- edges -------------------------------------------------------------------------------------------

EdgeId FlowDocument::connect(End from, End to) {
    if (edges_.size() >= kMaxEdges) return kNoEdge;
    if ((from.node != kNoNode && !nodeIndex_.count(from.node)) ||
        (to.node != kNoNode && !nodeIndex_.count(to.node))) {
        return kNoEdge;
    }
    Edge e;
    e.id = nextEdge_++;
    e.from = canonicalEnd(from);
    e.to = canonicalEnd(to);
    edges_.push_back(std::move(e));
    ++revision_;
    return edges_.back().id;
}

bool FlowDocument::reconnect(EdgeId id, bool fromEnd, End end) {
    if (end.node != kNoNode && !nodeIndex_.count(end.node)) return false;
    const Edge* found = edge(id);
    if (!found) return false;
    Edge& e = edges_[static_cast<std::size_t>(found - edges_.data())];
    End& slot = fromEnd ? e.from : e.to;
    const End next = canonicalEnd(end);
    if (slot == next) return false;
    slot = next;
    e.via.reset(); // the hand-made route was shaped for the old end
    ++revision_;
    return true;
}

bool FlowDocument::setEdgeRoute(EdgeId id, Route route, Arrow start, Arrow end) {
    if (!validRoute(route) || !validArrow(start) || !validArrow(end)) return false;
    const Edge* found = edge(id);
    if (!found) return false;
    Edge& e = edges_[static_cast<std::size_t>(found - edges_.data())];
    if (e.route == route && e.arrowStart == start && e.arrowEnd == end) return false;
    if (e.route != route) e.via.reset(); // an offset means something else for each route
    e.route = route;
    e.arrowStart = start;
    e.arrowEnd = end;
    ++revision_;
    return true;
}

bool FlowDocument::setEdgeVia(EdgeId id, std::optional<Pt> via) {
    const Edge* found = edge(id);
    if (!found) return false;
    Edge& e = edges_[static_cast<std::size_t>(found - edges_.data())];
    if (via) via = Pt{sanitizeCoord(via->x), sanitizeCoord(via->y)};
    if (e.via == via) return false;
    e.via = via;
    ++revision_;
    return true;
}

bool FlowDocument::setEdgeLabel(EdgeId id, std::string label) {
    const Edge* found = edge(id);
    if (!found) return false;
    Edge& e = edges_[static_cast<std::size_t>(found - edges_.data())];
    label = sanitizeText(std::move(label));
    if (label == e.label) return false;
    e.label = std::move(label);
    ++revision_;
    return true;
}

bool FlowDocument::setEdgeLabelPlacement(EdgeId id, float position, LabelSide side, bool along) {
    const Edge* found = edge(id);
    if (!found) return false;
    if (static_cast<int>(side) < 0 || static_cast<int>(side) > static_cast<int>(LabelSide::Below)) return false;
    Edge& e = edges_[static_cast<std::size_t>(found - edges_.data())];
    position = std::isnan(position) ? 0.5f : std::clamp(position, 0.0f, 1.0f);
    if (e.labelPos == position && e.labelSide == side && e.labelAlong == along) return false;
    e.labelPos = position;
    e.labelSide = side;
    e.labelAlong = along;
    ++revision_;
    return true;
}

bool FlowDocument::setEdgeStyle(EdgeId id, const Style& style) {
    const Edge* found = edge(id);
    if (!found) return false;
    Edge& e = edges_[static_cast<std::size_t>(found - edges_.data())];
    const Style s = sanitizeStyle(style);
    if (s == e.style) return false;
    e.style = s;
    ++revision_;
    return true;
}

std::size_t FlowDocument::removeEdges(std::span<const EdgeId> ids) {
    const std::unordered_set<EdgeId> doomed(ids.begin(), ids.end());
    const std::size_t removed = std::erase_if(edges_, [&](const Edge& e) { return doomed.count(e.id) != 0; });
    if (removed > 0) ++revision_;
    return removed;
}

// --- z-order -----------------------------------------------------------------------------------------

namespace {

// Rewrites z as 1..n following `order` and reports whether anything differed from `before`.
bool applyOrder(std::vector<Node>& nodes, const std::unordered_map<NodeId, std::size_t>& index,
                const std::vector<NodeId>& before, const std::vector<NodeId>& order, std::uint32_t& nextZ) {
    if (order == before) return false;
    std::uint32_t z = 1;
    for (const NodeId id : order) nodes[index.at(id)].z = z++;
    nextZ = z;
    return true;
}

} // namespace

bool FlowDocument::bringToFront(std::span<const NodeId> ids) {
    const std::unordered_set<NodeId> pick(ids.begin(), ids.end());
    const std::vector<NodeId> before = nodesByZ();
    std::vector<NodeId> order;
    order.reserve(before.size());
    for (const NodeId id : before) {
        if (!pick.count(id)) order.push_back(id);
    }
    for (const NodeId id : before) {
        if (pick.count(id)) order.push_back(id);
    }
    if (!applyOrder(nodes_, nodeIndex_, before, order, nextZ_)) return false;
    ++revision_;
    return true;
}

bool FlowDocument::sendToBack(std::span<const NodeId> ids) {
    const std::unordered_set<NodeId> pick(ids.begin(), ids.end());
    const std::vector<NodeId> before = nodesByZ();
    std::vector<NodeId> order;
    order.reserve(before.size());
    for (const NodeId id : before) {
        if (pick.count(id)) order.push_back(id);
    }
    for (const NodeId id : before) {
        if (!pick.count(id)) order.push_back(id);
    }
    if (!applyOrder(nodes_, nodeIndex_, before, order, nextZ_)) return false;
    ++revision_;
    return true;
}

bool FlowDocument::bringForward(std::span<const NodeId> ids) {
    const std::unordered_set<NodeId> pick(ids.begin(), ids.end());
    const std::vector<NodeId> before = nodesByZ();
    std::vector<NodeId> order = before;
    // Top to bottom, each picked node hops just past the next overlapping unpicked node above it; it never
    // crosses another picked node, so the block keeps its relative order.
    for (std::size_t i = order.size(); i-- > 0;) {
        if (!pick.count(order[i])) continue;
        const Rect& mine = nodes_[nodeIndex_.at(order[i])].rect;
        for (std::size_t j = i + 1; j < order.size(); ++j) {
            if (pick.count(order[j])) break;
            if (overlaps(mine, nodes_[nodeIndex_.at(order[j])].rect)) {
                std::rotate(order.begin() + static_cast<std::ptrdiff_t>(i),
                            order.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                            order.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                break;
            }
        }
    }
    if (!applyOrder(nodes_, nodeIndex_, before, order, nextZ_)) return false;
    ++revision_;
    return true;
}

bool FlowDocument::sendBackward(std::span<const NodeId> ids) {
    const std::unordered_set<NodeId> pick(ids.begin(), ids.end());
    const std::vector<NodeId> before = nodesByZ();
    std::vector<NodeId> order = before;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (!pick.count(order[i])) continue;
        const Rect& mine = nodes_[nodeIndex_.at(order[i])].rect;
        for (std::size_t j = i; j-- > 0;) {
            if (pick.count(order[j])) break;
            if (overlaps(mine, nodes_[nodeIndex_.at(order[j])].rect)) {
                std::rotate(order.begin() + static_cast<std::ptrdiff_t>(j),
                            order.begin() + static_cast<std::ptrdiff_t>(i),
                            order.begin() + static_cast<std::ptrdiff_t>(i) + 1);
                break;
            }
        }
    }
    if (!applyOrder(nodes_, nodeIndex_, before, order, nextZ_)) return false;
    ++revision_;
    return true;
}

// --- groups and containment --------------------------------------------------------------------------

GroupId FlowDocument::group(std::span<const NodeId> ids) {
    std::unordered_set<NodeId> picked;
    for (const NodeId id : ids) {
        if (nodeIndex_.count(id)) picked.insert(id);
    }
    if (picked.size() < 2) return kNoGroup;

    // Already exactly one group: nothing to change, hand back its id.
    const GroupId first = nodes_[nodeIndex_.at(*picked.begin())].groupId;
    if (first != kNoGroup) {
        bool same = true;
        for (const NodeId id : picked) same = same && nodes_[nodeIndex_.at(id)].groupId == first;
        if (same && groupMembers(first).size() == picked.size()) return first;
    }

    const GroupId fresh = nextGroup_++;
    for (const NodeId id : picked) nodes_[nodeIndex_.at(id)].groupId = fresh;

    // Repair the groups the picked nodes left behind.
    std::unordered_map<GroupId, std::size_t> sizes;
    for (const Node& n : nodes_) {
        if (n.groupId != kNoGroup) ++sizes[n.groupId];
    }
    for (Node& n : nodes_) {
        if (n.groupId != kNoGroup && sizes[n.groupId] < 2) n.groupId = kNoGroup;
    }
    ++revision_;
    return fresh;
}

bool FlowDocument::ungroup(GroupId group) {
    if (group == kNoGroup) return false;
    bool any = false;
    for (Node& n : nodes_) {
        if (n.groupId == group) {
            n.groupId = kNoGroup;
            any = true;
        }
    }
    if (any) ++revision_;
    return any;
}

bool FlowDocument::reparent(NodeId id, NodeId newParent) {
    const auto it = nodeIndex_.find(id);
    if (it == nodeIndex_.end()) return false;
    Node& n = nodes_[it->second];
    if (newParent == n.parentId) return false;
    if (newParent != kNoNode) {
        if (newParent == id) return false;
        const Node* p = node(newParent);
        if (!p || p->kind != NodeKind::Container) return false;
        // Refuse when the new parent sits somewhere below this node.
        std::size_t guard = nodes_.size() + 1;
        for (const Node* a = p; a && guard > 0; a = node(a->parentId), --guard) {
            if (a->id == id) return false;
        }
    }
    n.parentId = newParent;
    ++revision_;
    return true;
}

} // namespace trace::flow
