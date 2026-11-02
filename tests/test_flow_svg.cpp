#include <doctest/doctest.h>

#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "flow/flow_document.hpp"
#include "flow/flow_svg.hpp"

using namespace trace;
using namespace trace::flow;

namespace {

// --- a tiny XML checker: balanced tags, quoted attributes, valid entities, no raw '<' or '&' in text ---------------
bool validEntity(const std::string& x, std::size_t amp, std::size_t& semi) {
    semi = x.find(';', amp);
    if (semi == std::string::npos || semi - amp > 12)
        return false;
    const std::string name = x.substr(amp + 1, semi - amp - 1);
    if (name == "amp" || name == "lt" || name == "gt" || name == "quot" || name == "apos")
        return true;
    if (name.size() >= 2 && name[0] == '#') {
        const bool hexNum = name[1] == 'x';
        if (hexNum && name.size() < 3)
            return false;
        for (std::size_t i = hexNum ? 2 : 1; i < name.size(); ++i)
            if (!(hexNum ? std::isxdigit(static_cast<unsigned char>(name[i])) : std::isdigit(static_cast<unsigned char>(name[i]))))
                return false;
        return true;
    }
    return false;
}

bool isNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '-' || c == '.';
}

// Returns "" when the text is well-formed, else a description of the first problem.
std::string checkXml(const std::string& x) {
    std::size_t i = 0;
    const std::size_t n = x.size();
    std::vector<std::string> stack;
    bool rootClosed = false;
    bool sawRoot = false;
    if (x.rfind("<?xml", 0) == 0) {
        const std::size_t e = x.find("?>");
        if (e == std::string::npos)
            return "unterminated xml declaration";
        i = e + 2;
    }
    for (std::size_t k = 0; k < n; ++k) {
        const auto c = static_cast<unsigned char>(x[k]);
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
            return "control character at " + std::to_string(k);
    }
    while (i < n) {
        if (x[i] != '<') {
            const std::size_t e = x.find('<', i);
            const std::size_t end = e == std::string::npos ? n : e;
            for (std::size_t k = i; k < end; ++k) {
                if (x[k] == '&') {
                    std::size_t semi = 0;
                    if (!validEntity(x, k, semi))
                        return "bad entity at " + std::to_string(k);
                    k = semi;
                } else if (x[k] == '>') {
                    return "raw '>' in text at " + std::to_string(k);
                } else if (stack.empty() && !std::isspace(static_cast<unsigned char>(x[k]))) {
                    return "text outside the root";
                }
            }
            i = end;
            continue;
        }
        ++i;
        if (i >= n)
            return "dangling '<'";
        if (x[i] == '/') {
            ++i;
            std::string name;
            while (i < n && isNameChar(x[i]))
                name += x[i++];
            if (i >= n || x[i] != '>')
                return "bad end tag";
            ++i;
            if (stack.empty() || stack.back() != name)
                return "unbalanced end tag " + name;
            stack.pop_back();
            if (stack.empty())
                rootClosed = true;
            continue;
        }
        if (x[i] == '!' || x[i] == '?')
            return "unexpected markup declaration";
        if (rootClosed)
            return "content after the root";
        std::string name;
        while (i < n && isNameChar(x[i]))
            name += x[i++];
        if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])))
            return "bad tag name";
        std::vector<std::string> attrs;
        bool selfClose = false;
        for (;;) {
            while (i < n && std::isspace(static_cast<unsigned char>(x[i])))
                ++i;
            if (i >= n)
                return "unterminated tag " + name;
            if (x[i] == '>') {
                ++i;
                break;
            }
            if (x[i] == '/' && i + 1 < n && x[i + 1] == '>') {
                selfClose = true;
                i += 2;
                break;
            }
            std::string an;
            while (i < n && isNameChar(x[i]))
                an += x[i++];
            if (an.empty())
                return "bad attribute name in " + name;
            for (const std::string& seen : attrs)
                if (seen == an)
                    return "duplicate attribute " + an;
            attrs.push_back(an);
            if (i >= n || x[i] != '=')
                return "attribute without '=' in " + name;
            ++i;
            if (i >= n || x[i] != '"')
                return "unquoted attribute " + an;
            ++i;
            while (i < n && x[i] != '"') {
                if (x[i] == '<')
                    return "raw '<' in attribute " + an;
                if (x[i] == '&') {
                    std::size_t semi = 0;
                    if (!validEntity(x, i, semi))
                        return "bad entity in attribute " + an;
                    i = semi;
                }
                ++i;
            }
            if (i >= n)
                return "unterminated attribute " + an;
            ++i;
        }
        sawRoot = true;
        if (!selfClose)
            stack.push_back(name);
        else if (stack.empty())
            rootClosed = true;
    }
    if (!stack.empty())
        return "unclosed tag " + stack.back();
    if (!sawRoot)
        return "no root element";
    return {};
}

