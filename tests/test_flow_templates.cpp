#include <doctest/doctest.h>

#include <cmath>
#include <set>
#include <string>

#include "flow/flow_templates.hpp"

namespace trace::flow {
namespace {

constexpr TemplateKind kAll[kTemplateCount] = {TemplateKind::Process, TemplateKind::DecisionTree, TemplateKind::Swimlane};

bool finiteRect(const Rect& r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h) && r.w > 0 && r.h > 0;
}

// Every shape is finite and has text; every connector's ends dock to shapes that exist; parents are containers.
void checkValid(const FlowDocument& doc) {
    REQUIRE_FALSE(doc.nodes().empty());
    REQUIRE_FALSE(doc.edges().empty());
    std::set<NodeId> ids;
    for (const Node& n : doc.nodes()) {
        CHECK(ids.insert(n.id).second);
        CHECK(finiteRect(n.rect));
        CHECK_FALSE(n.text.empty());
        if (n.parentId != kNoNode) {
            const Node* p = doc.node(n.parentId);
            REQUIRE(p);
            CHECK(p->kind == NodeKind::Container);
        }
    }
    for (const Edge& e : doc.edges()) {
        CHECK(doc.node(e.from.node));
        CHECK(doc.node(e.to.node));
        CHECK(e.from.node != e.to.node);
    }
}

} // namespace

TEST_CASE("every template is a valid, connected document") {
    for (const TemplateKind kind : kAll) {
        CAPTURE(static_cast<int>(kind));
        checkValid(makeTemplate(kind));
    }
}

TEST_CASE("template contents") {
    const FlowDocument process = makeTemplate(TemplateKind::Process);
    CHECK(process.nodes().size() == 5);
    CHECK(process.edges().size() == 4);

    const FlowDocument tree = makeTemplate(TemplateKind::DecisionTree);
    CHECK(tree.nodes().size() == 5);
    CHECK(tree.edges().size() == 5);
    int diamonds = 0;
    for (const Node& n : tree.nodes()) {
        diamonds += n.kind == NodeKind::Diamond ? 1 : 0;
    }
    CHECK(diamonds == 1);
    std::set<std::string> labels;
    for (const Edge& e : tree.edges()) {
        if (!e.label.empty()) {
            labels.insert(e.label);
        }
    }
    CHECK(labels == std::set<std::string>{"Yes", "No"});

    const FlowDocument lanes = makeTemplate(TemplateKind::Swimlane);
    int containers = 0;
    int children = 0;
    for (const Node& n : lanes.nodes()) {
        containers += n.kind == NodeKind::Container ? 1 : 0;
        children += n.parentId != kNoNode ? 1 : 0;
    }
    CHECK(containers == 3);
    CHECK(children == 5);
    // Each step sits inside its lane's rectangle.
    for (const Node& n : lanes.nodes()) {
        if (n.parentId == kNoNode) {
            continue;
        }
        const Rect& lane = lanes.node(n.parentId)->rect;
        CHECK(n.rect.x >= lane.x);
        CHECK(n.rect.y >= lane.y);
        CHECK(n.rect.right() <= lane.right());
        CHECK(n.rect.bottom() <= lane.bottom());
    }
}

TEST_CASE("template text comes from the TemplateText given") {
    TemplateText other;
    other.start = "Inicio";
    other.end = "Fin";
    other.step = "Paso";
    other.decision = "Decision";
    other.yes = "Si";
    other.no = "No";
    other.action = "Accion";
    other.lane = "Carril";
    const FlowDocument doc = makeTemplate(TemplateKind::Swimlane, other);
    std::set<std::string> texts;
    for (const Node& n : doc.nodes()) {
        texts.insert(n.text);
    }
    CHECK(texts.count("Inicio") == 1);
    CHECK(texts.count("Fin") == 1);
    CHECK(texts.count("Paso 1") == 1);
    CHECK(texts.count("Carril 3") == 1);
    CHECK(texts.count("Start") == 0);

    const FlowDocument tree = makeTemplate(TemplateKind::DecisionTree, other);
    bool sawYes = false;
    for (const Edge& e : tree.edges()) {
        sawYes = sawYes || e.label == "Si";
    }
    CHECK(sawYes);
}

TEST_CASE("templates are deterministic and an out-of-range kind gives an empty document") {
    for (const TemplateKind kind : kAll) {
        const FlowDocument a = makeTemplate(kind);
        const FlowDocument b = makeTemplate(kind);
        REQUIRE(a.nodes().size() == b.nodes().size());
        for (std::size_t i = 0; i < a.nodes().size(); ++i) {
            CHECK(a.nodes()[i].rect == b.nodes()[i].rect);
            CHECK(a.nodes()[i].text == b.nodes()[i].text);
        }
    }
    CHECK(makeTemplate(static_cast<TemplateKind>(99)).empty());
}

} // namespace trace::flow
