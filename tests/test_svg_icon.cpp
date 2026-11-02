#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "core/svg_icon.hpp"

using namespace trace;

namespace {

int coverage(const Image& image) {
    int sum = 0;
    for (const Rgba8& p : image.pixels())
        sum += p.a;
    return sum;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("inlineSvgClassStyles: an SVG without a style sheet is unchanged") {
    const std::string svg = R"(<svg viewBox="0 0 8 8"><rect class="a" width="8" height="8"/></svg>)";
    CHECK(inlineSvgClassStyles(svg) == svg);
}

TEST_CASE("inlineSvgClassStyles: class rules become style attributes") {
    const std::string svg = R"(<svg viewBox="0 0 8 8"><style>.a, .b { fill: none; } .b{stroke:#000}</style>)"
                            R"(<rect class="a" width="8" height="8"/><path class="x b" d="M0 0"/>)"
                            R"(<circle class="a" style="stroke:red" r="1"></circle></svg>)";
    const std::string out = inlineSvgClassStyles(svg);
    CHECK(out.find(R"(<rect class="a" width="8" height="8" style="fill: none;"/>)") != std::string::npos);
    CHECK(out.find(R"(style="fill: none;stroke:#000;")") != std::string::npos);
    // An element's own style stays after the sheet's declarations, so it wins.
    CHECK(out.find(R"(style="fill: none;stroke:red")") != std::string::npos);
}

TEST_CASE("inlineSvgClassStyles: selectors other than a plain class are ignored") {
    const std::string svg = R"(<svg><style>g .a, #b, rect.c { fill:none }</style><rect class="a c" id="b"/></svg>)";
    CHECK(inlineSvgClassStyles(svg).find("style=\"") == std::string::npos);
}

TEST_CASE("rasterizeSvgCoverage: a filled square covers its area as white with alpha") {
    const std::string svg = R"(<svg viewBox="0 0 8 8" xmlns="http://www.w3.org/2000/svg">)"
                            R"(<rect x="2" y="2" width="4" height="4" fill="#ff0000"/></svg>)";
    const Image image = rasterizeSvgCoverage(svg, 16);
    REQUIRE(image.width() == 16);
    REQUIRE(image.height() == 16);
    CHECK(image.get(8, 8).a == 255);
    CHECK(image.get(8, 8).r == 255);
    CHECK(image.get(8, 8).g == 255);
    CHECK(image.get(8, 8).b == 255);
    CHECK(image.get(1, 1).a == 0);
    CHECK(image.get(14, 14).a == 0);
}

TEST_CASE("rasterizeSvgCoverage: artwork keeps its aspect ratio and is centered") {
    // A 2 x 1 viewBox in a square image: the artwork fills the width and the middle half of the height.
    const std::string svg = R"(<svg viewBox="0 0 2 1" xmlns="http://www.w3.org/2000/svg"><rect width="2" height="1"/></svg>)";
    const Image image = rasterizeSvgCoverage(svg, 16);
    REQUIRE_FALSE(image.empty());
    CHECK(image.get(8, 8).a == 255);
    CHECK(image.get(8, 2).a == 0);
    CHECK(image.get(8, 13).a == 0);
}

TEST_CASE("rasterizeSvgCoverage: class styles reach the rasterizer") {
    const std::string svg = R"(<svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">)"
                            R"(<style>.s{fill:none;stroke:#000;stroke-width:4}</style>)"
                            R"(<rect class="s" x="4" y="4" width="8" height="8"/></svg>)";
    const Image image = rasterizeSvgCoverage(svg, 16);
    REQUIRE_FALSE(image.empty());
    CHECK(image.get(4, 8).a == 255); // on the outline
    CHECK(image.get(8, 8).a == 0);   // the inside stays empty
}

TEST_CASE("rasterizeSvgCoverage: unusable input gives an empty image") {
    const std::string svg = R"(<svg viewBox="0 0 8 8"><rect width="8" height="8"/></svg>)";
    CHECK(rasterizeSvgCoverage(svg, 0).empty());
    CHECK(rasterizeSvgCoverage(svg, -4).empty());
    CHECK(rasterizeSvgCoverage("", 16).empty());
    CHECK(rasterizeSvgCoverage("not an svg", 16).empty());
}

TEST_CASE("every icon file in src/assets/icons draws something at the sizes the UI uses") {
    namespace fs = std::filesystem;
    const fs::path dir = TRACE_ICONS_DIR;
    REQUIRE(fs::is_directory(dir));
    int checked = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".svg")
            continue;
        const std::string name = entry.path().filename().string();
        const std::string svg = readFile(entry.path());
        INFO(name);
        // An embedded bitmap cannot be drawn by the rasterizer; it needs to be replaced by vector artwork.
        if (svg.find("<image") != std::string::npos)
            continue;
        for (const int size : {16, 24, 40}) {
            const Image image = rasterizeSvgCoverage(svg, size);
            CHECK_FALSE(image.empty());
            CHECK(coverage(image) > size * 2);
        }
        ++checked;
    }
    CHECK(checked > 100);
}
