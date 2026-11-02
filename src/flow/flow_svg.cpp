#include "flow/flow_svg.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "flow/flow_geometry.hpp"

namespace trace::flow {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxNum = 1.0e7;              // nothing printed is larger than this
constexpr double kDefaultPadding = 24.0;
constexpr double kMaxPadding = 1.0e6;
constexpr std::size_t kMaxTextLines = 400;      // as on the board
constexpr double kLineHeight = 1.15;            // line pitch in em
constexpr double kAscent = 0.8;                 // baseline below the top of a line, in em
constexpr float kTextPad = 8.0f;                // board units between a node's outline and its text (as on the board)
constexpr double kLabelPad = 4.0;               // label box margin around its text (as on the board)
// Images are embedded as uncompressed PNGs, so a few large ones would make a huge file. Beyond this much embedded
// base64 text in total, further images are drawn as the placeholder box instead.
constexpr std::size_t kMaxEmbeddedBytes = 64u * 1024u * 1024u;

constexpr Rgba8 kStickyInk{52, 44, 18, 255};
constexpr Rgba8 kDarkText{32, 33, 36, 255};
constexpr Rgba8 kLightText{244, 244, 246, 255};

// --- numbers ------------------------------------------------------------------------------------------------------
// Printed with std::to_chars: locale independent, never "nan" or "inf".
std::string num(double v, int decimals = 2) {
    if (!std::isfinite(v))
        v = 0.0;
    v = std::clamp(v, -kMaxNum, kMaxNum);
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed, decimals);
    std::string s(buf, r.ptr);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0')
            s.pop_back();
        if (s.back() == '.')
            s.pop_back();
    }
    if (s == "-0" || s.empty())
        s = "0";
    return s;
}

double finiteOr(double v, double fallback) { return std::isfinite(v) ? v : fallback; }

void appendPt(std::string& s, Pt p) {
    s += num(p.x);
    s += ' ';
    s += num(p.y);
}

// --- text: UTF-8 cleaning and XML escaping ---------------------------------------------------------------------------
bool xmlAllowed(char32_t c) {
    if (c == 0x9 || c == 0xA || c == 0xD)
        return true;
    if (c < 0x20 || (c >= 0x7F && c <= 0x9F))
        return false;
    return c <= 0xD7FF || (c >= 0xE000 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0x10FFFF);
}

// Decodes UTF-8; every invalid byte becomes U+FFFD and characters XML 1.0 forbids are dropped, so the result can be
// written into a document unchanged.
std::u32string cleanText(std::string_view in) {
    std::u32string out;
    out.reserve(in.size());
    std::size_t i = 0;
    while (i < in.size()) {
        const auto b0 = static_cast<unsigned char>(in[i]);
        char32_t cp = 0xFFFD;
        std::size_t len = 1;
        if (b0 < 0x80) {
            cp = b0;
        } else {
            int need = 0;
            char32_t minCp = 0;
            char32_t acc = 0;
            if ((b0 & 0xE0) == 0xC0) { need = 1; acc = b0 & 0x1F; minCp = 0x80; }
            else if ((b0 & 0xF0) == 0xE0) { need = 2; acc = b0 & 0x0F; minCp = 0x800; }
            else if ((b0 & 0xF8) == 0xF0) { need = 3; acc = b0 & 0x07; minCp = 0x10000; }
            if (need > 0 && i + static_cast<std::size_t>(need) < in.size()) {
                bool ok = true;
                for (int k = 1; k <= need; ++k) {
                    const auto bk = static_cast<unsigned char>(in[i + static_cast<std::size_t>(k)]);
                    if ((bk & 0xC0) != 0x80) { ok = false; break; }
                    acc = (acc << 6) | (bk & 0x3F);
                }
                if (ok && acc >= minCp && acc <= 0x10FFFF && !(acc >= 0xD800 && acc <= 0xDFFF)) {
                    cp = acc;
                    len = static_cast<std::size_t>(need) + 1;
                }
            }
        }
        i += len;
        if (xmlAllowed(cp))
            out.push_back(cp);
    }
    return out;
}

