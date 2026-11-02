#include <doctest/doctest.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "core/document.hpp"
#include "core/ini.hpp"
#include "flow/flow_document.hpp"
#include "flow/flow_serialize.hpp"
#include "io/flow_project.hpp"
#include "io/pixel_project.hpp"
#include "io/project_io.hpp"

using namespace trace::flow;
namespace fs = std::filesystem;

namespace {

using trace::IniFile;
using trace::Rgba8;

constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kTeal{16, 160, 150, 128};
constexpr Rgba8 kInk{20, 30, 40, 255};

struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        std::random_device rd;
        path = fs::temp_directory_path() / ("trace_flow_test_" + std::to_string(rd()) + "_" + std::to_string(++counter));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

ImagePtr patternImage(int w, int h, std::uint8_t seed) {
    auto image = std::make_shared<trace::Image>(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x)
            image->set(x, y, Rgba8{static_cast<std::uint8_t>(seed + x * 40), static_cast<std::uint8_t>(y * 70),
                                   static_cast<std::uint8_t>(seed * 3), static_cast<std::uint8_t>(255 - x)});
    }
    return image;
}

bool samePixels(const trace::Image& a, const trace::Image& b) {
    return a.width() == b.width() && a.height() == b.height() &&
           std::equal(a.pixels().begin(), a.pixels().end(), b.pixels().begin(), b.pixels().end());
}

// --- document checks ---------------------------------------------------------------------------------------

// "" when every invariant of FlowDocument's contract holds, else what is wrong.
std::string validate(const FlowDocument& doc) {
    if (doc.nodes().size() > kMaxNodes)
        return "too many nodes";
    if (doc.edges().size() > kMaxEdges)
        return "too many edges";
    std::set<std::uint32_t> zs;
    std::map<GroupId, int> groups;
    std::set<NodeId> ids;
    const auto finiteIn = [](float v) { return std::isfinite(v) && std::fabs(v) <= kMaxCoord; };
    for (const Node& n : doc.nodes()) {
        if (n.id == kNoNode || !ids.insert(n.id).second)
            return "node id";
        if (!zs.insert(n.z).second)
            return "duplicate z";
        if (!finiteIn(n.rect.x) || !finiteIn(n.rect.y) || !finiteIn(n.rect.w) || !finiteIn(n.rect.h))
            return "coordinates";
        if (n.rect.w < kMinNodeSize || n.rect.h < kMinNodeSize)
            return "size";
        if (n.text.size() > kMaxTextBytes)
            return "text length";
        if (n.sides < kMinPolygonSides || n.sides > kMaxPolygonSides)
            return "sides";
        if (n.imageId != kNoImage && !doc.image(n.imageId))
            return "image id";
        if (n.groupId != kNoGroup)
            ++groups[n.groupId];
        if (n.parentId != kNoNode) {
            const Node* parent = doc.node(n.parentId);
            if (!parent || parent->kind != NodeKind::Container)
                return "parent is not a container";
            std::size_t steps = 0;
            for (const Node* a = parent; a; a = doc.node(a->parentId)) {
                if (a->id == n.id || ++steps > doc.nodes().size())
                    return "parent cycle";
            }
        }
    }
    for (const auto& [group, count] : groups) {
        if (count < 2)
            return "group of one";
    }
    for (const Edge& e : doc.edges()) {
        if (e.from.node != kNoNode && !doc.node(e.from.node))
            return "edge start";
        if (e.to.node != kNoNode && !doc.node(e.to.node))
            return "edge end";
        if (!finiteIn(e.from.point.x) || !finiteIn(e.from.point.y) || !finiteIn(e.to.point.x) || !finiteIn(e.to.point.y))
            return "edge point";
        if (!(e.labelPos >= 0.0f && e.labelPos <= 1.0f))
            return "label position";
        if (e.label.size() > kMaxTextBytes)
            return "label length";
    }
    return {};
}

// "" when two documents are the same drawing: same nodes in paint order, same edges, same groups and parents,
// ignoring the ids themselves. Written separately from the serializer so a field it forgets is caught.
std::string diff(const FlowDocument& a, const FlowDocument& b) {
    const std::vector<NodeId> oa = a.nodesByZ();
    const std::vector<NodeId> ob = b.nodesByZ();
    if (oa.size() != ob.size())
        return "node count";
    std::map<NodeId, std::size_t> rankA;
    std::map<NodeId, std::size_t> rankB;
    for (std::size_t i = 0; i < oa.size(); ++i) {
        rankA[oa[i]] = i + 1;
        rankB[ob[i]] = i + 1;
    }
    const auto rank = [](const std::map<NodeId, std::size_t>& m, NodeId id) -> std::size_t {
        const auto it = m.find(id);
        return it == m.end() ? 0 : it->second;
    };
    std::map<GroupId, GroupId> groupMap;
    std::map<GroupId, GroupId> groupBack;
    for (std::size_t i = 0; i < oa.size(); ++i) {
        const Node& x = *a.node(oa[i]);
        const Node& y = *b.node(ob[i]);
        const std::string at = "node " + std::to_string(i + 1) + ": ";
        if (x.kind != y.kind)
            return at + "kind";
        if (!(x.rect == y.rect))
            return at + "rect";
        if (x.text != y.text)
            return at + "text";
        if (!(x.style == y.style))
            return at + "style";
        if (x.sides != y.sides)
            return at + "sides";
        if (rank(rankA, x.parentId) != rank(rankB, y.parentId))
            return at + "parent";
        if ((x.groupId == kNoGroup) != (y.groupId == kNoGroup))
            return at + "group";
        if (x.groupId != kNoGroup) {
            const auto [it, added] = groupMap.emplace(x.groupId, y.groupId);
            const auto [bt, backAdded] = groupBack.emplace(y.groupId, x.groupId);
            if ((!added && it->second != y.groupId) || (!backAdded && bt->second != x.groupId))
                return at + "group membership";
        }
        const ImagePtr ix = a.image(x.imageId);
        const ImagePtr iy = b.image(y.imageId);
        if ((ix == nullptr) != (iy == nullptr))
            return at + "image presence";
        if (ix && !samePixels(*ix, *iy))
            return at + "image pixels";
    }
    if (a.edges().size() != b.edges().size())
        return "edge count";
    for (std::size_t i = 0; i < a.edges().size(); ++i) {
        const Edge& x = a.edges()[i];
        const Edge& y = b.edges()[i];
        const std::string at = "edge " + std::to_string(i + 1) + ": ";
        const auto endsDiffer = [&](const End& p, const End& q) {
            if (p.node == kNoNode || q.node == kNoNode)
                return p.node != q.node || !(p.point == q.point);
            return rank(rankA, p.node) != rank(rankB, q.node) || p.face != q.face;
        };
        if (endsDiffer(x.from, y.from) || endsDiffer(x.to, y.to))
            return at + "ends";
        if (x.route != y.route || x.arrowStart != y.arrowStart || x.arrowEnd != y.arrowEnd)
            return at + "route or arrows";
        if (x.via != y.via)
            return at + "manual route";
        if (x.label != y.label)
            return at + "label";
        if (x.labelPos != y.labelPos || x.labelSide != y.labelSide || x.labelAlong != y.labelAlong)
            return at + "label placement";
        if (!(x.style == y.style))
            return at + "style";
    }
    return {};
}