bool has(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

std::size_t count(const std::string& s, const std::string& sub) {
    std::size_t c = 0;
    for (std::size_t p = s.find(sub); p != std::string::npos; p = s.find(sub, p + sub.size()))
        ++c;
    return c;
}

constexpr float kHuge = 1.0e6f;

Style colored(Rgba8 fill) {
    Style s;
    s.fill = fill;
    return s;
}

// A document that uses every kind of node, containers with children, every route, all label placements, images,
// explicit and Auto colors and dashes.
FlowDocument richDocument() {
    FlowDocument doc;
    std::vector<NodeId> ids;
    for (int k = 0; k < kNodeKindCount; ++k) {
        Style st;
        if (k % 3 == 1)
            st.fill = Rgba8{200, 40, 40, 255};
        if (k % 4 == 2)
            st.stroke = Rgba8{10, 120, 200, 128};
        if (k % 5 == 3)
            st.dash = Dash::Dashed;
        if (k % 5 == 4)
            st.dash = Dash::Dotted;
        st.align = static_cast<TextAlign>(k % 3);
        const float x = static_cast<float>(k % 5) * 220.0f;
        const float y = static_cast<float>(k / 5) * 160.0f;
        const NodeKind kind = static_cast<NodeKind>(k);
        const int sides = 3 + k % 7;
        ids.push_back(doc.addNode(kind, Rect{x, y, 180, 100}, "Node <" + std::to_string(k) + "> & \"q\" 'a'\nsecond line that is long enough to wrap", st, sides));
    }
    const NodeId box = doc.addNode(NodeKind::Container, Rect{0, 520, 600, 300}, "Lane <A>");
    const NodeId child1 = doc.addNode(NodeKind::Rounded, Rect{20, 580, 120, 60}, "in");
    const NodeId child2 = doc.addNode(NodeKind::Diamond, Rect{200, 600, 120, 100}, "yes?");
    CHECK(doc.reparent(child1, box));
    CHECK(doc.reparent(child2, box));
    const NodeId pic = doc.addNode(NodeKind::Image, Rect{700, 520, 100, 60});
    auto img = std::make_shared<Image>(4, 3, Rgba8{10, 20, 30, 255});
    img->set(1, 1, Rgba8{255, 0, 0, 100});
    CHECK(doc.setNodeImage(pic, doc.addImage(img)));
    const NodeId lost = doc.addNode(NodeKind::Image, Rect{700, 620, 100, 60});
    ids.push_back(box);
    ids.push_back(child1);
    ids.push_back(child2);
    ids.push_back(pic);
    ids.push_back(lost);

    int i = 0;
    for (std::size_t a = 0; a + 1 < ids.size(); a += 2) {
        const EdgeId e = doc.connect(End{ids[a], Face::Auto, {}}, End{ids[a + 1], Face::Auto, {}});
        CHECK(e != kNoEdge);
        const Route route = static_cast<Route>(i % 3);
        doc.setEdgeRoute(e, route, static_cast<Arrow>(i % 4), static_cast<Arrow>((i + 1) % 4));
        doc.setEdgeLabel(e, i == 1 ? "a < b & \"c\" 'd'" : "label " + std::to_string(i));
        doc.setEdgeLabelPlacement(e, 0.3f + 0.1f * static_cast<float>(i % 4), static_cast<LabelSide>(i % 3), i % 2 == 0);
        Style st;
        st.dash = static_cast<Dash>(i % 3);
        if (i % 2)
            st.stroke = Rgba8{20, 160, 60, 255};
        if (i % 3 == 1)
            st.textColor = Rgba8{200, 0, 120, 200};
        doc.setEdgeStyle(e, st);
        ++i;
    }
    // A self loop and a free-ended connector.
    doc.connect(End{ids[0], Face::Right, {}}, End{ids[0], Face::Top, {}});
    const EdgeId freeEdge = doc.connect(End{ids[1], Face::Auto, {}}, End{kNoNode, Face::Auto, Pt{900, 40}});
    doc.setEdgeLabel(freeEdge, "free");
    return doc;
}

std::vector<std::uint8_t> decodeBase64(const std::string& s) {
    std::vector<std::uint8_t> out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (const char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else break; // '=' padding
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

std::uint32_t crc32of(const std::uint8_t* d, std::size_t n) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc & 1u) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    return ~crc;
}

std::uint32_t be32(const std::vector<std::uint8_t>& v, std::size_t at) {
    return (std::uint32_t{v[at]} << 24) | (std::uint32_t{v[at + 1]} << 16) | (std::uint32_t{v[at + 2]} << 8) | v[at + 3];
}

} // namespace

