#include "core/svg_icon.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg.h>
#include <nanosvgrast.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace trace {

namespace {

constexpr std::size_t kMaxSvgBytes = 1u << 20; // an icon larger than this is not an icon

bool isSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

std::string_view trimmed(std::string_view s) noexcept {
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

// class name -> declarations, from every <style> block ("a.b" or "#id" selectors are skipped).
std::map<std::string, std::string> parseClassRules(std::string_view svg) {
    std::map<std::string, std::string> rules;
    std::size_t pos = 0;
    while ((pos = svg.find("<style", pos)) != std::string_view::npos) {
        const std::size_t open = svg.find('>', pos);
        if (open == std::string_view::npos)
            break;
        const std::size_t close = svg.find("</style", open);
        if (close == std::string_view::npos)
            break;
        std::string_view css = svg.substr(open + 1, close - open - 1);
        // CDATA wrapper, if any.
        if (const std::size_t cdata = css.find("<![CDATA["); cdata != std::string_view::npos)
            css.remove_prefix(cdata + 9);
        if (const std::size_t end = css.rfind("]]>"); end != std::string_view::npos)
            css = css.substr(0, end);

        std::size_t at = 0;
        while (at < css.size()) {
            const std::size_t brace = css.find('{', at);
            if (brace == std::string_view::npos)
                break;
            const std::size_t brace2 = css.find('}', brace);
            if (brace2 == std::string_view::npos)
                break;
            const std::string_view selectors = css.substr(at, brace - at);
            const std::string_view body = trimmed(css.substr(brace + 1, brace2 - brace - 1));
            at = brace2 + 1;

            std::size_t s = 0;
            while (s <= selectors.size()) {
                std::size_t comma = selectors.find(',', s);
                if (comma == std::string_view::npos)
                    comma = selectors.size();
                const std::string_view sel = trimmed(selectors.substr(s, comma - s));
                s = comma + 1;
                if (sel.size() < 2 || sel.front() != '.')
                    continue;
                const std::string_view name = sel.substr(1);
                if (name.find_first_of(".#:[ >+~") != std::string_view::npos)
                    continue;
                std::string& decls = rules[std::string(name)];
                if (!decls.empty() && decls.back() != ';')
                    decls += ';';
                decls += body;
            }
        }
        pos = close;
    }
    return rules;
}

// The value of attribute `name` in the start tag text [begin, end), as the range of the value without quotes.
bool findAttribute(std::string_view tag, std::string_view name, std::size_t& valueBegin, std::size_t& valueEnd) {
    std::size_t pos = 0;
    while ((pos = tag.find(name, pos)) != std::string_view::npos) {
        const bool startOk = pos > 0 && isSpace(tag[pos - 1]);
        std::size_t p = pos + name.size();
        while (p < tag.size() && isSpace(tag[p]))
            ++p;
        if (startOk && p < tag.size() && tag[p] == '=') {
            ++p;
            while (p < tag.size() && isSpace(tag[p]))
                ++p;
            if (p < tag.size() && (tag[p] == '"' || tag[p] == '\'')) {
                const char quote = tag[p];
                const std::size_t end = tag.find(quote, p + 1);
                if (end == std::string_view::npos)
                    return false;
                valueBegin = p + 1;
                valueEnd = end;
                return true;
            }
        }
        pos += name.size();
    }
    return false;
}

std::vector<std::string_view> splitClasses(std::string_view list) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && isSpace(list[i]))
            ++i;
        std::size_t j = i;
        while (j < list.size() && !isSpace(list[j]))
            ++j;
        if (j > i)
            out.push_back(list.substr(i, j - i));
        i = j;
    }
    return out;
}