// Through the same text a file holds, so the INI escaping is part of every round trip.
FlowParse roundTrip(const FlowDocument& doc) {
    FlowSerialized written = serializeFlow(doc);
    const IniFile reread = IniFile::parse(written.ini.serialize());
    std::vector<trace::Image> images;
    for (const ImagePtr& image : written.images)
        images.push_back(*image);
    return parseFlow(reread, images);
}

// A document with every kind of node and edge feature.
FlowDocument richDocument() {
    FlowDocument doc;
    Style plain;
    Style rich;
    rich.fill = Rgba8{200, 220, 255, 255};
    rich.stroke = kInk;
    rich.textColor = kTeal;
    rich.strokeWidth = 3.5f;
    rich.dash = Dash::Dashed;
    rich.cornerRadius = 21.25f;
    rich.fontSize = 22.5f;
    rich.align = TextAlign::Left;
    rich.valign = VAlign::Bottom;

    const NodeId rect = doc.addNode(NodeKind::Rectangle, Rect{-120.5f, 30.25f, 160, 80}, "Start\nsecond line", plain);
    const NodeId rounded = doc.addNode(NodeKind::Rounded, Rect{100, 40, 150.75f, 70}, "Rounded", rich);
    doc.addNode(NodeKind::Ellipse, Rect{300, 40, 120, 80}, "Ellipse");
    const NodeId diamond = doc.addNode(NodeKind::Diamond, Rect{300, 200, 140, 100}, "Yes or no?");
    doc.addNode(NodeKind::Parallelogram, Rect{500, 40, 160, 80}, "Input");
    doc.addNode(NodeKind::Terminator, Rect{500, 200, 160, 60}, "End");
    doc.addNode(NodeKind::Document, Rect{700, 40, 140, 90}, "Report");
    doc.addNode(NodeKind::Database, Rect{700, 200, 100, 120}, "Store");
    doc.addNode(NodeKind::Hexagon, Rect{860, 40, 140, 80}, "Prepare");
    doc.addNode(NodeKind::Circle, Rect{860, 200, 60, 60}, "A");
    const NodeId sticky = doc.addNode(NodeKind::Sticky, Rect{1000, 40, 120, 120}, "Note", rich);
    doc.addNode(NodeKind::Text, Rect{1000, 200, 100, 30}, "Free text");
    const NodeId polygon = doc.addNode(NodeKind::Polygon, Rect{1160, 40, 100, 100}, "Seven", plain, 7);

    const NodeId picture = doc.addNode(NodeKind::Image, Rect{-300, 200, 96, 64});
    const NodeId pictureTwin = doc.addNode(NodeKind::Image, Rect{-300, 300, 96, 64}); // shares picture 1
    const NodeId pictureOther = doc.addNode(NodeKind::Image, Rect{-300, 400, 48, 32});
    const ImageId first = doc.addImage(patternImage(3, 2, 10));
    const ImageId second = doc.addImage(patternImage(5, 4, 90));
    doc.setNodeImage(picture, first);
    doc.setNodeImage(pictureTwin, first);
    doc.setNodeImage(pictureOther, second);

    const NodeId lane = doc.addNode(NodeKind::Container, Rect{-400, 500, 900, 400}, "Lane");
    const NodeId childA = doc.addNode(NodeKind::Rectangle, Rect{-350, 560, 100, 60}, "Child A");
    const NodeId childB = doc.addNode(NodeKind::Rounded, Rect{-200, 560, 100, 60}, "Child B");
    const NodeId nested = doc.addNode(NodeKind::Container, Rect{-350, 650, 300, 200}, "Nested");
    const NodeId deep = doc.addNode(NodeKind::Circle, Rect{-300, 700, 50, 50}, "Deep");
    doc.reparent(childA, lane);
    doc.reparent(childB, lane);
    doc.reparent(nested, lane);
    doc.reparent(deep, nested);

    const NodeId groupMembers[] = {rect, rounded, polygon};
    doc.group(groupMembers);
    const NodeId otherGroup[] = {childA, childB};
    doc.group(otherGroup);
    const NodeId toFront[] = {rect}; // paint order is no longer creation order
    doc.bringToFront(toFront);
    const NodeId toBack[] = {diamond};
    doc.sendToBack(toBack);

    // Edges: each route and arrow, label placements, styles and free ends.
    const EdgeId e1 = doc.connect(End{rect, Face::Right, {}}, End{rounded, Face::Left, {}});
    doc.setEdgeRoute(e1, Route::Elbow, Arrow::None, Arrow::Open);
    doc.setEdgeLabel(e1, "yes, \"quoted\"");
    doc.setEdgeLabelPlacement(e1, 0.25f, LabelSide::Above, true);
    const EdgeId e2 = doc.connect(End{rounded, Face::Auto, {}}, End{diamond, Face::Top, {}});
    doc.setEdgeRoute(e2, Route::Straight, Arrow::Dot, Arrow::Arrow);
    doc.setEdgeLabelPlacement(e2, 1.0f, LabelSide::Below, false);
    Style edgeStyle;
    edgeStyle.stroke = kRed;
    edgeStyle.textColor = kTeal;
    edgeStyle.fill = Rgba8{1, 2, 3, 4};
    edgeStyle.strokeWidth = 5.0f;
    edgeStyle.dash = Dash::Dotted;
    edgeStyle.fontSize = 11.0f;
    edgeStyle.cornerRadius = 33.0f;
    edgeStyle.align = TextAlign::Right;
    const EdgeId e3 = doc.connect(End{diamond, Face::Bottom, {}}, End{kNoNode, Face::Auto, Pt{412.5f, -0.1f}});
    doc.setEdgeStyle(e3, edgeStyle);
    doc.setEdgeLabel(e3, "free end");
    doc.setEdgeVia(e3, Pt{12.5f, -30.25f});
    const EdgeId e4 = doc.connect(End{kNoNode, Face::Auto, Pt{-10.0f, 20.0f}}, End{kNoNode, Face::Auto, Pt{90.0f, 20.0f}});
    doc.setEdgeRoute(e4, Route::Curved, Arrow::Arrow, Arrow::Arrow);
    const EdgeId e5 = doc.connect(End{sticky, Face::Left, {}}, End{sticky, Face::Right, {}}); // a self loop
    doc.setEdgeLabelPlacement(e5, 0.75f, LabelSide::On, true);
    const EdgeId e6 = doc.connect(End{kNoNode, Face::Auto, Pt{5.0f, 5.0f}}, End{picture, Face::Top, {}});
    (void)e6;
    return doc;
}

// A hand-built file: [board] and whatever sections the test adds.
struct Hand {
    IniFile ini;
    Hand() { ini.set("board", "version", "1"); }
    Hand& node(int id, const std::string& kind, const std::string& extra = {}) {
        const std::string section = "node." + std::to_string(id);
        ini.set(section, "kind", kind);
        addExtra(section, extra);
        return *this;
    }
    Hand& edge(int id, int from, int to, const std::string& extra = {}) {
        const std::string section = "edge." + std::to_string(id);
        ini.set(section, "from_node", std::to_string(from));
        ini.set(section, "to_node", std::to_string(to));
        addExtra(section, extra);
        return *this;
    }
    Hand& image(int id, int chunk) {
        ini.set("image." + std::to_string(id), "chunk", std::to_string(chunk));
        return *this;
    }
    Hand& set(const std::string& section, const std::string& key, const std::string& value) {
        ini.set(section, key, value);
        return *this;
    }
    FlowParse parse(std::vector<trace::Image> images = {}) const { return parseFlow(ini, images); }

private:
    // "key=value;key=value"
    void addExtra(const std::string& section, const std::string& extra) {
        std::size_t pos = 0;
        while (pos < extra.size()) {
            std::size_t end = extra.find(';', pos);
            if (end == std::string::npos)
                end = extra.size();
            const std::string item = extra.substr(pos, end - pos);
            const std::size_t eq = item.find('=');
            if (eq != std::string::npos)
                ini.set(section, item.substr(0, eq), item.substr(eq + 1));
            pos = end + 1;
        }
    }
};