TEST_CASE("svg: a rich document is well-formed XML") {
    const FlowDocument doc = richDocument();
    SvgOptions opt;
    opt.background = Rgba8{250, 250, 250, 255};
    const std::string svg = exportSvg(doc, opt);
    CHECK(checkXml(svg) == "");
    CHECK(svg.rfind("<?xml", 0) == 0);
    CHECK(has(svg, "xmlns=\"http://www.w3.org/2000/svg\""));
    CHECK(has(svg, "viewBox=\""));
    CHECK(has(svg, "<path "));
    CHECK(has(svg, "<text "));
    CHECK(has(svg, "<image "));
    CHECK(has(svg, "stroke-dasharray"));
    CHECK(has(svg, "rotate("));
    CHECK(has(svg, ">Lane &lt;A&gt;<"));
    // With a transparent background and Auto colors exported dark-on-light, still well-formed.
    SvgOptions dark;
    dark.inkAuto = Rgba8{240, 240, 240, 255};
    dark.paperAuto = Rgba8{30, 30, 34, 255};
    CHECK(checkXml(exportSvg(doc, dark)) == "");
}

TEST_CASE("svg: empty documents and empty selections give a small valid SVG") {
    FlowDocument empty;
    const std::string a = exportSvg(empty);
    CHECK(checkXml(a) == "");
    CHECK(has(a, "<svg "));
    CHECK(!has(a, "<path"));
    CHECK(!has(a, "<text"));
    CHECK(a.size() < 600);

    FlowDocument doc;
    const NodeId n = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 50}, "hello");
    (void)n;
    const NodeId bogus = 9999;
    SvgOptions opt;
    opt.only = std::span<const NodeId>(&bogus, 1);
    const std::string b = exportSvg(doc, opt);
    CHECK(checkXml(b) == "");
    CHECK(!has(b, "hello"));
    CHECK(!has(b, "<path"));
}

TEST_CASE("svg: user text, labels and the font family are escaped") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 300, 80}, "x < y & \"z\" 'w'");
    const NodeId b = doc.addNode(NodeKind::Rectangle, Rect{0, 200, 300, 80}, "b");
    const EdgeId e = doc.connect(End{a, Face::Bottom, {}}, End{b, Face::Top, {}});
    doc.setEdgeLabel(e, "<tag attr=\"1\"> & 'q'");
    SvgOptions opt;
    opt.fontFamily = "My <Font> & \"Co\" 'x'";
    const std::string svg = exportSvg(doc, opt);
    CHECK(checkXml(svg) == "");
    CHECK(has(svg, "x &lt; y &amp; &quot;z&quot; &apos;w&apos;"));
    CHECK(has(svg, "&lt;tag attr=&quot;1&quot;&gt; &amp; &apos;q&apos;"));
    CHECK(has(svg, "font-family=\"My &lt;Font&gt; &amp; &quot;Co&quot; &apos;x&apos;, sans-serif\""));
    CHECK(!has(svg, "<tag"));
}

TEST_CASE("svg: the font family always ends with a generic fallback") {
    FlowDocument doc;
    doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 50}, "t");
    SvgOptions opt;
    opt.fontFamily = "";
    CHECK(has(exportSvg(doc, opt), "font-family=\"sans-serif\""));
    opt.fontFamily = "Inter, monospace";
    CHECK(has(exportSvg(doc, opt), "font-family=\"Inter, monospace\""));
    opt.fontFamily = "Georgia";
    CHECK(has(exportSvg(doc, opt), "font-family=\"Georgia, sans-serif\""));
}

TEST_CASE("svg: invalid UTF-8 and forbidden control characters never reach the document") {
    FlowDocument doc;
    std::string text = "ok";
    text += '\x01';
    text += "\xFF\xC3";
    text += "\xE2\x82";
    text += "\xED\xA0\x80"; // an encoded surrogate
    text += '\x0B';
    text += "end \xE2\x82\xAC";
    doc.addNode(NodeKind::Rectangle, Rect{0, 0, 300, 80}, text);
    const std::string svg = exportSvg(doc);
    CHECK(checkXml(svg) == "");
    CHECK(has(svg, "end \xE2\x82\xAC"));
    CHECK(!has(svg, "\x01"));
}