void appendUtf8(std::string& s, char32_t c) {
    if (c < 0x80) {
        s += static_cast<char>(c);
    } else if (c < 0x800) {
        s += static_cast<char>(0xC0 | (c >> 6));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        s += static_cast<char>(0xE0 | (c >> 12));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (c >> 18));
        s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// Appends already-cleaned text with the five XML entities escaped (valid in text nodes and in attribute values).
void appendEscaped(std::string& s, std::u32string_view text) {
    for (const char32_t c : text) {
        switch (c) {
        case U'&': s += "&amp;"; break;
        case U'<': s += "&lt;"; break;
        case U'>': s += "&gt;"; break;
        case U'"': s += "&quot;"; break;
        case U'\'': s += "&apos;"; break;
        default: appendUtf8(s, c); break;
        }
    }
}

std::string escaped(std::string_view text) {
    std::string s;
    appendEscaped(s, cleanText(text));
    return s;
}

// The font-family attribute value: the user's name(s) first, then always a generic family. Escaped.
std::string fontFamilyValue(const std::string& requested) {
    std::u32string f = cleanText(requested);
    f.erase(std::remove_if(f.begin(), f.end(), [](char32_t c) { return c == U'\n' || c == U'\r' || c == U'\t'; }), f.end());
    if (f.size() > 200)
        f.resize(200);
    std::string family;
    appendEscaped(family, f);
    // Does the last comma-separated entry already name a generic family?
    std::string last = family.substr(family.find_last_of(',') == std::string::npos ? 0 : family.find_last_of(',') + 1);
    last.erase(0, last.find_first_not_of(' '));
    while (!last.empty() && last.back() == ' ')
        last.pop_back();
    std::transform(last.begin(), last.end(), last.begin(), [](char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + 32) : ch; });
    static constexpr std::string_view generics[] = {"serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui"};
    for (const std::string_view g : generics)
        if (last == g)
            return family;
    return family.empty() ? std::string("sans-serif") : family + ", sans-serif";
}

// --- colors ---------------------------------------------------------------------------------------------------------
std::string hex(Rgba8 c) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string s = "#";
    for (const std::uint8_t v : {c.r, c.g, c.b}) {
        s += digits[v >> 4];
        s += digits[v & 15];
    }
    return s;
}

Rgba8 alphaScaled(Rgba8 c, double f) {
    c.a = static_cast<std::uint8_t>(std::clamp(std::lround(static_cast<double>(c.a) * f), 0l, 255l));
    return c;
}

Rgba8 mix(Rgba8 a, Rgba8 b, double t) {
    const auto ch = [&](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(x + (y - x) * t), 0l, 255l));
    };
    return Rgba8{ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), ch(a.a, b.a)};
}

double luminance(Rgba8 c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b; }

// ` fill="#rrggbb" fill-opacity="0.5"`, or ` fill="none"` when fully transparent. `name` is "fill" or "stroke".
std::string paint(const char* name, Rgba8 c) {
    std::string s = " ";
    s += name;
    if (c.a == 0)
        return s + "=\"none\"";
    s += "=\"" + hex(c) + "\"";
    if (c.a < 255)
        s += std::string(" ") + name + "-opacity=\"" + num(c.a / 255.0, 3) + "\"";
    return s;
}

double strokeWidthOf(float w) { return std::isfinite(w) ? std::clamp<double>(w, 0.5, 32.0) : 2.0; }
double fontSizeOf(float s) { return std::isfinite(s) ? std::clamp<double>(s, 6.0, 200.0) : 16.0; }

// Stroke attributes with the board's dash patterns (dashPolyline in flow_view.cpp).
std::string strokeAttrs(Rgba8 c, double sw, Dash dash) {
    std::string s = paint("stroke", c);
    if (c.a == 0)
        return s;
    s += " stroke-width=\"" + num(sw) + "\"";
    if (dash == Dash::Dashed)
        s += " stroke-dasharray=\"" + num(std::max(sw * 4.0, 7.0)) + " " + num(std::max(sw * 2.5, 5.0)) + "\"";
    else if (dash == Dash::Dotted)
        s += " stroke-dasharray=\"" + num(std::max(sw, 1.5)) + " " + num(std::max(sw * 2.0, 4.0)) + "\"";
    return s;
}

// --- text layout (estimated) ------------------------------------------------------------------------------------------
// The pure library has no font metrics, so widths are ESTIMATED: 0.55 em for an average glyph and 1 em for wide
// (CJK, full-width, emoji) glyphs. Wrapping therefore differs a little from the board, which measures the real font,
// and a viewer that substitutes another font may break lines differently from what is written here.
double glyphEm(char32_t c) {
    if (c < 0x20 || (c >= 0x300 && c <= 0x36F) || (c >= 0x200B && c <= 0x200F) || (c >= 0xFE00 && c <= 0xFE0F))
        return 0.0;
    const bool wide = (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) ||
                      (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE6F) || (c >= 0xFF00 && c <= 0xFF60) ||
                      (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1FAFF) || (c >= 0x20000 && c <= 0x3FFFD);
    return wide ? 1.0 : 0.55;
}