const Node* nodeWithText(const FlowDocument& doc, const std::string& text) {
    for (const Node& n : doc.nodes()) {
        if (n.text == text)
            return &n;
    }
    return nullptr;
}

} // namespace

// --- round trip --------------------------------------------------------------------------------------------

TEST_CASE("A rich document survives a round trip through INI text") {
    const FlowDocument doc = richDocument();
    REQUIRE(validate(doc).empty());
    const FlowParse back = roundTrip(doc);
    REQUIRE(back.status == FlowParseStatus::Ok);
    CHECK(back.repaired == 0);
    CHECK_EQ(validate(back.doc), "");
    CHECK_EQ(diff(doc, back.doc), "");
}

TEST_CASE("Every node kind comes back as the same kind, with polygon sides") {
    const FlowDocument doc = richDocument();
    const FlowParse back = roundTrip(doc);
    std::set<NodeKind> kinds;
    for (const Node& n : back.doc.nodes())
        kinds.insert(n.kind);
    CHECK(kinds.size() == static_cast<std::size_t>(kNodeKindCount));
    const Node* polygon = nodeWithText(back.doc, "Seven");
    REQUIRE(polygon);
    CHECK(polygon->kind == NodeKind::Polygon);
    CHECK(polygon->sides == 7);
}

TEST_CASE("Styles keep Auto colors Auto and explicit colors exact") {
    FlowDocument doc;
    Style custom;
    custom.fill = Rgba8{1, 2, 3, 4};
    custom.stroke = Rgba8{255, 254, 253, 252};
    // textColor stays Auto
    doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 50}, "custom", custom);
    doc.addNode(NodeKind::Rectangle, Rect{0, 100, 100, 50}, "auto");
    const FlowParse back = roundTrip(doc);
    const Node* a = nodeWithText(back.doc, "custom");
    const Node* b = nodeWithText(back.doc, "auto");
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->style.fill == std::optional<Rgba8>(Rgba8{1, 2, 3, 4}));
    CHECK(a->style.stroke == std::optional<Rgba8>(Rgba8{255, 254, 253, 252}));
    CHECK_FALSE(a->style.textColor.has_value());
    CHECK_FALSE(b->style.fill.has_value());
    CHECK_FALSE(b->style.stroke.has_value());
    CHECK_FALSE(b->style.textColor.has_value());
}

TEST_CASE("Z-order, groups, parents and images keep their structure") {
    const FlowDocument doc = richDocument();
    const FlowParse back = roundTrip(doc);
    const std::vector<NodeId> before = doc.nodesByZ();
    const std::vector<NodeId> after = back.doc.nodesByZ();
    REQUIRE(before.size() == after.size());
    for (std::size_t i = 0; i < before.size(); ++i)
        CHECK(doc.node(before[i])->text == back.doc.node(after[i])->text);

    // The two shared-picture nodes still share one picture; the third has its own.
    const auto imageOf = [&](const FlowDocument& d, NodeId id) { return d.node(id)->imageId; };
    std::vector<ImageId> imageNodes;
    for (const NodeId id : after) {
        if (back.doc.node(id)->kind == NodeKind::Image)
            imageNodes.push_back(imageOf(back.doc, id));
    }
    REQUIRE(imageNodes.size() == 3);
    CHECK(imageNodes[0] != kNoImage);
    CHECK(std::count(imageNodes.begin(), imageNodes.end(), imageNodes[0]) == 2);

    const Node* lane = nodeWithText(back.doc, "Lane");
    const Node* nested = nodeWithText(back.doc, "Nested");
    const Node* deep = nodeWithText(back.doc, "Deep");
    REQUIRE(lane);
    REQUIRE(nested);
    REQUIRE(deep);
    CHECK(nested->parentId == lane->id);
    CHECK(deep->parentId == nested->id);
    CHECK(back.doc.groupMembers(nodeWithText(back.doc, "Start\nsecond line")->groupId).size() == 3);
}

TEST_CASE("A free-ended edge keeps its points, and an empty board round trips") {
    FlowDocument doc;
    doc.connect(End{kNoNode, Face::Auto, Pt{1.5f, -2.5f}}, End{kNoNode, Face::Auto, Pt{300.25f, 40.0f}});
    const FlowParse back = roundTrip(doc);
    REQUIRE(back.doc.edges().size() == 1);
    const Edge& e = back.doc.edges()[0];
    CHECK(e.from.node == kNoNode);
    CHECK(e.from.point == Pt{1.5f, -2.5f});
    CHECK(e.to.point == Pt{300.25f, 40.0f});

    const FlowParse empty = roundTrip(FlowDocument{});
    CHECK(empty.status == FlowParseStatus::Ok);
    CHECK(empty.doc.empty());
    CHECK(empty.repaired == 0);
}

TEST_CASE("Text escaping keeps every awkward string exact") {
    const std::string control = std::string("a") + '\x01' + "b" + '\x7f' + "c";
    const std::vector<std::string> texts = {
        "line one\nline two\n\nline four",
        "back\\slash and \\n written out and \\x41 and \\s",
        "ends with a backslash\\",
        "\\",
        "he said \"hi\" and 'bye'",
        "key=value ; not a comment # nor this",
        "[not a section]",
        " leading space",
        "trailing space ",
        " ",
        "   ",
        "tab\there",
        "windows\r\nbreaks\rhere",
        "h\xC3\xA9llo \xE2\x80\x94 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80",
        control,
        std::string("nul") + '\0' + "inside",
        "= starts with equals",
    };
    FlowDocument doc;
    float y = 0.0f;
    for (const std::string& text : texts) {
        const NodeId id = doc.addNode(NodeKind::Rectangle, Rect{0, y, 100, 40}, text);
        const EdgeId e = doc.connect(End{id, Face::Auto, {}}, End{kNoNode, Face::Auto, Pt{500, y}});
        doc.setEdgeLabel(e, text);
        y += 50.0f;
    }
    const FlowParse back = roundTrip(doc);
    REQUIRE(back.doc.nodes().size() == texts.size());
    for (std::size_t i = 0; i < texts.size(); ++i) {
        CAPTURE(i);
        CHECK(back.doc.nodes()[i].text == texts[i]);
        CHECK(back.doc.edges()[i].label == texts[i]);
    }
    CHECK_EQ(diff(doc, back.doc), "");
}

TEST_CASE("Written text is plain lines: no raw line break ever lands inside a value") {
    FlowDocument doc;
    doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 40}, "a\nb\r\nc");
    const std::string text = serializeFlow(doc).ini.serialize();
    CHECK(text.find("a\nb") == std::string::npos);
    CHECK(text.find('\r') == std::string::npos);
    CHECK(text.find("text = a\\nb\\r\\nc") != std::string::npos);
}