TEST_CASE("svg: Auto colors resolve per the options") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 50});
    const NodeId b = doc.addNode(NodeKind::Rectangle, Rect{300, 0, 100, 50}, "t");
    doc.connect(End{a, Face::Right, {}}, End{b, Face::Left, {}});
    SvgOptions o1;
    o1.inkAuto = Rgba8{0x11, 0x22, 0x33, 255};
    o1.paperAuto = Rgba8{0x44, 0x55, 0x66, 255};
    SvgOptions o2;
    o2.inkAuto = Rgba8{0xaa, 0xbb, 0xcc, 255};
    o2.paperAuto = Rgba8{0x77, 0x88, 0x99, 255};
    const std::string s1 = exportSvg(doc, o1);
    const std::string s2 = exportSvg(doc, o2);
    CHECK(s1 != s2);
    CHECK(has(s1, "stroke=\"#112233\""));
    CHECK(has(s1, "fill=\"#445566\""));
    CHECK(has(s1, "fill=\"#112233\"")); // text and arrowhead
    CHECK(!has(s1, "#aabbcc"));
    CHECK(has(s2, "stroke=\"#aabbcc\""));
    CHECK(has(s2, "fill=\"#778899\""));
    CHECK(!has(s2, "#112233"));
}

TEST_CASE("svg: sticky default, explicit colors and automatic text color") {
    FlowDocument doc;
    doc.addNode(NodeKind::Sticky, Rect{0, 0, 120, 80}, "note");
    doc.addNode(NodeKind::Rectangle, Rect{200, 0, 120, 80}, "dark", colored(Rgba8{10, 10, 10, 255}));
    doc.addNode(NodeKind::Rectangle, Rect{400, 0, 120, 80}, "light", colored(Rgba8{250, 250, 200, 255}));
    Style translucent = colored(Rgba8{0, 0, 0, 40});
    doc.addNode(NodeKind::Rectangle, Rect{600, 0, 120, 80}, "faint", translucent);
    SvgOptions opt;
    opt.stickyFill = Rgba8{1, 2, 3, 255};
    opt.inkAuto = Rgba8{9, 9, 9, 255};
    const std::string svg = exportSvg(doc, opt);
    CHECK(checkXml(svg) == "");
    CHECK(has(svg, "fill=\"#010203\""));  // sticky body
    CHECK(has(svg, "fill=\"#342c12\""));  // sticky ink
    CHECK(has(svg, "fill=\"#f4f4f6\""));  // light text on the dark fill
    CHECK(has(svg, "fill=\"#202124\""));  // dark text on the light fill
    CHECK(has(svg, "fill-opacity=\"0.157\"")); // alpha 40
    CHECK(has(svg, "fill=\"#090909\""));  // Auto text on a translucent fill keeps the ink color
}

TEST_CASE("svg: no outline on Text nodes, rim on Database, dash styles") {
    FlowDocument doc;
    doc.addNode(NodeKind::Text, Rect{0, 0, 120, 40}, "plain");
    doc.addNode(NodeKind::Database, Rect{200, 0, 100, 100});
    Style dashed;
    dashed.dash = Dash::Dashed;
    dashed.strokeWidth = 3.0f;
    doc.addNode(NodeKind::Ellipse, Rect{400, 0, 100, 100}, "", dashed);
    Style dotted;
    dotted.dash = Dash::Dotted;
    doc.addNode(NodeKind::Diamond, Rect{600, 0, 100, 100}, "", dotted);
    const std::string svg = exportSvg(doc);
    CHECK(checkXml(svg) == "");
    // Text: no path at all for it; Database: outline plus rim; two more shapes: 1 + 2 + 1 + 1.
    CHECK(count(svg, "<path ") == 4);
    CHECK(has(svg, "stroke-dasharray=\"12 7.5\""));
    CHECK(has(svg, "stroke-dasharray=\"2 4\""));
}

TEST_CASE("svg: background is present only when given") {
    FlowDocument doc;
    doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 50});
    SvgOptions none;
    const std::string a = exportSvg(doc, none);
    CHECK(count(a, "<rect") == 0);
    SvgOptions bg;
    bg.background = Rgba8{16, 32, 48, 255};
    const std::string b = exportSvg(doc, bg);
    CHECK(checkXml(b) == "");
    CHECK(count(b, "<rect") == 1);
    CHECK(has(b, "fill=\"#102030\""));
    SvgOptions clear;
    clear.background = Rgba8{16, 32, 48, 0};
    CHECK(count(exportSvg(doc, clear), "<rect") == 0);
}