double lineWidth(std::u32string_view line, double size) {
    double w = 0.0;
    for (const char32_t c : line)
        w += glyphEm(c) * size;
    return w;
}

// Splits at explicit newlines and (wrapWidth > 0) greedily at spaces so each line fits the width; a word wider than
// the line is broken between glyphs. Capped at kMaxTextLines.
std::vector<std::u32string> layoutLines(const std::u32string& text, double size, double wrapWidth) {
    std::vector<std::u32string> lines;
    std::size_t p = 0;
    while (lines.size() < kMaxTextLines) {
        std::size_t nl = text.find(U'\n', p);
        const std::size_t end = nl == std::u32string::npos ? text.size() : nl;
        std::u32string para;
        for (std::size_t i = p; i < end; ++i)
            if (text[i] != U'\r')
                para.push_back(text[i] == U'\t' ? U' ' : text[i]);
        if (para.empty() || wrapWidth <= 0.0) {
            lines.push_back(std::move(para));
        } else {
            std::size_t s = 0;
            while (s < para.size() && lines.size() < kMaxTextLines) {
                double w = 0.0;
                std::size_t i = s;
                std::size_t lastSpace = std::u32string::npos;
                while (i < para.size()) {
                    if (para[i] == U' ')
                        lastSpace = i;
                    const double gw = glyphEm(para[i]) * size;
                    if (w + gw > wrapWidth && i > s)
                        break;
                    w += gw;
                    ++i;
                }
                std::size_t e = i;
                std::size_t next = i;
                if (i < para.size() && lastSpace != std::u32string::npos && lastSpace > s) {
                    e = lastSpace;
                    next = lastSpace + 1;
                }
                std::size_t t = e;
                while (t > s && para[t - 1] == U' ')
                    --t;
                lines.push_back(para.substr(s, t - s));
                s = next;
                while (s < para.size() && para[s] == U' ')
                    ++s;
            }
        }
        if (nl == std::u32string::npos)
            break;
        p = nl + 1;
    }
    return lines;
}

// Where a node's text goes (board units); mirrors textAreaOf in flow_view.cpp.
Rect textAreaOf(const Node& n) {
    Rect r = n.rect;
    float padX = kTextPad;
    float padY = kTextPad;
    const auto shrink = [&](float f) {
        const Pt ctr = r.center();
        r.w *= f;
        r.h *= f;
        r.x = ctr.x - r.w * 0.5f;
        r.y = ctr.y - r.h * 0.5f;
    };
    switch (n.kind) {
    case NodeKind::Ellipse:
    case NodeKind::Polygon:
    case NodeKind::Circle: shrink(0.7071f); break;
    case NodeKind::Diamond: shrink(0.5f); break;
    case NodeKind::Hexagon: {
        const float k = std::min(r.w * 0.25f, r.h * 0.5f);
        r.x += k;
        r.w -= 2.0f * k;
        break;
    }
    case NodeKind::Parallelogram: {
        const float k = std::min(r.w * 0.25f, r.h * 0.4f);
        r.x += k;
        r.w -= 2.0f * k;
        break;
    }
    case NodeKind::Document: r.h -= std::min(r.h * 0.1f, 16.0f) * 2.0f; break;
    case NodeKind::Database: {
        const float cap = std::clamp(std::min(r.h * 0.15f, r.w * 0.25f), 0.0f, r.h * 0.5f);
        r.y += cap * 1.6f;
        r.h -= cap * 2.6f;
        break;
    }
    case NodeKind::Rounded: padX += std::min(n.style.cornerRadius, std::min(r.w, r.h) * 0.5f) * 0.3f; break;
    case NodeKind::Terminator: padX += r.h * 0.2f; break;
    case NodeKind::Text:
        padX = 2.0f;
        padY = 2.0f;
        break;
    default: break;
    }
    r.x += padX;
    r.y += padY;
    r.w = std::max(r.w - 2.0f * padX, 1.0f);
    r.h = std::max(r.h - 2.0f * padY, 1.0f);
    return r;
}

// --- PNG (uncompressed) and base64 ---------------------------------------------------------------------------------------
// trace_flow links only trace_core (trace_io links trace_flow), so the stb encoder is out of reach; this writes a valid
// PNG whose zlib stream uses "stored" blocks. Larger than a deflated file, with no dependency.
constexpr std::array<std::uint32_t, 256> makeCrcTable() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[n] = c;
    }
    return t;
}
constexpr std::array<std::uint32_t, 256> kCrcTable = makeCrcTable();