TEST_CASE("Enums are written by name") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Hexagon, Rect{0, 0, 100, 40});
    const NodeId b = doc.addNode(NodeKind::Rectangle, Rect{200, 0, 100, 40});
    const EdgeId e = doc.connect(End{a, Face::Left, {}}, End{b, Face::Top, {}});
    doc.setEdgeRoute(e, Route::Elbow, Arrow::Dot, Arrow::Open);
    doc.setEdgeLabelPlacement(e, 0.5f, LabelSide::Below, false);
    const IniFile ini = serializeFlow(doc).ini;
    REQUIRE(ini.get("node.1", "kind"));
    CHECK(*ini.get("node.1", "kind") == "hexagon");
    CHECK(*ini.get("edge.1", "route") == "elbow");
    CHECK(*ini.get("edge.1", "arrow_start") == "dot");
    CHECK(*ini.get("edge.1", "arrow_end") == "open");
    CHECK(*ini.get("edge.1", "from_face") == "left");
    CHECK(*ini.get("edge.1", "to_face") == "top");
    CHECK(*ini.get("edge.1", "label_side") == "below");
    CHECK(*ini.get("board", "version") == "1");
    CHECK(*ini.get("node.1", "dash") == "solid");
}

TEST_CASE("Output is deterministic and canonical") {
    const FlowDocument doc = richDocument();
    const std::string once = serializeFlow(doc).ini.serialize();
    CHECK(once == serializeFlow(doc).ini.serialize());

    // Reading and writing again changes nothing, even though the loaded ids are new.
    const FlowParse back = roundTrip(doc);
    CHECK(serializeFlow(back.doc).ini.serialize() == once);

    // A document whose ids are sparse (nodes removed) writes the same text as a dense twin.
    FlowDocument sparse;
    const NodeId gone = sparse.addNode(NodeKind::Rectangle, Rect{0, 0, 50, 50}, "gone");
    const NodeId keepA = sparse.addNode(NodeKind::Ellipse, Rect{100, 0, 50, 50}, "a");
    const NodeId keepB = sparse.addNode(NodeKind::Diamond, Rect{200, 0, 50, 50}, "b");
    sparse.connect(End{keepA, Face::Auto, {}}, End{keepB, Face::Auto, {}});
    const NodeId doomed[] = {gone};
    sparse.removeNodes(doomed);
    FlowDocument dense;
    const NodeId da = dense.addNode(NodeKind::Ellipse, Rect{100, 0, 50, 50}, "a");
    const NodeId db = dense.addNode(NodeKind::Diamond, Rect{200, 0, 50, 50}, "b");
    dense.connect(End{da, Face::Auto, {}}, End{db, Face::Auto, {}});
    CHECK(serializeFlow(sparse).ini.serialize() == serializeFlow(dense).ini.serialize());
}

TEST_CASE("Only the pictures nodes use are written, in order of first use") {
    FlowDocument doc;
    const ImageId unused = doc.addImage(patternImage(2, 2, 5));
    (void)unused;
    const ImageId used = doc.addImage(patternImage(3, 3, 6));
    const NodeId n = doc.addNode(NodeKind::Image, Rect{0, 0, 60, 60});
    doc.setNodeImage(n, used);
    const FlowSerialized written = serializeFlow(doc);
    REQUIRE(written.images.size() == 1);
    CHECK(written.images[0]->width() == 3);
    REQUIRE(written.ini.get("image.0", "chunk"));
    CHECK(*written.ini.get("image.0", "chunk") == "0");
    CHECK(*written.ini.get("node.1", "image") == "0");
}

// --- repairs -----------------------------------------------------------------------------------------------

TEST_CASE("A missing board or a newer version is refused") {
    IniFile none;
    none.set("node.1", "kind", "rectangle");
    std::vector<trace::Image> images;
    CHECK(parseFlow(none, images).status == FlowParseStatus::NotABoard);
    CHECK(parseFlow(IniFile{}, images).status == FlowParseStatus::NotABoard);

    Hand future;
    future.set("board", "version", "2");
    future.node(1, "rectangle");
    const FlowParse parsed = future.parse();
    CHECK(parsed.status == FlowParseStatus::TooNew);
    CHECK(parsed.doc.empty());

    Hand odd; // a version nobody understands reads as the current one
    odd.set("board", "version", "banana");
    odd.node(1, "rectangle");
    CHECK(odd.parse().status == FlowParseStatus::Ok);
}

TEST_CASE("Unknown kinds are skipped, and their edges go with them") {
    Hand h;
    h.node(1, "rectangle", "text=keep");
    h.node(2, "hologram");
    h.node(3, "ellipse", "text=also keep");
    h.node(4, "");
    h.edge(1, 1, 3);
    h.edge(2, 1, 2); // to the skipped one
    const FlowParse parsed = h.parse();
    REQUIRE(parsed.status == FlowParseStatus::Ok);
    CHECK(parsed.doc.nodes().size() == 2);
    CHECK(parsed.doc.edges().size() == 1);
    CHECK(parsed.repaired >= 3); // two nodes and one edge
    CHECK_EQ(validate(parsed.doc), "");
}

TEST_CASE("Edges to missing nodes are dropped; a free end needs no node") {
    Hand h;
    h.node(1, "rectangle");
    h.edge(1, 1, 99);
    h.edge(2, 77, 1);
    h.set("edge.3", "from_node", "oops");
    h.set("edge.3", "to_node", "1");
    h.set("edge.4", "to_node", "1"); // no from_node: a free start at the origin
    h.set("edge.4", "from_x", "12");
    h.set("edge.4", "from_y", "34");
    h.set("edge.5", "from_node", "0"); // zero means free as well
    h.set("edge.5", "to_node", "1");
    const FlowParse parsed = h.parse();
    REQUIRE(parsed.doc.edges().size() == 2);
    CHECK(parsed.repaired == 3);
    const Edge& free = parsed.doc.edges()[0];
    CHECK(free.from.node == kNoNode);
    CHECK(free.from.point == Pt{12.0f, 34.0f});
    CHECK(free.to.node != kNoNode);
    CHECK_EQ(validate(parsed.doc), "");
}

TEST_CASE("Duplicate ids keep the first") {
    Hand h;
    h.node(1, "rectangle", "text=first");
    h.set("node.01", "kind", "ellipse"); // the same id written another way
    h.set("node.01", "text", "second");
    h.set("edge.7", "from_x", "0");
    h.set("edge.7", "to_x", "10");
    h.set("edge.007", "from_x", "0");
    h.set("edge.007", "to_x", "99");
    const FlowParse parsed = h.parse();
    REQUIRE(parsed.doc.nodes().size() == 1);
    CHECK(parsed.doc.nodes()[0].text == "first");
    REQUIRE(parsed.doc.edges().size() == 1);
    CHECK(parsed.doc.edges()[0].to.point.x == 10.0f);
    CHECK(parsed.repaired >= 2);
}

TEST_CASE("Entries with an unusable id are ignored and counted") {
    Hand h;
    h.node(1, "rectangle");
    h.set("node.zero", "kind", "rectangle");
    h.set("node.0", "kind", "rectangle");
    h.set("node.", "kind", "rectangle");
    h.set("node.-4", "kind", "rectangle");
    h.set("node.99999999999999999999", "kind", "rectangle");
    h.set("something.else", "kind", "rectangle"); // not an entry at all: ignored without a count
    const FlowParse parsed = h.parse();
    CHECK(parsed.doc.nodes().size() == 1);
    CHECK(parsed.repaired == 5);
}