TEST_CASE("svg: size is the content plus padding") {
    FlowDocument doc;
    doc.addNode(NodeKind::Rectangle, Rect{100, 200, 300, 100});
    SvgOptions opt;
    opt.padding = 10.0f;
    const std::string svg = exportSvg(doc, opt);
    // 300 + 2 (half the stroke on each side) + 20 of padding.
    CHECK(has(svg, "width=\"322\" height=\"122\""));
    CHECK(has(svg, "viewBox=\"89 189 322 122\""));
}

TEST_CASE("svg: only restricts to the nodes and the connectors between them") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 50}, "AAA");
    const NodeId b = doc.addNode(NodeKind::Rectangle, Rect{300, 0, 100, 50}, "BBB");
    const NodeId c = doc.addNode(NodeKind::Rectangle, Rect{600, 0, 100, 50}, "CCC");
    const EdgeId ab = doc.connect(End{a, Face::Right, {}}, End{b, Face::Left, {}});
    const EdgeId bc = doc.connect(End{b, Face::Right, {}}, End{c, Face::Left, {}});
    const EdgeId af = doc.connect(End{a, Face::Top, {}}, End{kNoNode, Face::Auto, Pt{50, -200}});
    const EdgeId cf = doc.connect(End{kNoNode, Face::Auto, Pt{650, 250}}, End{c, Face::Bottom, {}});
    doc.setEdgeLabel(ab, "lab-ab");
    doc.setEdgeLabel(bc, "lab-bc");
    doc.setEdgeLabel(af, "lab-af");
    doc.setEdgeLabel(cf, "lab-cf");

    const std::string all = exportSvg(doc);
    CHECK(has(all, "CCC"));
    CHECK(has(all, "lab-bc"));
    CHECK(has(all, "lab-cf"));

    const std::vector<NodeId> sel{a, b};
    SvgOptions opt;
    opt.only = sel;
    const std::string part = exportSvg(doc, opt);
    CHECK(checkXml(part) == "");
    CHECK(has(part, ">AAA<"));
    CHECK(has(part, ">BBB<"));
    CHECK(!has(part, "CCC"));
    CHECK(has(part, "lab-ab"));
    CHECK(!has(part, "lab-bc"));
    CHECK(has(part, "lab-af"));
    CHECK(!has(part, "lab-cf"));
    CHECK(part.size() < all.size());

    // An unknown id in the selection is ignored.
    const std::vector<NodeId> sel2{a, 4242};
    opt.only = sel2;
    const std::string one = exportSvg(doc, opt);
    CHECK(has(one, ">AAA<"));
    CHECK(!has(one, ">BBB<"));
    CHECK(!has(one, "lab-ab"));
}

TEST_CASE("svg: the same input gives the same bytes") {
    const FlowDocument doc = richDocument();
    SvgOptions opt;
    opt.background = Rgba8{1, 2, 3, 255};
    CHECK(exportSvg(doc, opt) == exportSvg(doc, opt));
    const FlowDocument copy = doc;
    CHECK(exportSvg(copy, opt) == exportSvg(doc, opt));
}

