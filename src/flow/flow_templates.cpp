#include "flow/flow_templates.hpp"

#include <vector>

namespace trace::flow {

namespace {

constexpr float kTerminatorW = 160.0f;
constexpr float kTerminatorH = 56.0f;
constexpr float kStepW = 160.0f;
constexpr float kStepH = 72.0f;

NodeId addTerminator(FlowDocument& doc, float x, float y, const std::string& text) {
    return doc.addNode(NodeKind::Terminator, Rect{x, y, kTerminatorW, kTerminatorH}, text);
}

NodeId addStep(FlowDocument& doc, float x, float y, const std::string& text) {
    return doc.addNode(NodeKind::Rectangle, Rect{x, y, kStepW, kStepH}, text);
}

// A connector from `from` to `to` on the given faces.
EdgeId join(FlowDocument& doc, NodeId from, Face fromFace, NodeId to, Face toFace, Route route,
            const std::string& label = {}) {
    const EdgeId id = doc.connect(End{from, fromFace, {}}, End{to, toFace, {}});
    if (id == kNoEdge) {
        return id;
    }
    doc.setEdgeRoute(id, route, Arrow::None, Arrow::Arrow);
    if (!label.empty()) {
        doc.setEdgeLabel(id, label);
        doc.setEdgeLabelPlacement(id, 0.5f, LabelSide::Above, false);
    }
    return id;
}

void buildProcess(FlowDocument& doc, const TemplateText& t) {
    constexpr float kPitch = 120.0f;
    std::vector<NodeId> chain;
    chain.push_back(addTerminator(doc, 0.0f, 0.0f, t.start));
    for (int i = 1; i <= 3; ++i) {
        chain.push_back(addStep(doc, 0.0f, kPitch * static_cast<float>(i), t.step + " " + std::to_string(i)));
    }
    chain.push_back(addTerminator(doc, 0.0f, kPitch * 4.0f, t.end));
    for (std::size_t i = 1; i < chain.size(); ++i) {
        join(doc, chain[i - 1], Face::Bottom, chain[i], Face::Top, Route::Straight);
    }
}

void buildDecisionTree(FlowDocument& doc, const TemplateText& t) {
    const float cx = 260.0f; // the center line of the trunk
    const NodeId start = addTerminator(doc, cx - kTerminatorW * 0.5f, 0.0f, t.start);
    const NodeId decision = doc.addNode(NodeKind::Diamond, Rect{cx - 110.0f, 116.0f, 220.0f, 120.0f}, t.decision);
    const NodeId yes = addStep(doc, cx - 260.0f, 316.0f, t.action + " A");
    const NodeId no = addStep(doc, cx + 100.0f, 316.0f, t.action + " B");
    const NodeId end = addTerminator(doc, cx - kTerminatorW * 0.5f, 476.0f, t.end);
    join(doc, start, Face::Bottom, decision, Face::Top, Route::Straight);
    join(doc, decision, Face::Left, yes, Face::Top, Route::Elbow, t.yes);
    join(doc, decision, Face::Right, no, Face::Top, Route::Elbow, t.no);
    join(doc, yes, Face::Bottom, end, Face::Left, Route::Elbow);
    join(doc, no, Face::Bottom, end, Face::Right, Route::Elbow);
}

void buildSwimlane(FlowDocument& doc, const TemplateText& t) {
    constexpr float kLaneW = 900.0f;
    constexpr float kLaneH = 200.0f;
    std::vector<NodeId> lanes;
    for (int i = 0; i < 3; ++i) {
        lanes.push_back(doc.addNode(NodeKind::Container, Rect{0.0f, kLaneH * static_cast<float>(i), kLaneW, kLaneH},
                                    t.lane + " " + std::to_string(i + 1)));
    }
    const auto inLane = [&](NodeId lane, NodeId node) {
        doc.reparent(node, lane);
        return node;
    };
    const NodeId start = inLane(lanes[0], addTerminator(doc, 40.0f, 84.0f, t.start));
    const NodeId a = inLane(lanes[0], addStep(doc, 240.0f, 76.0f, t.step + " 1"));
    const NodeId b = inLane(lanes[1], addStep(doc, 440.0f, kLaneH + 76.0f, t.step + " 2"));
    const NodeId c = inLane(lanes[2], addStep(doc, 640.0f, kLaneH * 2.0f + 76.0f, t.step + " 3"));
    const NodeId end = inLane(lanes[2], addTerminator(doc, 40.0f, kLaneH * 2.0f + 84.0f, t.end));
    join(doc, start, Face::Right, a, Face::Left, Route::Straight);
    join(doc, a, Face::Bottom, b, Face::Left, Route::Elbow);
    join(doc, b, Face::Bottom, c, Face::Left, Route::Elbow);
    join(doc, c, Face::Left, end, Face::Right, Route::Straight);
}

} // namespace

FlowDocument makeTemplate(TemplateKind kind, const TemplateText& text) {
    FlowDocument doc;
    switch (kind) {
    case TemplateKind::Process: buildProcess(doc, text); break;
    case TemplateKind::DecisionTree: buildDecisionTree(doc, text); break;
    case TemplateKind::Swimlane: buildSwimlane(doc, text); break;
    }
    return doc;
}

} // namespace trace::flow