TEST_CASE("Parent cycles are broken and a non-container parent is dropped") {
    Hand h;
    h.node(1, "container", "parent=2");
    h.node(2, "container", "parent=3");
    h.node(3, "container", "parent=1"); // 1 -> 2 -> 3 -> 1
    h.node(4, "container", "parent=4"); // its own parent
    h.node(5, "rectangle");
    h.node(6, "rectangle", "parent=5"); // a rectangle is not a container
    h.node(7, "rectangle", "parent=42"); // no such node
    h.node(8, "rectangle", "parent=1");
    const FlowParse parsed = h.parse();
    CHECK_EQ(validate(parsed.doc), "");
    CHECK(parsed.doc.nodes().size() == 8);
    CHECK(parsed.repaired >= 4);
    // The legitimate parent link survives.
    int withParent = 0;
    for (const Node& n : parsed.doc.nodes())
        withParent += n.parentId != kNoNode ? 1 : 0;
    CHECK(withParent >= 3); // two links of the cycle, plus node 8
}

TEST_CASE("Groups of fewer than two members dissolve") {
    Hand h;
    h.node(1, "rectangle", "group=1");
    h.node(2, "rectangle", "group=1");
    h.node(3, "rectangle", "group=2"); // alone
    h.node(4, "rectangle", "group=3");
    h.node(5, "hologram", "group=3"); // its partner is skipped
    h.node(6, "rectangle", "group=0");
    const FlowParse parsed = h.parse();
    CHECK_EQ(validate(parsed.doc), "");
    std::map<GroupId, int> groups;
    for (const Node& n : parsed.doc.nodes()) {
        if (n.groupId != kNoGroup)
            ++groups[n.groupId];
    }
    REQUIRE(groups.size() == 1);
    CHECK(groups.begin()->second == 2);
    CHECK(parsed.repaired >= 3); // the lone group, the half group, the hologram
}

TEST_CASE("A bad picture index leaves the node without a picture") {
    Hand h;
    h.node(1, "image", "image=0");  // fine
    h.node(2, "image", "image=7");  // no such entry
    h.node(3, "image", "image=x");  // not a number
    h.node(4, "image", "image=1");  // entry points at a chunk that is not there
    h.node(5, "image", "image=2");  // chunk could not be decoded
    h.node(6, "image");             // never had one
    h.image(0, 0).image(1, 9).image(2, 1);
    std::vector<trace::Image> images;
    images.push_back(*patternImage(2, 2, 1));
    images.push_back(trace::Image{}); // undecodable: an empty Image
    const FlowParse parsed = h.parse(std::move(images));
    REQUIRE(parsed.doc.nodes().size() == 6);
    CHECK_EQ(validate(parsed.doc), "");
    const auto& nodes = parsed.doc.nodes();
    CHECK(nodes[0].imageId != kNoImage);
    CHECK(nodes[1].imageId == kNoImage);
    CHECK(nodes[2].imageId == kNoImage);
    CHECK(nodes[3].imageId == kNoImage);
    CHECK(nodes[4].imageId == kNoImage);
    CHECK(nodes[5].imageId == kNoImage);
    CHECK(parsed.repaired == 3); // the undecodable chunk is counted by the project reader, not here
}

TEST_CASE("An image key on a node that is not an Image node is ignored") {
    Hand h;
    h.node(1, "rectangle", "image=0");
    h.image(0, 0);
    std::vector<trace::Image> images;
    images.push_back(*patternImage(2, 2, 1));
    const FlowParse parsed = h.parse(std::move(images));
    REQUIRE(parsed.doc.nodes().size() == 1);
    CHECK(parsed.doc.nodes()[0].imageId == kNoImage);
}

TEST_CASE("Several entries may name the same chunk, and the picture is shared") {
    Hand h;
    h.node(1, "image", "image=0");
    h.node(2, "image", "image=1");
    h.image(0, 0).image(1, 0);
    std::vector<trace::Image> images;
    images.push_back(*patternImage(2, 2, 1));
    const FlowParse parsed = h.parse(std::move(images));
    REQUIRE(parsed.doc.nodes().size() == 2);
    CHECK(parsed.doc.nodes()[0].imageId != kNoImage);
    CHECK(parsed.doc.nodes()[0].imageId == parsed.doc.nodes()[1].imageId);
    CHECK(parsed.repaired == 0);
}

TEST_CASE("NaN, infinity and huge numbers are sanitised") {
    Hand h;
    h.node(1, "rectangle", "x=nan;y=inf;w=nan;h=-inf");
    h.node(2, "rectangle", "x=1e30;y=-1e30;w=1e30;h=-5");
    h.node(3, "rectangle", "x=0x1p3;y=banana;w=0;h=1e-45");
    h.node(4, "rectangle", "x=5;y=6;w=7;h=9;stroke_width=nan;font_size=1e9;corner_radius=-4");
    h.edge(1, 1, 2, "from_x=nan");
    h.set("edge.2", "from_x", "inf");
    h.set("edge.2", "from_y", "-1e40");
    h.set("edge.2", "to_x", "nan");
    h.set("edge.2", "to_y", "1e7");
    const FlowParse parsed = h.parse();
    CHECK_EQ(validate(parsed.doc), "");
    REQUIRE(parsed.doc.nodes().size() == 4);
    const auto& nodes = parsed.doc.nodes();
    CHECK(nodes[0].rect.x == 0.0f);
    CHECK(nodes[0].rect.y == kMaxCoord);
    CHECK(nodes[0].rect.w == 160.0f); // NaN size takes the default
    CHECK(nodes[0].rect.h == kMinNodeSize);
    CHECK(nodes[1].rect.x == kMaxCoord);
    CHECK(nodes[1].rect.y == -kMaxCoord);
    CHECK(nodes[1].rect.w == kMaxCoord);
    CHECK(nodes[1].rect.h == kMinNodeSize);
    CHECK(nodes[2].rect.w == kMinNodeSize);
    CHECK(nodes[3].rect.w == kMinNodeSize); // 7 is under the minimum
    CHECK(nodes[3].style.strokeWidth == Style{}.strokeWidth);
    CHECK(nodes[3].style.fontSize == 200.0f);
    CHECK(nodes[3].style.cornerRadius == 0.0f);
    REQUIRE(parsed.doc.edges().size() == 2);
    CHECK(parsed.doc.edges()[1].from.point.x == kMaxCoord);
    CHECK(parsed.doc.edges()[1].from.point.y == -kMaxCoord);
    CHECK(parsed.doc.edges()[1].to.point.x == 0.0f);
    CHECK(parsed.doc.edges()[1].to.point.y == kMaxCoord);
}

TEST_CASE("Sides and label position are clamped") {
    Hand h;
    h.node(1, "polygon", "sides=1000");
    h.node(2, "polygon", "sides=-5");
    h.node(3, "polygon", "sides=abc");
    h.node(4, "polygon", "sides=12");
    h.edge(1, 1, 2, "label_pos=5");
    h.edge(2, 1, 2, "label_pos=-1");
    h.edge(3, 1, 2, "label_pos=nan");
    h.edge(4, 1, 2, "label_pos=0.3");
    const FlowParse parsed = h.parse();
    const auto& nodes = parsed.doc.nodes();
    REQUIRE(nodes.size() == 4);
    CHECK(nodes[0].sides == kMaxPolygonSides);
    CHECK(nodes[1].sides == kMinPolygonSides);
    CHECK(nodes[2].sides == kDefaultPolygonSides);
    CHECK(nodes[3].sides == 12);
    const auto& edges = parsed.doc.edges();
    REQUIRE(edges.size() == 4);
    CHECK(edges[0].labelPos == 1.0f);
    CHECK(edges[1].labelPos == 0.0f);
    CHECK(edges[2].labelPos == 0.5f);
    CHECK(edges[3].labelPos == 0.3f);
}