TEST_CASE("svg: an image becomes a decodable PNG data URI") {
    FlowDocument doc;
    const NodeId pic = doc.addNode(NodeKind::Image, Rect{0, 0, 80, 40});
    auto img = std::make_shared<Image>(5, 2, Rgba8{1, 2, 3, 255});
    img->set(0, 0, Rgba8{9, 8, 7, 200});
    CHECK(doc.setNodeImage(pic, doc.addImage(img)));
    const std::string svg = exportSvg(doc);
    CHECK(checkXml(svg) == "");
    const std::string key = "xlink:href=\"data:image/png;base64,";
    const std::size_t at = svg.find(key);
    REQUIRE(at != std::string::npos);
    const std::size_t from = at + key.size();
    const std::vector<std::uint8_t> png = decodeBase64(svg.substr(from, svg.find('"', from) - from));
    REQUIRE(png.size() > 40);
    const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i)
        CHECK(png[static_cast<std::size_t>(i)] == sig[i]);

    // Walk the chunks, checking lengths and CRCs; collect the IDAT bytes.
    std::size_t pos = 8;
    std::vector<std::uint8_t> idat;
    std::string types;
    while (pos + 12 <= png.size()) {
        const std::uint32_t len = be32(png, pos);
        REQUIRE(pos + 12 + len <= png.size());
        const std::string type(reinterpret_cast<const char*>(&png[pos + 4]), 4);
        types += type + " ";
        CHECK(crc32of(&png[pos + 4], len + 4) == be32(png, pos + 8 + len));
        if (type == "IHDR") {
            CHECK(len == 13);
            CHECK(be32(png, pos + 8) == 5);
            CHECK(be32(png, pos + 12) == 2);
            CHECK(png[pos + 16] == 8);
            CHECK(png[pos + 17] == 6); // the image has translucent pixels: RGBA
        } else if (type == "IDAT") {
            idat.insert(idat.end(), png.begin() + static_cast<std::ptrdiff_t>(pos + 8), png.begin() + static_cast<std::ptrdiff_t>(pos + 8 + len));
        }
        pos += 12 + len;
    }
    CHECK(pos == png.size());
    CHECK(types == "IHDR IDAT IEND ");

    // Inflate the stored blocks and compare with the pixels.
    REQUIRE(idat.size() > 6);
    CHECK(idat[0] == 0x78);
    std::vector<std::uint8_t> raw;
    std::size_t p = 2;
    for (bool last = false; !last;) {
        REQUIRE(p + 5 <= idat.size());
        last = (idat[p] & 1) != 0;
        CHECK((idat[p] >> 1) == 0); // stored
        const std::size_t len = idat[p + 1] | (std::size_t{idat[p + 2]} << 8);
        const std::size_t nlen = idat[p + 3] | (std::size_t{idat[p + 4]} << 8);
        CHECK((len ^ nlen) == 0xFFFF);
        REQUIRE(p + 5 + len <= idat.size());
        raw.insert(raw.end(), idat.begin() + static_cast<std::ptrdiff_t>(p + 5), idat.begin() + static_cast<std::ptrdiff_t>(p + 5 + len));
        p += 5 + len;
    }
    CHECK(p + 4 == idat.size());
    std::uint32_t a = 1, b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    CHECK(be32(idat, p) == ((b << 16) | a));
    REQUIRE(raw.size() == 2u * (1u + 5u * 4u));
    CHECK(raw[0] == 0);
    CHECK(raw[1] == 9);
    CHECK(raw[2] == 8);
    CHECK(raw[3] == 7);
    CHECK(raw[4] == 200);
    CHECK(raw[5] == 1);
}

TEST_CASE("svg: an opaque image is written without an alpha channel") {
    FlowDocument doc;
    const NodeId pic = doc.addNode(NodeKind::Image, Rect{0, 0, 80, 40});
    CHECK(doc.setNodeImage(pic, doc.addImage(std::make_shared<Image>(3, 3, Rgba8{5, 5, 5, 255}))));
    const std::string svg = exportSvg(doc);
    const std::string key = "base64,";
    const std::size_t from = svg.find(key) + key.size();
    const std::vector<std::uint8_t> png = decodeBase64(svg.substr(from, svg.find('"', from) - from));
    REQUIRE(png.size() > 30);
    CHECK(png[8 + 8 + 9] == 2); // colour type RGB
}

TEST_CASE("svg: a missing image is a placeholder box with no data URI") {
    FlowDocument doc;
    doc.addNode(NodeKind::Image, Rect{0, 0, 80, 40});
    const std::string svg = exportSvg(doc);
    CHECK(checkXml(svg) == "");
    CHECK(!has(svg, "<image"));
    CHECK(!has(svg, "base64"));
    CHECK(has(svg, "<rect"));
    CHECK(count(svg, "<path ") == 2); // the cross
}

TEST_CASE("svg: text is wrapped to the node and keeps explicit newlines") {
    FlowDocument doc;
    Style left;
    left.align = TextAlign::Left;
    doc.addNode(NodeKind::Rectangle, Rect{0, 0, 120, 200}, "alpha beta gamma delta epsilon zeta eta theta", left);
    doc.addNode(NodeKind::Rectangle, Rect{300, 0, 400, 100}, "one\ntwo\n\nfour");
    Style right;
    right.align = TextAlign::Right;
    doc.addNode(NodeKind::Rectangle, Rect{800, 0, 200, 100}, "r", right);
    const std::string svg = exportSvg(doc);
    CHECK(checkXml(svg) == "");
    CHECK(has(svg, "text-anchor=\"start\""));
    CHECK(has(svg, "text-anchor=\"end\""));
    CHECK(has(svg, "text-anchor=\"middle\""));
    // The long sentence takes several lines; the newline text has 3 non-empty lines.
    CHECK(count(svg, "<tspan") >= 4 + 3 + 1);
    CHECK(has(svg, ">one<"));
    CHECK(has(svg, ">four<"));
    // Words are never cut in two when they fit.
    CHECK(has(svg, ">alpha beta<"));
}

