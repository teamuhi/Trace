#include "flow/flow_serialize.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace trace::flow {

namespace {

// --- names: enums are stored by name so the order of the enums in code never matters -------------------------

template <class E>
struct EnumName {
    E value;
    const char* name;
};

constexpr std::array<EnumName<NodeKind>, kNodeKindCount> kKindNames{{
    {NodeKind::Rectangle, "rectangle"},
    {NodeKind::Rounded, "rounded"},
    {NodeKind::Ellipse, "ellipse"},
    {NodeKind::Diamond, "diamond"},
    {NodeKind::Parallelogram, "parallelogram"},
    {NodeKind::Terminator, "terminator"},
    {NodeKind::Document, "document"},
    {NodeKind::Database, "database"},
    {NodeKind::Hexagon, "hexagon"},
    {NodeKind::Circle, "circle"},
    {NodeKind::Sticky, "sticky"},
    {NodeKind::Text, "text"},
    {NodeKind::Image, "image"},
    {NodeKind::Container, "container"},
    {NodeKind::Polygon, "polygon"},
}};
constexpr std::array<EnumName<Face>, 5> kFaceNames{{
    {Face::Top, "top"}, {Face::Right, "right"}, {Face::Bottom, "bottom"}, {Face::Left, "left"}, {Face::Auto, "auto"},
}};
constexpr std::array<EnumName<Route>, 3> kRouteNames{{
    {Route::Curved, "curved"}, {Route::Elbow, "elbow"}, {Route::Straight, "straight"},
}};
constexpr std::array<EnumName<Arrow>, 4> kArrowNames{{
    {Arrow::None, "none"}, {Arrow::Arrow, "arrow"}, {Arrow::Open, "open"}, {Arrow::Dot, "dot"},
}};
constexpr std::array<EnumName<Dash>, 3> kDashNames{{
    {Dash::Solid, "solid"}, {Dash::Dashed, "dashed"}, {Dash::Dotted, "dotted"},
}};
constexpr std::array<EnumName<TextAlign>, 3> kAlignNames{{
    {TextAlign::Left, "left"}, {TextAlign::Center, "center"}, {TextAlign::Right, "right"},
}};
constexpr std::array<EnumName<VAlign>, 3> kVAlignNames{{
    {VAlign::Top, "top"}, {VAlign::Middle, "middle"}, {VAlign::Bottom, "bottom"},
}};
constexpr std::array<EnumName<LabelSide>, 3> kSideNames{{
    {LabelSide::On, "on"}, {LabelSide::Above, "above"}, {LabelSide::Below, "below"},
}};

template <class E, std::size_t N>
const char* nameOf(const std::array<EnumName<E>, N>& table, E value) {
    for (const EnumName<E>& entry : table) {
        if (entry.value == value)
            return entry.name;
    }
    return table[0].name; // out-of-range values cannot occur (documents sanitise them); the first is the default
}

bool equalsLower(std::string_view text, std::string_view lower) {
    if (text.size() != lower.size())
        return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if (c != lower[i])
            return false;
    }
    return true;
}

template <class E, std::size_t N>
std::optional<E> enumFromName(const std::array<EnumName<E>, N>& table, std::string_view text) {
    for (const EnumName<E>& entry : table) {
        if (equalsLower(text, entry.name))
            return entry.value;
    }
    return std::nullopt;
}

// --- values ------------------------------------------------------------------------------------------------------

std::string formatFloat(float value) {
    char buffer[40];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value); // shortest text that reads back exactly
    return std::string(buffer, result.ptr);
}

std::string formatColor(Rgba8 color) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    const std::uint8_t channels[4] = {color.r, color.g, color.b, color.a};
    std::string text(8, '0');
    for (std::size_t i = 0; i < 4; ++i) {
        text[i * 2] = kDigits[channels[i] >> 4];
        text[i * 2 + 1] = kDigits[channels[i] & 0xF];
    }
    return text;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// "RRGGBBAA" or "RRGGBB" (opaque), with an optional leading '#'. Anything else, "auto" included, is nothing.