TEST_CASE("Missing optional keys take their defaults") {
    Hand h;
    h.node(1, "rectangle");
    h.node(2, "rectangle", "x=10");
    h.set("edge.1", "from_node", "1");
    h.set("edge.1", "to_node", "2");
    const FlowParse parsed = h.parse();
    REQUIRE(parsed.doc.nodes().size() == 2);
    const Node& n = parsed.doc.nodes()[0];
    CHECK(n.rect == Rect{0, 0, 160, 80});
    CHECK(n.text.empty());
    CHECK(n.style == Style{});
    CHECK(n.sides == kDefaultPolygonSides);
    CHECK(n.parentId == kNoNode);
    CHECK(n.groupId == kNoGroup);
    CHECK(parsed.doc.nodes()[1].rect.x == 10.0f);
    REQUIRE(parsed.doc.edges().size() == 1);
    const Edge& e = parsed.doc.edges()[0];
    CHECK(e.route == Route::Curved);
    CHECK(e.arrowStart == Arrow::None);
    CHECK(e.arrowEnd == Arrow::Arrow);
    CHECK(e.labelPos == 0.5f);
    CHECK(e.labelSide == LabelSide::On);
    CHECK_FALSE(e.labelAlong);
    CHECK(e.from.face == Face::Auto);
    CHECK(parsed.repaired == 0);
}

TEST_CASE("Unknown enum names and colors fall back to defaults without dropping the entry") {
    Hand h;
    h.node(1, "rectangle", "dash=wavy;align=justify;fill=zzzzzz;stroke=#FF0000;text_color=auto");
    h.node(2, "rectangle");
    h.edge(1, 1, 2, "route=teleport;arrow_end=spear;label_side=sideways;from_face=middle");
    const FlowParse parsed = h.parse();
    REQUIRE(parsed.doc.nodes().size() == 2);
    const Node& n = parsed.doc.nodes()[0];
    CHECK(n.style.dash == Dash::Solid);
    CHECK(n.style.align == TextAlign::Center);
    CHECK_FALSE(n.style.fill.has_value());
    CHECK(n.style.stroke == std::optional<Rgba8>(Rgba8{255, 0, 0, 255}));
    CHECK_FALSE(n.style.textColor.has_value());
    REQUIRE(parsed.doc.edges().size() == 1);
    CHECK(parsed.doc.edges()[0].route == Route::Curved);
    CHECK(parsed.doc.edges()[0].arrowEnd == Arrow::Arrow);
    CHECK(parsed.doc.edges()[0].from.face == Face::Auto);
}

TEST_CASE("Paint order follows z, not the order of the sections") {
    Hand h;
    h.node(5, "rectangle", "text=top;z=30");
    h.node(2, "rectangle", "text=bottom;z=-4");
    h.node(9, "rectangle", "text=middle;z=7");
    h.node(1, "rectangle", "text=tie a;z=7");
    const FlowParse parsed = h.parse();
    const std::vector<NodeId> order = parsed.doc.nodesByZ();
    REQUIRE(order.size() == 4);
    CHECK(parsed.doc.node(order[0])->text == "bottom");
    CHECK(parsed.doc.node(order[1])->text == "middle"); // the tie keeps file order
    CHECK(parsed.doc.node(order[2])->text == "tie a");
    CHECK(parsed.doc.node(order[3])->text == "top");
}

TEST_CASE("Text and labels are cut to the limit without splitting a character") {
    Hand h;
    std::string huge;
    for (std::size_t i = 0; i < kMaxTextBytes; ++i)
        huge += "\xC3\xA9"; // 2 bytes each: 32 KiB of text
    h.node(1, "rectangle");
    h.set("node.1", "text", huge);
    h.node(2, "rectangle");
    h.edge(1, 1, 2);
    h.set("edge.1", "label", huge);
    const FlowParse parsed = h.parse();
    REQUIRE(parsed.doc.nodes().size() == 2);
    const std::string& text = parsed.doc.nodes()[0].text;
    CHECK(text.size() <= kMaxTextBytes);
    CHECK(text.size() > kMaxTextBytes - 4);
    CHECK(text.size() % 2 == 0); // never half of a two-byte character
    REQUIRE(parsed.doc.edges().size() == 1);
    CHECK(parsed.doc.edges()[0].label.size() <= kMaxTextBytes);
}

TEST_CASE("Counts are capped at the document limits") {
    Hand h;
    for (std::size_t i = 1; i <= kMaxNodes + 5; ++i)
        h.set("node." + std::to_string(i), "kind", "rectangle");
    const FlowParse nodes = h.parse();
    CHECK(nodes.doc.nodes().size() == kMaxNodes);
    CHECK(nodes.repaired == 5);
    CHECK_EQ(validate(nodes.doc), "");

    Hand e;
    e.node(1, "rectangle").node(2, "rectangle");
    for (std::size_t i = 1; i <= kMaxEdges + 5; ++i)
        e.edge(static_cast<int>(i), 1, 2);
    const FlowParse edges = e.parse();
    CHECK(edges.doc.edges().size() == kMaxEdges);
    CHECK(edges.repaired == 5);

    Hand p;
    std::vector<trace::Image> images;
    for (std::size_t i = 0; i < kMaxImages + 10; ++i) {
        p.node(static_cast<int>(i) + 1, "image", "image=" + std::to_string(i));
        p.image(static_cast<int>(i), static_cast<int>(i));
        images.push_back(*patternImage(1, 1, static_cast<std::uint8_t>(i)));
    }
    const FlowParse pictures = p.parse(std::move(images));
    int withPicture = 0;
    for (const Node& n : pictures.doc.nodes())
        withPicture += n.imageId != kNoImage ? 1 : 0;
    CHECK(withPicture == static_cast<int>(kMaxImages));
    CHECK(pictures.doc.nodes().size() == kMaxImages + 10);
    CHECK_EQ(validate(pictures.doc), "");
}

// --- hostile corpus ----------------------------------------------------------------------------------------