TEST_CASE("svg: a tall text block is clipped to its area") {
    FlowDocument doc;
    std::string lots;
    for (int i = 0; i < 40; ++i)
        lots += "line " + std::to_string(i) + "\n";
    doc.addNode(NodeKind::Rectangle, Rect{0, 0, 120, 60}, lots);
    const std::string svg = exportSvg(doc);
    CHECK(checkXml(svg) == "");
    CHECK(has(svg, "<clipPath id=\"c1\">"));
    CHECK(has(svg, "clip-path=\"url(#c1)\""));
    CHECK(has(svg, "<defs>"));
}

TEST_CASE("svg: connector labels sit on a plate when On and turn with the line") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 50});
    const NodeId b = doc.addNode(NodeKind::Rectangle, Rect{400, 300, 100, 50});
    const EdgeId e = doc.connect(End{a, Face::Right, {}}, End{b, Face::Left, {}});
    doc.setEdgeRoute(e, Route::Straight, Arrow::None, Arrow::Arrow);
    doc.setEdgeLabel(e, "lbl");
    doc.setEdgeLabelPlacement(e, 0.5f, LabelSide::On, true);
    SvgOptions opt;
    opt.paperAuto = Rgba8{1, 2, 3, 255};
    const std::string on = exportSvg(doc, opt);
    CHECK(checkXml(on) == "");
    CHECK(has(on, "<g transform=\"rotate("));
    CHECK(has(on, "fill=\"#010203\" fill-opacity=\"0.922\""));
    CHECK(has(on, ">lbl<"));
    opt.background = Rgba8{9, 9, 9, 255};
    const std::string withBg = exportSvg(doc, opt);
    CHECK(has(withBg, "fill=\"#090909\" fill-opacity=\"0.922\""));

    doc.setEdgeLabelPlacement(e, 0.5f, LabelSide::Above, false);
    const std::string above = exportSvg(doc, SvgOptions{});
    CHECK(checkXml(above) == "");
    CHECK(!has(above, "rotate("));
    CHECK(!has(above, "fill-opacity=\"0.922\""));
    CHECK(has(above, ">lbl<"));
}

TEST_CASE("svg: hostile numbers give finite output") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{-kHuge, -kHuge, 100, 100});
    const NodeId b = doc.addNode(NodeKind::Ellipse, Rect{kHuge, kHuge, 1.0e9f, 1.0e9f});
    const NodeId c = doc.addNode(NodeKind::Polygon, Rect{std::numeric_limits<float>::quiet_NaN(), 5, 50, 50}, "", {}, 99999);
    const EdgeId e = doc.connect(End{a, Face::Auto, {}}, End{b, Face::Auto, {}});
    doc.setEdgeLabelPlacement(e, std::numeric_limits<float>::quiet_NaN(), LabelSide::On, true);
    doc.connect(End{c, Face::Auto, {}}, End{kNoNode, Face::Auto, Pt{std::numeric_limits<float>::infinity(), 1.0e30f}});
    Style st;
    st.strokeWidth = std::numeric_limits<float>::quiet_NaN();
    st.fontSize = std::numeric_limits<float>::infinity();
    doc.setEdgeStyle(e, st);
    SvgOptions opt;
    opt.padding = std::numeric_limits<float>::quiet_NaN();
    std::string svg = exportSvg(doc, opt);
    CHECK(checkXml(svg) == "");
    for (std::string& s : std::vector<std::string>{svg}) {
        for (char& ch : s)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        CHECK(!has(s, "nan"));
        CHECK(!has(s, "inf"));
        CHECK(!has(s, "e+"));
    }
    opt.padding = std::numeric_limits<float>::infinity();
    CHECK(checkXml(exportSvg(doc, opt)) == "");
    opt.padding = -50.0f;
    CHECK(checkXml(exportSvg(doc, opt)) == "");
}

TEST_CASE("svg: numbers do not depend on the C locale and are trimmed") {
    FlowDocument doc;
    doc.addNode(NodeKind::Rectangle, Rect{10.5f, 20.25f, 100, 50});
    const std::string svg = exportSvg(doc);
    CHECK(has(svg, "M10.5 20.25L"));
    CHECK(!has(svg, ","));
    CHECK(!has(svg, ".00"));
}

TEST_CASE("svg: containers draw a band and a title, children paint over them") {
    FlowDocument doc;
    const NodeId box = doc.addNode(NodeKind::Container, Rect{0, 0, 400, 300}, "Title");
    const NodeId kid = doc.addNode(NodeKind::Rectangle, Rect{20, 60, 100, 50}, "Kid");
    CHECK(doc.reparent(kid, box));
    const std::string svg = exportSvg(doc);
    CHECK(checkXml(svg) == "");
    CHECK(svg.find(">Title<") < svg.find(">Kid<"));
    CHECK(has(svg, "<rect")); // the title band
}