std::optional<Rgba8> parseColor(std::string_view text) {
    if (!text.empty() && text.front() == '#')
        text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8)
        return std::nullopt;
    std::uint8_t channels[4] = {0, 0, 0, 255};
    for (std::size_t i = 0; i < text.size() / 2; ++i) {
        const int hi = hexDigit(text[i * 2]);
        const int lo = hexDigit(text[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return std::nullopt;
        channels[i] = static_cast<std::uint8_t>(hi << 4 | lo);
    }
    return Rgba8{channels[0], channels[1], channels[2], channels[3]};
}

// Text and labels. One line of INI must hold the whole value and survive the reader's trimming.
std::string escapeText(std::string_view text) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const auto byte = static_cast<unsigned char>(c);
        if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c == ' ' && (i == 0 || i + 1 == text.size()))
            out += "\\s";
        else if (byte < 0x20 || byte == 0x7F) {
            out += "\\x";
            out += kDigits[byte >> 4];
            out += kDigits[byte & 0xF];
        } else
            out += c;
    }
    return out;
}

// The reverse. An unknown escape keeps the character after the backslash; a lone final backslash stays. Output
// stops a little past kMaxTextBytes (the document cuts it properly), so a huge hostile value costs no more.
std::string unescapeText(std::string_view text) {
    std::string out;
    out.reserve(std::min(text.size(), kMaxTextBytes + 8));
    for (std::size_t i = 0; i < text.size() && out.size() <= kMaxTextBytes; ++i) {
        const char c = text[i];
        if (c != '\\' || i + 1 == text.size()) {
            out += c;
            continue;
        }
        const char next = text[++i];
        switch (next) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 's': out += ' '; break;
        case 'x':
            if (i + 2 < text.size() && hexDigit(text[i + 1]) >= 0 && hexDigit(text[i + 2]) >= 0) {
                out += static_cast<char>(hexDigit(text[i + 1]) << 4 | hexDigit(text[i + 2]));
                i += 2;
            } else {
                out += 'x';
            }
            break;
        default: out += next; break;
        }
    }
    return out;
}

// One [section] of the file, with typed readers that fall back to the caller's default on anything malformed.
class Fields {
public:
    explicit Fields(const IniFile::Section& section) : section_(section) {}

    const std::string* get(std::string_view key) const {
        for (const IniFile::Entry& entry : section_.entries) {
            if (entry.key == key)
                return &entry.value;
        }
        return nullptr;
    }

    // Finite numbers only; infinity is allowed through for the document to clamp, NaN and junk are nothing.
    std::optional<float> real(std::string_view key) const {
        const std::string* value = get(key);
        if (!value || value->empty())
            return std::nullopt;
        char* end = nullptr;
        const float parsed = std::strtof(value->c_str(), &end);
        if (end != value->c_str() + value->size() || std::isnan(parsed))
            return std::nullopt;
        return parsed;
    }

    int integer(std::string_view key, int lo, int hi, int fallback) const {
        if (const std::string* value = get(key)) {
            if (const std::optional<int> parsed = parseInt(*value, INT_MIN, INT_MAX))
                return std::clamp(*parsed, lo, hi);
        }
        return fallback;
    }

    // nullopt: the key is missing or is not a whole number that fits.
    std::optional<std::uint32_t> id(std::string_view key) const {
        const std::string* value = get(key);
        if (!value)
            return std::nullopt;
        std::uint32_t parsed = 0;
        const auto [end, ec] = std::from_chars(value->data(), value->data() + value->size(), parsed);
        if (ec != std::errc() || end != value->data() + value->size())
            return std::nullopt;
        return parsed;
    }

    bool boolean(std::string_view key, bool fallback) const {
        if (const std::string* value = get(key)) {
            if (const std::optional<bool> parsed = parseBool(*value))
                return *parsed;
        }
        return fallback;
    }

    std::optional<Rgba8> color(std::string_view key) const {
        const std::string* value = get(key);
        return value ? parseColor(*value) : std::nullopt;
    }

    std::string text(std::string_view key) const {
        const std::string* value = get(key);
        return value ? unescapeText(*value) : std::string();
    }

    template <class E, std::size_t N>
    std::optional<E> named(std::string_view key, const std::array<EnumName<E>, N>& table) const {
        const std::string* value = get(key);
        return value ? enumFromName(table, *value) : std::nullopt;
    }