namespace {

FlowDocument smallDocument() {
    FlowDocument doc;
    Style style;
    style.fill = kTeal;
    const NodeId container = doc.addNode(NodeKind::Container, Rect{0, 0, 400, 300}, "Lane");
    const NodeId a = doc.addNode(NodeKind::Rounded, Rect{20, 40, 100, 50}, "A\nline", style);
    const NodeId b = doc.addNode(NodeKind::Polygon, Rect{200, 40, 80, 80}, "B", {}, 6);
    const NodeId picture = doc.addNode(NodeKind::Image, Rect{20, 150, 64, 64});
    doc.setNodeImage(picture, doc.addImage(patternImage(2, 2, 40)));
    doc.reparent(a, container);
    doc.reparent(b, container);
    const NodeId pair[] = {a, b};
    doc.group(pair);
    const EdgeId e = doc.connect(End{a, Face::Right, {}}, End{b, Face::Left, {}});
    doc.setEdgeLabel(e, "go");
    doc.setEdgeRoute(e, Route::Elbow, Arrow::None, Arrow::Open);
    doc.connect(End{b, Face::Bottom, {}}, End{kNoNode, Face::Auto, Pt{240, 260}});
    return doc;
}

// Loads bytes as a project: "" when it loaded a valid document, an error otherwise; a failure of the checks is a
// returned message too.
std::string loadBytes(const std::vector<unsigned char>& bytes) {
    trace::ProjectReadResult read = trace::readProject(bytes, "hostile.tflow");
    if (!read.project)
        return read.error.empty() ? "no project and no message" : std::string();
    trace::FlowProjectLoad loaded = trace::flowProjectToDocument(std::move(*read.project), "hostile.tflow");
    if (!loaded.doc)
        return loaded.error.empty() ? "no document and no message" : std::string();
    return validate(*loaded.doc);
}

} // namespace

TEST_CASE("Truncating a project file at every length never crashes or yields a bad document") {
    const trace::ProjectWriteResult written = trace::writeProject(trace::flowProjectData(smallDocument()));
    REQUIRE(!written.bytes.empty());
    REQUIRE_EQ(loadBytes(written.bytes), "");
    for (std::size_t length = 0; length < written.bytes.size(); ++length) {
        const std::vector<unsigned char> cut(written.bytes.begin(), written.bytes.begin() + static_cast<std::ptrdiff_t>(length));
        const std::string problem = loadBytes(cut);
        if (!problem.empty()) {
            CAPTURE(length);
            FAIL(problem);
        }
    }
}

TEST_CASE("Random byte changes in a project file never crash or yield a bad document") {
    const trace::ProjectWriteResult written = trace::writeProject(trace::flowProjectData(smallDocument()));
    REQUIRE(!written.bytes.empty());
    std::mt19937 rng(0xF10F1u);
    for (int round = 0; round < 3000; ++round) {
        std::vector<unsigned char> bytes = written.bytes;
        const int flips = 1 + static_cast<int>(rng() % 4);
        for (int i = 0; i < flips; ++i)
            bytes[rng() % bytes.size()] = static_cast<unsigned char>(rng());
        const std::string problem = loadBytes(bytes);
        if (!problem.empty()) {
            CAPTURE(round);
            FAIL(problem);
        }
    }
}

TEST_CASE("Damaged INI text of a board never yields a bad document") {
    const FlowSerialized written = serializeFlow(smallDocument());
    const std::string text = written.ini.serialize();
    std::vector<trace::Image> pictures;
    for (const ImagePtr& image : written.images)
        pictures.push_back(*image);
    const auto load = [&](const std::string& source) {
        std::vector<trace::Image> images = pictures;
        const FlowParse parsed = parseFlow(IniFile::parse(source), images);
        return parsed.status == FlowParseStatus::Ok ? validate(parsed.doc) : std::string();
    };
    REQUIRE_EQ(load(text), "");
    for (std::size_t length = 0; length <= text.size(); ++length) {
        const std::string problem = load(text.substr(0, length));
        if (!problem.empty()) {
            CAPTURE(length);
            FAIL(problem);
        }
    }
    std::mt19937 rng(0xB0A2Du);
    static const char kNasty[] = {'=', '[', ']', '\n', ';', '#', '\\', '-', '9', 'n', 'x', '0', '.', ' ', '\0', '\xFF'};
    for (int round = 0; round < 6000; ++round) {
        std::string damaged = text;
        const int edits = 1 + static_cast<int>(rng() % 5);
        for (int i = 0; i < edits; ++i) {
            const std::size_t at = rng() % damaged.size();
            if (rng() % 3 == 0)
                damaged[at] = kNasty[rng() % sizeof(kNasty)];
            else if (rng() % 2 == 0)
                damaged[at] = static_cast<char>(rng());
            else
                damaged.erase(at, 1 + rng() % 6);
            if (damaged.empty())
                damaged = "x";
        }
        const std::string problem = load(damaged);
        if (!problem.empty()) {
            CAPTURE(round);
            CAPTURE(damaged);
            FAIL(problem);
        }
    }
}

// --- project files -----------------------------------------------------------------------------------------

TEST_CASE("A flow project saves to a file and loads back the same") {
    TempDir dir;
    const FlowDocument doc = richDocument();
    const fs::path path = dir.path / "board.tflow";
    CHECK(trace::saveFlowProject(path, doc).empty());
    CHECK(trace::sniffProjectKind(path) == std::optional<std::uint32_t>(trace::kProjectFlow));

    const trace::FlowProjectLoad loaded = trace::loadFlowProject(path);
    REQUIRE_MESSAGE(loaded.doc.has_value(), loaded.error);
    CHECK(loaded.repaired == 0);
    CHECK_EQ(validate(*loaded.doc), "");
    CHECK_EQ(diff(doc, *loaded.doc), "");

    // Saving over an existing file works, and the empty board is a valid project.
    CHECK(trace::saveFlowProject(path, FlowDocument{}).empty());
    const trace::FlowProjectLoad empty = trace::loadFlowProject(path);
    REQUIRE(empty.doc.has_value());
    CHECK(empty.doc->empty());
}

TEST_CASE("The project holds the pictures at their original size, one PNG chunk each") {
    const trace::ProjectData data = trace::flowProjectData(richDocument());
    CHECK(data.kind == trace::kProjectFlow);
    REQUIRE(data.images.size() == 2);
    CHECK(data.images[0].width() == 3);
    CHECK(data.images[0].height() == 2);
    CHECK(data.images[1].width() == 5);
    CHECK(data.images[1].height() == 4);
}

TEST_CASE("A Pixel Art project is refused with a clear message") {
    TempDir dir;
    const fs::path path = dir.path / "art.tpix";
    trace::Document art(4, 4);
    REQUIRE(trace::savePixelProject(path, art).empty());
    const trace::FlowProjectLoad loaded = trace::loadFlowProject(path);
    CHECK_FALSE(loaded.doc.has_value());
    CHECK(loaded.error.find("art.tpix") != std::string::npos);
    CHECK(loaded.error.find("Flowchart") != std::string::npos);
}

TEST_CASE("Missing, empty and unrelated files give an error message") {
    TempDir dir;
    CHECK_FALSE(trace::loadFlowProject(dir.path / "nope.tflow").doc.has_value());
    CHECK_FALSE(trace::loadFlowProject(dir.path / "nope.tflow").error.empty());

    const fs::path empty = dir.path / "empty.tflow";
    std::ofstream(empty, std::ios::binary).close();
    CHECK_FALSE(trace::loadFlowProject(empty).doc.has_value());
    CHECK_FALSE(trace::loadFlowProject(empty).error.empty());

    const fs::path junk = dir.path / "junk.tflow";
    {
        std::ofstream out(junk, std::ios::binary);
        out << "this is not a project file at all";
    }
    const trace::FlowProjectLoad loaded = trace::loadFlowProject(junk);
    CHECK_FALSE(loaded.doc.has_value());
    CHECK_FALSE(loaded.error.empty());
}