// Rewrites one start tag so that its class rules are in its style attribute.
std::string withInlinedStyle(std::string_view tag, const std::map<std::string, std::string>& rules) {
    std::size_t cb = 0;
    std::size_t ce = 0;
    if (!findAttribute(tag, "class", cb, ce))
        return std::string(tag);
    std::string decls;
    for (const std::string_view cls : splitClasses(tag.substr(cb, ce - cb))) {
        const auto it = rules.find(std::string(cls));
        if (it == rules.end())
            continue;
        decls += it->second;
        if (!decls.empty() && decls.back() != ';')
            decls += ';';
    }
    if (decls.empty())
        return std::string(tag);

    std::size_t sb = 0;
    std::size_t se = 0;
    if (findAttribute(tag, "style", sb, se)) { // the element's own style wins over the sheet
        std::string out(tag.substr(0, sb));
        out += decls;
        out += tag.substr(sb);
        return out;
    }
    std::size_t endPos = tag.size() - 1; // the '>'
    if (endPos > 0 && tag[endPos - 1] == '/')
        --endPos;
    std::string out(tag.substr(0, endPos));
    out += " style=\"";
    out += decls;
    out += '"';
    out += tag.substr(endPos);
    return out;
}

} // namespace

std::string inlineSvgClassStyles(std::string_view svg) {
    const std::map<std::string, std::string> rules = parseClassRules(svg);
    if (rules.empty())
        return std::string(svg);

    std::string out;
    out.reserve(svg.size() + 256);
    std::size_t pos = 0;
    while (pos < svg.size()) {
        const std::size_t lt = svg.find('<', pos);
        if (lt == std::string_view::npos) {
            out.append(svg.substr(pos));
            break;
        }
        out.append(svg.substr(pos, lt - pos));
        // Comments, declarations and processing instructions are copied as they are.
        if (svg.compare(lt, 4, "<!--") == 0) {
            const std::size_t end = svg.find("-->", lt + 4);
            const std::size_t stop = end == std::string_view::npos ? svg.size() : end + 3;
            out.append(svg.substr(lt, stop - lt));
            pos = stop;
            continue;
        }
        // The end of the tag: the first '>' outside quotes.
        char quote = 0;
        std::size_t gt = lt + 1;
        for (; gt < svg.size(); ++gt) {
            const char c = svg[gt];
            if (quote) {
                if (c == quote)
                    quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                break;
            }
        }
        if (gt >= svg.size()) {
            out.append(svg.substr(lt));
            break;
        }
        const std::string_view tag = svg.substr(lt, gt - lt + 1);
        const bool element = tag.size() > 2 && tag[1] != '/' && tag[1] != '!' && tag[1] != '?';
        if (element)
            out += withInlinedStyle(tag, rules);
        else
            out.append(tag);
        pos = gt + 1;
    }
    return out;
}

Image rasterizeSvgCoverage(std::string_view svg, int sizePx) {
    if (sizePx <= 0 || sizePx > 4096 || svg.empty() || svg.size() > kMaxSvgBytes)
        return Image();

    std::string text = inlineSvgClassStyles(svg); // nanosvg parses in place and needs a terminated buffer
    NSVGimage* parsed = nsvgParse(text.data(), "px", 96.0f);
    if (!parsed)
        return Image();
    struct ImageGuard {
        NSVGimage* image;
        ~ImageGuard() { nsvgDelete(image); }
    } guard{parsed};
    if (!(parsed->width > 0.0f) || !(parsed->height > 0.0f))
        return Image();

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    if (!rasterizer)
        return Image();
    struct RasterGuard {
        NSVGrasterizer* r;
        ~RasterGuard() { nsvgDeleteRasterizer(r); }
    } rasterGuard{rasterizer};

    const float side = static_cast<float>(sizePx);
    const float scale = std::min(side / parsed->width, side / parsed->height);
    const float tx = (side - parsed->width * scale) * 0.5f;
    const float ty = (side - parsed->height * scale) * 0.5f;

    Image image(sizePx, sizePx);
    nsvgRasterize(rasterizer, parsed, tx, ty, scale, reinterpret_cast<unsigned char*>(image.pixels().data()), sizePx,
                  sizePx, sizePx * 4);
    for (Rgba8& p : image.pixels()) {
        p.r = 255;
        p.g = 255;
        p.b = 255;
    }
    return image;
}

} // namespace trace