    template <class E, std::size_t N>
    E named(std::string_view key, const std::array<EnumName<E>, N>& table, E fallback) const {
        return named(key, table).value_or(fallback);
    }

private:
    const IniFile::Section& section_;
};

// "node.12" -> 12 for prefix "node.". Nothing when the name does not start with the prefix or the rest is not a
// plain number.
std::optional<std::uint32_t> idFromName(std::string_view name, std::string_view prefix) {
    if (name.size() <= prefix.size() || name.substr(0, prefix.size()) != prefix)
        return std::nullopt;
    const std::string_view digits = name.substr(prefix.size());
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc() || end != digits.data() + digits.size())
        return std::nullopt;
    return value;
}

bool hasPrefix(std::string_view name, std::string_view prefix) {
    return name.size() >= prefix.size() && name.substr(0, prefix.size()) == prefix;
}

// --- writing -----------------------------------------------------------------------------------------------------

void putColor(IniFile& ini, const std::string& section, const char* key, const std::optional<Rgba8>& color) {
    if (color)
        ini.set(section, key, formatColor(*color));
}

// Nodes always write every style key; edges write the ones they use and the others only when they matter.
void putStyle(IniFile& ini, const std::string& section, const Style& style, const char* widthKey, bool isEdge) {
    constexpr Style kDefault{};
    putColor(ini, section, "fill", style.fill);
    putColor(ini, section, "stroke", style.stroke);
    putColor(ini, section, "text_color", style.textColor);
    ini.set(section, widthKey, formatFloat(style.strokeWidth));
    ini.set(section, "dash", nameOf(kDashNames, style.dash));
    if (!isEdge || style.cornerRadius != kDefault.cornerRadius)
        ini.set(section, "corner_radius", formatFloat(style.cornerRadius));
    ini.set(section, "font_size", formatFloat(style.fontSize));
    if (!isEdge || style.align != kDefault.align)
        ini.set(section, "align", nameOf(kAlignNames, style.align));
    if (style.valign != kDefault.valign) // written only when it is not the default, so older readers and files agree
        ini.set(section, "valign", nameOf(kVAlignNames, style.valign));
}

void putEnd(IniFile& ini, const std::string& section, const char* prefix, const End& end,
            const std::unordered_map<NodeId, std::uint32_t>& rankOf) {
    const std::string p = prefix;
    if (end.node != kNoNode) {
        ini.set(section, p + "node", std::to_string(rankOf.at(end.node)));
        ini.set(section, p + "face", nameOf(kFaceNames, end.face));
    } else {
        ini.set(section, p + "x", formatFloat(end.point.x));
        ini.set(section, p + "y", formatFloat(end.point.y));
    }
}

// --- reading -----------------------------------------------------------------------------------------------------

struct RawNode {
    std::uint32_t fileId = 0;
    NodeKind kind = NodeKind::Rectangle;
    Rect rect{0, 0, 160, 80};
    std::string text;
    Style style;
    std::optional<std::uint32_t> image; // [image.N] it names
    std::uint32_t parent = 0;
    std::uint32_t group = 0;
    int sides = kDefaultPolygonSides;
    int z = 0;
};

Style readStyle(const Fields& f, const char* widthKey) {
    constexpr Style kDefault{};
    Style style;
    style.fill = f.color("fill");
    style.stroke = f.color("stroke");
    style.textColor = f.color("text_color");
    style.strokeWidth = f.real(widthKey).value_or(kDefault.strokeWidth);
    style.dash = f.named("dash", kDashNames, kDefault.dash);
    style.cornerRadius = f.real("corner_radius").value_or(kDefault.cornerRadius);
    style.fontSize = f.real("font_size").value_or(kDefault.fontSize);
    style.align = f.named("align", kAlignNames, kDefault.align);
    style.valign = f.named("valign", kVAlignNames, kDefault.valign);
    return style; // the document clamps every number
}