TEST_CASE("A project without a board section is refused; one from a newer version too") {
    trace::ProjectData data;
    data.kind = trace::kProjectFlow;
    data.meta.set("node.1", "kind", "rectangle");
    const trace::FlowProjectLoad noBoard = trace::flowProjectToDocument(data, "x.tflow");
    CHECK_FALSE(noBoard.doc.has_value());
    CHECK(noBoard.error.find("x.tflow") != std::string::npos);

    trace::ProjectData future;
    future.kind = trace::kProjectFlow;
    future.meta.set("board", "version", "99");
    const trace::FlowProjectLoad tooNew = trace::flowProjectToDocument(future, "y.tflow");
    CHECK_FALSE(tooNew.doc.has_value());
    CHECK(tooNew.error.find("newer") != std::string::npos);

    trace::ProjectData pixel;
    pixel.kind = trace::kProjectPixel;
    pixel.meta.set("board", "version", "1");
    CHECK_FALSE(trace::flowProjectToDocument(pixel, "z.tflow").doc.has_value());
}

TEST_CASE("Damage in the file is counted: repaired entries and undecodable pictures") {
    TempDir dir;
    FlowDocument doc;
    const NodeId picture = doc.addNode(NodeKind::Image, Rect{0, 0, 64, 64}, "pic");
    doc.setNodeImage(picture, doc.addImage(patternImage(4, 4, 70)));
    doc.addNode(NodeKind::Rectangle, Rect{100, 0, 80, 40}, "ok");

    // One bad entry in the META text...
    trace::ProjectData data = trace::flowProjectData(doc);
    data.meta.set("node.50", "kind", "hologram");
    trace::ProjectWriteResult written = trace::writeProject(data);
    REQUIRE(!written.bytes.empty());

    // ...and a picture whose PNG signature is ruined.
    const unsigned char signature[] = {0x89, 'P', 'N', 'G'};
    const auto at = std::search(written.bytes.begin(), written.bytes.end(), std::begin(signature), std::end(signature));
    REQUIRE(at != written.bytes.end());
    *at = 'X';

    const fs::path path = dir.path / "damaged.tflow";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(written.bytes.data()), static_cast<std::streamsize>(written.bytes.size()));
    }
    const trace::FlowProjectLoad loaded = trace::loadFlowProject(path);
    REQUIRE_MESSAGE(loaded.doc.has_value(), loaded.error);
    CHECK(loaded.repaired == 2);
    CHECK(loaded.doc->nodes().size() == 2);
    const Node* pic = nodeWithText(*loaded.doc, "pic");
    REQUIRE(pic);
    CHECK(pic->kind == NodeKind::Image);
    CHECK(pic->imageId == kNoImage); // the node stays, as a placeholder
    CHECK_EQ(validate(*loaded.doc), "");
}

// --- text placement and manual routes ----------------------------------------------------------------------

TEST_CASE("Vertical text alignment is written only when it is not the default, and comes back") {
    FlowDocument doc;
    Style top;
    top.valign = VAlign::Top;
    Style bottomRight;
    bottomRight.valign = VAlign::Bottom;
    bottomRight.align = TextAlign::Right;
    doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 50}, "plain");
    doc.addNode(NodeKind::Rectangle, Rect{200, 0, 100, 50}, "top", top);
    doc.addNode(NodeKind::Rectangle, Rect{400, 0, 100, 50}, "br", bottomRight);

    const IniFile ini = serializeFlow(doc).ini;
    const IniFile::Section* plain = ini.section("node.1");
    REQUIRE(plain != nullptr);
    for (const IniFile::Entry& entry : plain->entries)
        CHECK(entry.key != "valign"); // files of a plain document stay as they were before it existed

    const FlowParse back = roundTrip(doc);
    REQUIRE(back.status == FlowParseStatus::Ok);
    CHECK_EQ(diff(doc, back.doc), "");
    CHECK(nodeWithText(back.doc, "top")->style.valign == VAlign::Top);
    CHECK(nodeWithText(back.doc, "br")->style.valign == VAlign::Bottom);
    CHECK(nodeWithText(back.doc, "br")->style.align == TextAlign::Right);
    CHECK(nodeWithText(back.doc, "plain")->style.valign == VAlign::Middle);
}

TEST_CASE("A bad vertical alignment name falls back to the middle") {
    Hand h;
    h.node(1, "rectangle", "valign=sideways;align=right");
    const FlowParse parsed = h.parse();
    REQUIRE(parsed.doc.nodes().size() == 1);
    CHECK(parsed.doc.nodes()[0].style.valign == VAlign::Middle);
    CHECK(parsed.doc.nodes()[0].style.align == TextAlign::Right);
}

TEST_CASE("A manual route round trips, and files written before it read as automatic") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 50}, "a");
    const NodeId b = doc.addNode(NodeKind::Rectangle, Rect{300, 200, 100, 50}, "b");
    const EdgeId bent = doc.connect(End{a, Face::Auto, {}}, End{b, Face::Auto, {}});
    const EdgeId straight = doc.connect(End{a, Face::Auto, {}}, End{b, Face::Auto, {}});
    doc.setEdgeRoute(bent, Route::Elbow, Arrow::None, Arrow::Arrow);
    REQUIRE(doc.setEdgeVia(bent, Pt{-40.5f, 17.25f}));
    (void)straight;

    const FlowParse back = roundTrip(doc);
    REQUIRE(back.status == FlowParseStatus::Ok);
    REQUIRE(back.doc.edges().size() == 2);
    CHECK(back.doc.edges()[0].via == std::optional<Pt>(Pt{-40.5f, 17.25f}));
    CHECK_FALSE(back.doc.edges()[1].via.has_value());
    CHECK_EQ(diff(doc, back.doc), "");

    // The automatic edge writes no via keys at all.
    const IniFile ini = serializeFlow(doc).ini;
    const IniFile::Section* auto_ = ini.section("edge.2");
    REQUIRE(auto_ != nullptr);
    for (const IniFile::Entry& entry : auto_->entries) {
        CHECK(entry.key != "via_x");
        CHECK(entry.key != "via_y");
    }

    // A hand-written old file has no such keys.
    Hand old;
    old.node(1, "rectangle").node(2, "rectangle");
    old.edge(1, 1, 2, "route=elbow");
    const FlowParse parsed = old.parse();
    REQUIRE(parsed.doc.edges().size() == 1);
    CHECK_FALSE(parsed.doc.edges()[0].via.has_value());
    CHECK(parsed.repaired == 0);
}

TEST_CASE("Hostile manual route numbers are clamped or ignored") {
    Hand h;
    h.node(1, "rectangle").node(2, "rectangle");
    h.edge(1, 1, 2, "via_x=1e30;via_y=-inf");
    h.edge(2, 1, 2, "via_x=nan;via_y=junk");
    h.edge(3, 1, 2, "via_x=5");
    const FlowParse parsed = h.parse();
    REQUIRE(parsed.doc.edges().size() == 3);
    const auto& e = parsed.doc.edges();
    REQUIRE(e[0].via.has_value());
    CHECK(std::isfinite(e[0].via->x));
    CHECK(std::isfinite(e[0].via->y));
    CHECK(std::abs(e[0].via->x) <= kMaxCoord);
    CHECK(std::abs(e[0].via->y) <= kMaxCoord);
    REQUIRE(e[1].via.has_value()); // keys present but unreadable: a zero offset, never NaN
    CHECK(e[1].via->x == 0.0f);
    CHECK(e[1].via->y == 0.0f);
    REQUIRE(e[2].via.has_value());
    CHECK(e[2].via->x == 5.0f);
    CHECK(e[2].via->y == 0.0f);
}