std::uint32_t crc32(std::uint32_t crc, const std::uint8_t* data, std::size_t n) {
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i)
        crc = kCrcTable[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

void putBE32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

void putChunk(std::vector<std::uint8_t>& png, const char type[4], const std::vector<std::uint8_t>& data) {
    putBE32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = png.size();
    for (int i = 0; i < 4; ++i)
        png.push_back(static_cast<std::uint8_t>(type[i]));
    png.insert(png.end(), data.begin(), data.end());
    putBE32(png, crc32(0, png.data() + start, png.size() - start));
}

// Size of the PNG pngFor() would produce (an upper bound), so a huge image is refused before it is allocated.
std::size_t pngSizeEstimate(const Image& img) {
    const std::size_t raw = static_cast<std::size_t>(img.height()) * (1u + 4u * static_cast<std::size_t>(img.width()));
    return raw + raw / 65535u * 5u + 128u;
}

std::vector<std::uint8_t> encodePng(const Image& img) {
    const std::size_t w = static_cast<std::size_t>(img.width());
    const std::size_t h = static_cast<std::size_t>(img.height());
    bool opaque = true;
    for (const Rgba8 p : img.pixels())
        if (p.a != 255) {
            opaque = false;
            break;
        }
    const std::size_t bpp = opaque ? 3 : 4;
    std::vector<std::uint8_t> raw;
    raw.reserve(h * (1 + w * bpp));
    for (std::size_t y = 0; y < h; ++y) {
        raw.push_back(0); // filter: none
        for (const Rgba8 p : img.row(static_cast<int>(y))) {
            raw.push_back(p.r);
            raw.push_back(p.g);
            raw.push_back(p.b);
            if (!opaque)
                raw.push_back(p.a);
        }
    }

    std::vector<std::uint8_t> z{0x78, 0x01};
    std::size_t pos = 0;
    do {
        const std::size_t len = std::min<std::size_t>(65535, raw.size() - pos);
        const bool last = pos + len == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(len & 0xFF));
        z.push_back(static_cast<std::uint8_t>(len >> 8));
        z.push_back(static_cast<std::uint8_t>(~len & 0xFF));
        z.push_back(static_cast<std::uint8_t>((~len >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
    } while (pos < raw.size());
    std::uint32_t a = 1, b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    putBE32(z, (b << 16) | a);

    std::vector<std::uint8_t> ihdr;
    putBE32(ihdr, static_cast<std::uint32_t>(w));
    putBE32(ihdr, static_cast<std::uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, static_cast<std::uint8_t>(opaque ? 2 : 6), 0, 0, 0});

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    putChunk(png, "IHDR", ihdr);
    putChunk(png, "IDAT", z);
    putChunk(png, "IEND", {});
    return png;
}

std::string base64(const std::vector<std::uint8_t>& in) {
    static constexpr char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const std::uint32_t v = (std::uint32_t{in[i]} << 16) | (std::uint32_t{in[i + 1]} << 8) | in[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
    }
    if (i + 1 == in.size()) {
        const std::uint32_t v = std::uint32_t{in[i]} << 16;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        const std::uint32_t v = (std::uint32_t{in[i]} << 16) | (std::uint32_t{in[i + 1]} << 8);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

// --- the exporter -----------------------------------------------------------------------------------------------------------
struct Bounds {
    double x0 = std::numeric_limits<double>::max();
    double y0 = std::numeric_limits<double>::max();
    double x1 = std::numeric_limits<double>::lowest();
    double y1 = std::numeric_limits<double>::lowest();
    bool any = false;
    void add(double x, double y, double margin = 0.0) {
        x = finiteOr(x, 0.0);
        y = finiteOr(y, 0.0);
        x0 = std::min(x0, x - margin);
        y0 = std::min(y0, y - margin);
        x1 = std::max(x1, x + margin);
        y1 = std::max(y1, y + margin);
        any = true;
    }
    void add(Pt p, double margin = 0.0) { add(p.x, p.y, margin); }
    void add(const Rect& r, double margin = 0.0) {
        add(r.x, r.y, margin);
        add(r.right(), r.bottom(), margin);
    }
};

class Exporter {
public:
    Exporter(const FlowDocument& doc, const SvgOptions& opt) : doc_(doc), opt_(opt) {}

    std::string run() {
        const bool filtered = !opt_.only.empty();
        std::unordered_set<NodeId> inSet;
        if (filtered)
            for (const NodeId id : opt_.only)
                if (doc_.node(id))
                    inSet.insert(id);

        for (const NodeId id : doc_.nodesByZ()) {
            if (filtered && !inSet.count(id))
                continue;
            if (const Node* n = doc_.node(id))
                drawNode(*n);
        }
        const NodeLookup lookup = [&](NodeId id) { return doc_.node(id); };
        for (const Edge& e : doc_.edges()) {
            if (filtered && !edgeSelected(e, inSet))
                continue;
            drawEdge(e, lookup);
        }
        return assemble();
    }

private:
    const FlowDocument& doc_;
    const SvgOptions& opt_;
    std::string body_;
    std::string defs_;
    Bounds bounds_;
    int nextClip_ = 1;
    std::size_t embedded_ = 0;
    std::unordered_map<ImageId, std::string> uriCache_;

    // Docked ends must be in the set; a connector with both ends free belongs to no selection.
    static bool edgeSelected(const Edge& e, const std::unordered_set<NodeId>& inSet) {
        const bool fromOk = e.from.node == kNoNode || inSet.count(e.from.node);
        const bool toOk = e.to.node == kNoNode || inSet.count(e.to.node);
        return fromOk && toOk && (e.from.node != kNoNode || e.to.node != kNoNode);
    }

    std::string assemble() const {
        const double pad = std::clamp(finiteOr(opt_.padding, kDefaultPadding), 0.0, kMaxPadding);
        Bounds b = bounds_;
        if (!b.any)
            b.add(0.0, 0.0);
        const double x = std::clamp(b.x0, -kMaxNum, kMaxNum) - pad;
        const double y = std::clamp(b.y0, -kMaxNum, kMaxNum) - pad;
        const double w = std::max(std::clamp(b.x1, -kMaxNum, kMaxNum) - std::clamp(b.x0, -kMaxNum, kMaxNum) + 2.0 * pad, 1.0);
        const double h = std::max(std::clamp(b.y1, -kMaxNum, kMaxNum) - std::clamp(b.y0, -kMaxNum, kMaxNum) + 2.0 * pad, 1.0);

        std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        s += "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" version=\"1.1\" width=\"" +
             num(w) + "\" height=\"" + num(h) + "\" viewBox=\"" + num(x) + " " + num(y) + " " + num(w) + " " + num(h) + "\">\n";
        if (!defs_.empty())
            s += "<defs>\n" + defs_ + "</defs>\n";
        if (opt_.background && opt_.background->a > 0)
            s += "<rect x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\"" + num(h) + "\"" +
                 paint("fill", *opt_.background) + "/>\n";
        if (!body_.empty())
            s += "<g font-family=\"" + fontFamilyValue(opt_.fontFamily) + "\">\n" + body_ + "</g>\n";
        s += "</svg>\n";
        return s;
    }

    // --- colors of a node (the rules of fillOf / strokeOf / textOf on the board, with the options for Auto) ---
    Rgba8 fillOf(const Node& n) const {
        if (n.style.fill)
            return *n.style.fill;
        switch (n.kind) {
        case NodeKind::Sticky: return opt_.stickyFill;
        case NodeKind::Container: return alphaScaled(opt_.paperAuto, 0.45);
        default: return opt_.paperAuto;
        }
    }
    Rgba8 strokeOf(const Node& n) const {
        if (n.style.stroke)
            return *n.style.stroke;
        switch (n.kind) {
        case NodeKind::Sticky: return mix(opt_.stickyFill, Rgba8{0, 0, 0, 255}, 0.28);
        case NodeKind::Container: return alphaScaled(opt_.inkAuto, 0.6);
        default: return opt_.inkAuto;
        }
    }
    Rgba8 textOf(const Node& n) const {
        if (n.style.textColor)
            return *n.style.textColor;
        if (n.kind == NodeKind::Sticky && !n.style.fill)
            return kStickyInk;
        if (n.style.fill && n.kind != NodeKind::Container && n.kind != NodeKind::Text && n.style.fill->a > 100)
            return luminance(*n.style.fill) > 150.0 ? kDarkText : kLightText;
        return opt_.inkAuto;
    }
    static bool hasFill(const Node& n) {
        return n.style.fill || (n.kind != NodeKind::Text && n.kind != NodeKind::Image);
    }

    // --- elements ---
    void rect(const Rect& r, const std::string& attrs) {
        body_ += "<rect x=\"" + num(r.x) + "\" y=\"" + num(r.y) + "\" width=\"" + num(r.w) + "\" height=\"" + num(r.h) + "\"" +
                 attrs + "/>\n";
    }

    void path(const std::vector<Pt>& pts, bool closed, const std::string& attrs) {
        if (pts.size() < 2)
            return;
        std::string d = "M";
        for (std::size_t i = 0; i < pts.size(); ++i) {
            if (i > 0)
                d += 'L';
            appendPt(d, pts[i]);
        }
        if (closed)
            d += 'Z';
        body_ += "<path d=\"" + d + "\"" + attrs + "/>\n";
    }

    // A block of text in `area`: lines aligned inside it, placed at the top, middle or bottom, clipped to it only when
    // the estimate says it overflows (so a slightly wrong estimate never cuts glyphs off).
    void textBlock(const std::string& text, const Rect& area, double fontSize, TextAlign align, VAlign valign, Rgba8 color,
                   bool wrap) {
        if (text.empty() || color.a == 0 || !(area.w >= 1.0f) || !(area.h >= 1.0f))
            return;
        const std::vector<std::u32string> lines = layoutLines(cleanText(text), fontSize, wrap ? area.w : 0.0);
        const double lineH = fontSize * kLineHeight;
        const double blockH = static_cast<double>(lines.size()) * lineH;
        bool overflow = blockH > area.h;
        bool any = false;
        for (const std::u32string& ln : lines) {
            any = any || !ln.empty();
            overflow = overflow || lineWidth(ln, fontSize) > area.w;
        }
        if (!any)
            return;
        double x = area.x + area.w * 0.5;
        const char* anchor = "middle";
        if (align == TextAlign::Left) {
            x = area.x;
            anchor = "start";
        } else if (align == TextAlign::Right) {
            x = area.right();
            anchor = "end";
        }
        double top = area.y;
        if (blockH < area.h)
            top += valign == VAlign::Top ? 0.0 : (valign == VAlign::Bottom ? area.h - blockH : (area.h - blockH) * 0.5);

        if (overflow) {
            const std::string id = "c" + std::to_string(nextClip_++);
            defs_ += "<clipPath id=\"" + id + "\"><rect x=\"" + num(area.x) + "\" y=\"" + num(area.y) + "\" width=\"" +
                     num(area.w) + "\" height=\"" + num(area.h) + "\"/></clipPath>\n";
            body_ += "<g clip-path=\"url(#" + id + ")\">";
        }
        body_ += "<text font-size=\"" + num(fontSize) + "\"" + paint("fill", color) + " text-anchor=\"" + anchor +
                 "\" xml:space=\"preserve\">";
        writeTspans(lines, x, top, fontSize, lineH);
        body_ += "</text>";
        body_ += overflow ? "</g>\n" : "\n";
    }

    // No whitespace between the tspans: with xml:space="preserve" it would become glyphs and shift centered lines.
    void writeTspans(const std::vector<std::u32string>& lines, double x, double top, double fontSize, double lineH) {
        double y = top;
        for (const std::u32string& ln : lines) {
            if (!ln.empty()) {
                body_ += "<tspan x=\"" + num(x) + "\" y=\"" + num(y + kAscent * fontSize) + "\">";
                appendEscaped(body_, ln);
                body_ += "</tspan>";
            }
            y += lineH;
        }
    }

    // --- nodes ---
    void drawNode(const Node& n) {
        const Rect r = sanitize(n.rect);
        const double sw = strokeWidthOf(n.style.strokeWidth);
        const double fs = fontSizeOf(n.style.fontSize);
        const Rgba8 fill = fillOf(n);
        const Rgba8 stroke = strokeOf(n);
        bounds_.add(r, sw * 0.5);

        // Too small to show detail: a dot of the outline color, as on the board.
        if (r.w < 3.0f && r.h < 3.0f) {
            rect(Rect{r.x, r.y, std::max(r.w, 2.0f), std::max(r.h, 2.0f)}, paint("fill", n.style.stroke ? stroke : opt_.inkAuto));
            bounds_.add(r.x + 2.0, r.y + 2.0);
            return;
        }
        const std::string strokeA = strokeAttrs(stroke, sw, n.style.dash);

        switch (n.kind) {
        case NodeKind::Text:
            if (n.style.fill)
                rect(r, paint("fill", fill));
            break;
        case NodeKind::Image:
            if (n.style.fill)
                rect(r, paint("fill", fill));
            drawImage(n, r);
            if (n.style.stroke)
                rect(r, " fill=\"none\"" + strokeAttrs(stroke, sw, Dash::Solid));
            return; // images carry no text
        case NodeKind::Container: {
            const Rect band = containerTitleBand(r);
            path(nodeOutline(n), true, paint("fill", fill) + " stroke=\"none\"");
            const Rgba8 bandCol = n.style.fill ? mix(fill, Rgba8{128, 128, 128, 255}, 0.18)
                                               : mix(alphaScaled(opt_.paperAuto, 0.95), opt_.inkAuto, 0.12);
            rect(band, paint("fill", bandCol));
            path(nodeOutline(n), true, " fill=\"none\"" + strokeA);
            const double lw = std::max(1.0, sw * 0.6);
            path({Pt{band.x, band.bottom()}, Pt{band.right(), band.bottom()}}, false,
                 " fill=\"none\"" + strokeAttrs(alphaScaled(stroke, 0.7), lw, Dash::Solid));
            textBlock(n.text, Rect{band.x + 10.0f, band.y, std::max(band.w - 20.0f, 1.0f), band.h}, fs, n.style.align,
                      n.style.valign, textOf(n), false);
            return;
        }
        case NodeKind::Sticky:
            // A soft shadow lifts the note off the board.
            rect(Rect{r.x + 2.0f, r.y + 4.0f, r.w, r.h}, paint("fill", Rgba8{0, 0, 0, 38}));
            bounds_.add(r.right() + 2.0, r.bottom() + 4.0);
            [[fallthrough]];
        default: {
            const std::vector<Pt> outline = nodeOutline(n);
            path(outline, true, (hasFill(n) ? paint("fill", fill) : std::string(" fill=\"none\"")) + strokeA);
            if (n.kind == NodeKind::Database)
                path(databaseRim(r), false, " fill=\"none\"" + strokeA);
            break;
        }
        }

        if (!n.text.empty())
            textBlock(n.text, textAreaOf(n), fs, n.style.align, n.style.valign, textOf(n), true);
    }

    void drawImage(const Node& n, const Rect& r) {
        const std::string* uri = imageUri(n.imageId);
        if (uri) {
            embedded_ += uri->size();
            body_ += "<image x=\"" + num(r.x) + "\" y=\"" + num(r.y) + "\" width=\"" + num(r.w) + "\" height=\"" + num(r.h) +
                     "\" preserveAspectRatio=\"none\" xlink:href=\"" + *uri + "\"/>\n";
            return;
        }
        // Missing, empty or over budget: a faint box with a cross, as on the board.
        const Rgba8 faint = alphaScaled(opt_.inkAuto, 0.35);
        rect(r, paint("fill", alphaScaled(opt_.inkAuto, 0.06)) + strokeAttrs(faint, 1.0, Dash::Solid));
        const std::string line = " fill=\"none\"" + strokeAttrs(faint, 1.0, Dash::Solid);
        path({Pt{r.x, r.y}, Pt{r.right(), r.bottom()}}, false, line);
        path({Pt{r.x, r.bottom()}, Pt{r.right(), r.y}}, false, line);
    }

    // The data URI of an image, or nullptr when there is none or the size budget is spent.
    const std::string* imageUri(ImageId id) {
        if (id == kNoImage)
            return nullptr;
        const auto cached = uriCache_.find(id);
        if (cached != uriCache_.end())
            return embedded_ + cached->second.size() <= kMaxEmbeddedBytes ? &cached->second : nullptr;
        const ImagePtr img = doc_.image(id);
        if (!img || img->empty())
            return nullptr;
        const std::size_t est = pngSizeEstimate(*img);
        if (embedded_ + est / 3 * 4 + 64 > kMaxEmbeddedBytes)
            return nullptr;
        std::string uri = "data:image/png;base64," + base64(encodePng(*img));
        return &uriCache_.emplace(id, std::move(uri)).first->second;
    }

    // --- connectors ---
    static std::vector<Pt> trimPolyline(std::vector<Pt> p, float head, float tail) {
        const float total = polylineLength(p);
        if (!(head > 0.0f || tail > 0.0f) || !(total > (head + tail) * 1.2f + 1.0f))
            return p;
        const auto trimBack = [](std::vector<Pt>& v, float len) {
            while (len > 0.0f && v.size() >= 2) {
                const Pt a = v[v.size() - 2];
                const Pt b = v.back();
                const float l = std::hypot(b.x - a.x, b.y - a.y);
                if (l > len) {
                    const float t = len / l;
                    v.back() = Pt{b.x + (a.x - b.x) * t, b.y + (a.y - b.y) * t};
                    return;
                }
                len -= l;
                v.pop_back();
            }
        };
        trimBack(p, tail);
        std::reverse(p.begin(), p.end());
        trimBack(p, head);
        std::reverse(p.begin(), p.end());
        return p;
    }

    void arrowHead(const ArrowHead& h, Rgba8 color, double sw) {
        if (h.points.size() < 2)
            return;
        for (const Pt& p : h.points)
            bounds_.add(p, sw * 0.5);
        if (h.filled && h.closed)
            path(h.points, true, paint("fill", color) + " stroke=\"none\"");
        else
            path(h.points, h.closed, " fill=\"none\"" + strokeAttrs(color, sw, Dash::Solid));
    }

    void drawEdge(const Edge& e, const NodeLookup& lookup) {
        const std::optional<EdgePath> routed = routeEdge(e, lookup);
        if (!routed || routed->points.size() < 2)
            return;
        const EdgePath& ep = *routed;
        const double sw = strokeWidthOf(e.style.strokeWidth);
        const Rgba8 color = e.style.stroke ? *e.style.stroke : opt_.inkAuto;
        const ArrowHead hs = arrowHeadFor(ep, true, e.arrowStart, static_cast<float>(sw));
        const ArrowHead he = arrowHeadFor(ep, false, e.arrowEnd, static_cast<float>(sw));
        const std::vector<Pt> line = trimPolyline(ep.points, hs.inset, he.inset);
        for (const Pt& p : line)
            bounds_.add(p, sw * 0.5);
        path(line, false, " fill=\"none\"" + strokeAttrs(color, sw, e.style.dash));
        arrowHead(hs, color, sw);
        arrowHead(he, color, sw);
        drawLabel(e, ep);
    }

    void drawLabel(const Edge& e, const EdgePath& ep) {
        if (e.label.empty())
            return;
        const double fs = fontSizeOf(e.style.fontSize);
        const std::vector<std::u32string> lines = layoutLines(cleanText(e.label), fs, 0.0);
        double w = 0.0;
        bool any = false;
        for (const std::u32string& ln : lines) {
            w = std::max(w, lineWidth(ln, fs));
            any = any || !ln.empty();
        }
        if (!any)
            return;
        const double lineH = fs * kLineHeight;
        const double h = static_cast<double>(lines.size()) * lineH;
        const double halfW = w * 0.5 + kLabelPad;
        const double halfH = h * 0.5 + kLabelPad;
        const float t = std::isfinite(e.labelPos) ? std::clamp(e.labelPos, 0.0f, 1.0f) : 0.5f;
        const LabelPlacement lp = placeLabel(ep.points, t, e.labelAlong, e.labelSide, static_cast<float>(halfW), static_cast<float>(halfH));
        const double cx = finiteOr(lp.center.x, 0.0);
        const double cy = finiteOr(lp.center.y, 0.0);
        const double angle = finiteOr(lp.angle, 0.0);
        const double ca = std::cos(angle);
        const double sa = std::sin(angle);
        for (int i = 0; i < 4; ++i) {
            const double lx = (i == 0 || i == 3) ? -halfW : halfW;
            const double ly = i < 2 ? -halfH : halfH;
            bounds_.add(cx + lx * ca - ly * sa, cy + lx * sa + ly * ca);
        }

        const double deg = angle * 180.0 / kPi;
        const bool turned = std::abs(deg) >= 0.005;
        if (turned)
            body_ += "<g transform=\"rotate(" + num(deg) + " " + num(cx) + " " + num(cy) + ")\">\n";
        if (e.labelSide == LabelSide::On) {
            // The label interrupts the line: a plate in the background (or paper) color behind the text.
            const Rgba8 plate = alphaScaled(opt_.background ? *opt_.background : opt_.paperAuto, 0.92);
            rect(Rect{static_cast<float>(cx - halfW), static_cast<float>(cy - halfH), static_cast<float>(halfW * 2.0),
                      static_cast<float>(halfH * 2.0)},
                 paint("fill", plate));
        }
        const Rgba8 textColor = e.style.textColor ? *e.style.textColor : opt_.inkAuto;
        if (textColor.a > 0) {
            body_ += "<text font-size=\"" + num(fs) + "\"" + paint("fill", textColor) +
                     " text-anchor=\"middle\" xml:space=\"preserve\">";
            writeTspans(lines, cx, cy - h * 0.5, fs, lineH);
            body_ += "</text>\n";
        }
        if (turned)
            body_ += "</g>\n";
    }
};

} // namespace

std::string exportSvg(const FlowDocument& doc, const SvgOptions& options) {
    return Exporter(doc, options).run();
}

} // namespace trace::flow