// Fills `out` from a [node.N] section. False when the kind is missing or unknown (the node is skipped).
bool readNode(const Fields& f, std::uint32_t fileId, int fileOrder, RawNode& out) {
    const std::optional<NodeKind> kind = f.named("kind", kKindNames);
    if (!kind)
        return false;
    out.fileId = fileId;
    out.kind = *kind;
    static const Node kDefault{};
    out.rect = Rect{f.real("x").value_or(0.0f), f.real("y").value_or(0.0f), f.real("w").value_or(kDefault.rect.w),
                    f.real("h").value_or(kDefault.rect.h)};
    out.text = f.text("text");
    out.style = readStyle(f, "stroke_width");
    if (const std::optional<std::uint32_t> image = f.id("image"))
        out.image = *image;
    else if (f.get("image"))
        out.image = UINT32_MAX; // present but not a number: counted as a bad picture index
    out.parent = f.id("parent").value_or(0);
    out.group = f.id("group").value_or(0);
    out.sides = f.integer("sides", kMinPolygonSides, kMaxPolygonSides, kDefaultPolygonSides);
    out.z = f.integer("z", INT_MIN, INT_MAX, fileOrder);
    return true;
}

// Reads one end of an edge. False when it names a node the file does not have (the edge is dropped).
bool readEnd(const Fields& f, const std::string& prefix, const std::unordered_map<std::uint32_t, NodeId>& nodes,
             End& out) {
    out = End{};
    const std::string nodeKey = prefix + "node";
    if (f.get(nodeKey)) {
        const std::optional<std::uint32_t> ref = f.id(nodeKey);
        if (!ref)
            return false;
        if (*ref != 0) {
            const auto found = nodes.find(*ref);
            if (found == nodes.end())
                return false;
            out.node = found->second;
            out.face = f.named(prefix + "face", kFaceNames, Face::Auto);
            return true;
        }
    }
    out.point = Pt{f.real(prefix + "x").value_or(0.0f), f.real(prefix + "y").value_or(0.0f)};
    return true;
}