TEST_CASE("svg: wide characters are estimated wider than Latin ones") {
    FlowDocument doc;
    // A 100 wide box has about 84 units of text width: 5 Latin glyphs of 16 fit on a line, 5 wide ones do not.
    doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 200}, "abcde");
    doc.addNode(NodeKind::Rectangle, Rect{300, 0, 100, 200}, "\xE6\xBC\xA2\xE5\xAD\x97\xE6\xBC\xA2\xE5\xAD\x97\xE6\xBC\xA2");
    const std::string svg = exportSvg(doc);
    CHECK(checkXml(svg) == "");
    // The Latin word stays on one line; the five CJK glyphs need three lines (width for 5 only at 1 em each = 80).
    CHECK(has(svg, ">abcde<"));
    CHECK(count(svg, "<tspan") >= 2);
}

TEST_CASE("svg: text alignment moves the anchor and the lines, left to right and top to bottom") {
    // The number of the attribute `attr` ("x" or "y") on the tspan that holds `word`.
    const auto tspanValue = [](const std::string& svg, const std::string& word, const std::string& attr) {
        const std::size_t at = svg.find(">" + word + "<");
        REQUIRE(at != std::string::npos);
        const std::string key = " " + attr + "=\"";
        const std::size_t from = svg.rfind(key, at);
        REQUIRE(from != std::string::npos);
        return std::stod(svg.substr(from + key.size()));
    };
    FlowDocument doc;
    Style left, center, right;
    left.align = TextAlign::Left;
    center.align = TextAlign::Center;
    right.align = TextAlign::Right;
    // The same box three times; only the alignment differs.
    doc.addNode(NodeKind::Rectangle, Rect{0, 0, 200, 100}, "LL", left);
    doc.addNode(NodeKind::Rectangle, Rect{0, 300, 200, 100}, "CC", center);
    doc.addNode(NodeKind::Rectangle, Rect{0, 600, 200, 100}, "RR", right);
    const std::string svg = exportSvg(doc);
    CHECK(checkXml(svg) == "");
    const double xl = tspanValue(svg, "LL", "x");
    const double xc = tspanValue(svg, "CC", "x");
    const double xr = tspanValue(svg, "RR", "x");
    CHECK(xl < xc);
    CHECK(xc < xr);
    CHECK(xc == doctest::Approx(100.0));

    FlowDocument vdoc;
    Style top, middle, bottom;
    top.valign = VAlign::Top;
    middle.valign = VAlign::Middle;
    bottom.valign = VAlign::Bottom;
    vdoc.addNode(NodeKind::Rectangle, Rect{0, 0, 200, 200}, "TT", top);
    vdoc.addNode(NodeKind::Rectangle, Rect{400, 0, 200, 200}, "MM", middle);
    vdoc.addNode(NodeKind::Rectangle, Rect{800, 0, 200, 200}, "BB", bottom);
    const std::string vsvg = exportSvg(vdoc);
    CHECK(checkXml(vsvg) == "");
    const double yt = tspanValue(vsvg, "TT", "y");
    const double ym = tspanValue(vsvg, "MM", "y");
    const double yb = tspanValue(vsvg, "BB", "y");
    CHECK(yt < ym);
    CHECK(ym < yb);
    CHECK(yt < 50.0);  // near the top of the box
    CHECK(yb > 150.0); // near the bottom
}

TEST_CASE("svg: a connector shaped by hand is drawn through its offset point") {
    FlowDocument doc;
    const NodeId a = doc.addNode(NodeKind::Rectangle, Rect{0, 0, 100, 50});
    const NodeId b = doc.addNode(NodeKind::Rectangle, Rect{400, 0, 100, 50});
    const EdgeId e = doc.connect(End{a, Face::Right, {}}, End{b, Face::Left, {}});
    doc.setEdgeRoute(e, Route::Straight, Arrow::None, Arrow::None);
    const std::string plain = exportSvg(doc);
    REQUIRE(doc.setEdgeVia(e, Pt{0, -123}));
    const std::string bent = exportSvg(doc);
    CHECK(checkXml(bent) == "");
    CHECK(has(bent, "250 -98")); // the midpoint (250, 25) moved up by 123
    CHECK(!has(plain, "250 -98"));
    CHECK(bent != plain);
}