void parseInto(const IniFile& ini, std::span<Image> images, FlowParse& result) {
    const IniFile::Section* board = ini.section("board");
    if (!board) {
        result.status = FlowParseStatus::NotABoard;
        return;
    }
    if (Fields(*board).integer("version", INT_MIN, INT_MAX, kFlowFormatVersion) > kFlowFormatVersion) {
        result.status = FlowParseStatus::TooNew;
        return;
    }

    FlowDocument& doc = result.doc;
    int& repaired = result.repaired;

    // Pass 1: find the entries. A repeated id keeps its first section; sections that look like entries but are
    // unusable are counted; anything else is ignored (a newer version may add sections).
    std::vector<RawNode> raws;
    std::vector<const IniFile::Section*> edgeSections;
    std::unordered_set<std::uint32_t> seenNodes;
    std::unordered_set<std::uint32_t> seenEdges;
    std::unordered_map<std::uint32_t, std::uint32_t> imageChunks; // [image.N] -> chunk index
    for (const IniFile::Section& section : ini.sections()) {
        if (hasPrefix(section.name, "node.")) {
            const std::optional<std::uint32_t> id = idFromName(section.name, "node.");
            RawNode raw;
            if (!id || *id == 0 || !seenNodes.insert(*id).second || raws.size() >= kMaxNodes ||
                !readNode(Fields(section), *id, static_cast<int>(raws.size()), raw)) {
                ++repaired;
                continue;
            }
            raws.push_back(std::move(raw));
        } else if (hasPrefix(section.name, "edge.")) {
            const std::optional<std::uint32_t> id = idFromName(section.name, "edge.");
            if (!id || *id == 0 || !seenEdges.insert(*id).second || edgeSections.size() >= kMaxEdges) {
                ++repaired;
                continue;
            }
            edgeSections.push_back(&section);
        } else if (hasPrefix(section.name, "image.")) {
            const std::optional<std::uint32_t> id = idFromName(section.name, "image.");
            const std::optional<std::uint32_t> chunk = Fields(section).id("chunk");
            if (!id || !chunk || imageChunks.size() >= kMaxImages) {
                ++repaired;
                continue;
            }
            imageChunks.emplace(*id, *chunk); // the first of a repeated id wins
        }
    }

    // Pictures are added to the document when a node first uses them (an unused one is dropped), and a chunk used
    // by several [image.N] entries becomes one picture.
    std::unordered_map<std::uint32_t, ImageId> imageByKey;
    std::unordered_map<std::uint32_t, ImageId> imageByChunk;
    const auto resolveImage = [&](std::uint32_t key) -> ImageId {
        if (const auto cached = imageByKey.find(key); cached != imageByKey.end())
            return cached->second;
        ImageId id = kNoImage;
        const auto entry = imageChunks.find(key);
        if (entry == imageChunks.end() || entry->second >= images.size()) {
            ++repaired; // no such picture
        } else if (const auto shared = imageByChunk.find(entry->second); shared != imageByChunk.end()) {
            id = shared->second;
        } else if (Image& source = images[entry->second]; !source.empty()) {
            id = doc.addImage(std::make_shared<const Image>(std::move(source)));
            if (id == kNoImage)
                ++repaired; // over the picture cap
            else
                imageByChunk.emplace(entry->second, id);
        } // else the chunk could not be decoded: the caller already counted it
        imageByKey.emplace(key, id);
        return id;
    };

    // Pass 2: nodes, in paint order, so the document hands out z values that keep it.
    std::vector<std::size_t> order(raws.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return raws[a].z < raws[b].z; });

    std::unordered_map<std::uint32_t, NodeId> nodes; // file id -> document id
    std::vector<std::pair<NodeId, std::uint32_t>> parents;
    std::vector<std::uint32_t> groupOrder;
    std::unordered_map<std::uint32_t, std::vector<NodeId>> groupMembers;
    for (const std::size_t index : order) {
        RawNode& raw = raws[index];
        const NodeId id = doc.addNode(raw.kind, raw.rect, std::move(raw.text), raw.style, raw.sides);
        if (id == kNoNode) {
            ++repaired;
            continue;
        }
        nodes.emplace(raw.fileId, id);
        if (raw.kind == NodeKind::Image && raw.image) {
            const ImageId image = resolveImage(*raw.image);
            if (image != kNoImage)
                doc.setNodeImage(id, image);
        }
        if (raw.parent != 0)
            parents.emplace_back(id, raw.parent);
        if (raw.group != 0) {
            std::vector<NodeId>& members = groupMembers[raw.group];
            if (members.empty())
                groupOrder.push_back(raw.group);
            members.push_back(id);
        }
    }

    // Parents: a missing, non-container, self or cyclic parent is refused by the document and counted.
    for (const auto& [child, parentFileId] : parents) {
        const auto parent = nodes.find(parentFileId);
        if (parent == nodes.end() || !doc.reparent(child, parent->second))
            ++repaired;
    }

    // Groups of fewer than two surviving members dissolve.
    for (const std::uint32_t fileGroup : groupOrder) {
        const std::vector<NodeId>& members = groupMembers[fileGroup];
        if (members.size() < 2 || doc.group(members) == kNoGroup)
            ++repaired;
    }

    // Edges, in file order.
    for (const IniFile::Section* section : edgeSections) {
        const Fields f(*section);
        End from;
        End to;
        if (!readEnd(f, "from_", nodes, from) || !readEnd(f, "to_", nodes, to)) {
            ++repaired;
            continue;
        }
        const EdgeId id = doc.connect(from, to);
        if (id == kNoEdge) {
            ++repaired;
            continue;
        }
        static const Edge kDefault{};
        doc.setEdgeRoute(id, f.named("route", kRouteNames, kDefault.route),
                         f.named("arrow_start", kArrowNames, kDefault.arrowStart),
                         f.named("arrow_end", kArrowNames, kDefault.arrowEnd));
        if (f.get("via_x") || f.get("via_y")) // a hand-made route; files from before it have neither key
            doc.setEdgeVia(id, Pt{f.real("via_x").value_or(0.0f), f.real("via_y").value_or(0.0f)});
        doc.setEdgeLabel(id, f.text("label"));
        doc.setEdgeLabelPlacement(id, f.real("label_pos").value_or(kDefault.labelPos),
                                  f.named("label_side", kSideNames, kDefault.labelSide),
                                  f.boolean("label_along", kDefault.labelAlong));
        doc.setEdgeStyle(id, readStyle(f, "width"));
    }
}

} // namespace

FlowSerialized serializeFlow(const FlowDocument& doc) {
    FlowSerialized out;
    IniFile& ini = out.ini;
    ini.set("board", "version", std::to_string(kFlowFormatVersion));

    const std::vector<NodeId> order = doc.nodesByZ();
    std::unordered_map<NodeId, std::uint32_t> rankOf; // document id -> canonical number (1..)
    rankOf.reserve(order.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        rankOf.emplace(order[i], static_cast<std::uint32_t>(i) + 1);

    std::unordered_map<GroupId, std::uint32_t> groupNumber; // numbered by first member in paint order
    std::unordered_map<ImageId, std::uint32_t> imageNumber;

    for (const NodeId id : order) {
        const Node& n = *doc.node(id);
        const std::uint32_t rank = rankOf.at(id);
        const std::string section = "node." + std::to_string(rank);
        ini.set(section, "kind", nameOf(kKindNames, n.kind));
        ini.set(section, "x", formatFloat(n.rect.x));
        ini.set(section, "y", formatFloat(n.rect.y));
        ini.set(section, "w", formatFloat(n.rect.w));
        ini.set(section, "h", formatFloat(n.rect.h));
        if (!n.text.empty())
            ini.set(section, "text", escapeText(n.text));
        putStyle(ini, section, n.style, "stroke_width", false);
        if (n.kind == NodeKind::Image && n.imageId != kNoImage) {
            if (const ImagePtr image = doc.image(n.imageId)) {
                const auto [slot, added] = imageNumber.emplace(n.imageId, static_cast<std::uint32_t>(out.images.size()));
                if (added)
                    out.images.push_back(image);
                ini.set(section, "image", std::to_string(slot->second));
            }
        }
        if (n.parentId != kNoNode) {
            if (const auto parent = rankOf.find(n.parentId); parent != rankOf.end())
                ini.set(section, "parent", std::to_string(parent->second));
        }
        if (n.groupId != kNoGroup) {
            const auto [slot, added] = groupNumber.emplace(n.groupId, static_cast<std::uint32_t>(groupNumber.size()) + 1);
            (void)added;
            ini.set(section, "group", std::to_string(slot->second));
        }
        ini.set(section, "sides", std::to_string(n.sides));
        ini.set(section, "z", std::to_string(rank));
    }

    std::uint32_t edgeNumber = 0;
    for (const Edge& e : doc.edges()) {
        const bool fromOk = e.from.node == kNoNode || rankOf.count(e.from.node);
        const bool toOk = e.to.node == kNoNode || rankOf.count(e.to.node);
        if (!fromOk || !toOk)
            continue; // cannot happen in a valid document
        const std::string section = "edge." + std::to_string(++edgeNumber);
        putEnd(ini, section, "from_", e.from, rankOf);
        putEnd(ini, section, "to_", e.to, rankOf);
        ini.set(section, "route", nameOf(kRouteNames, e.route));
        ini.set(section, "arrow_start", nameOf(kArrowNames, e.arrowStart));
        ini.set(section, "arrow_end", nameOf(kArrowNames, e.arrowEnd));
        if (e.via) {
            ini.set(section, "via_x", formatFloat(e.via->x));
            ini.set(section, "via_y", formatFloat(e.via->y));
        }
        if (!e.label.empty())
            ini.set(section, "label", escapeText(e.label));
        ini.set(section, "label_pos", formatFloat(e.labelPos));
        ini.set(section, "label_side", nameOf(kSideNames, e.labelSide));
        ini.set(section, "label_along", e.labelAlong ? "true" : "false");
        putStyle(ini, section, e.style, "width", true);
    }

    for (std::size_t i = 0; i < out.images.size(); ++i)
        ini.set("image." + std::to_string(i), "chunk", std::to_string(i));
    return out;
}

FlowParse parseFlow(const IniFile& ini, std::span<Image> images) {
    FlowParse result;
    try {
        parseInto(ini, images, result);
    } catch (const std::exception&) { // out of memory: nothing usable
        result = FlowParse{};
        result.status = FlowParseStatus::Failed;
    }
    if (result.status != FlowParseStatus::Ok)
        result.doc = FlowDocument{};
    return result;
}

} // namespace trace::flow
