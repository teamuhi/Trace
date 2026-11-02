#include <doctest/doctest.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "core/document.hpp"
#include "io/image_io.hpp"
#include "io/pixel_export.hpp"

using namespace trace;
namespace fs = std::filesystem;

namespace {

constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kGreen{0, 255, 0, 255};
constexpr Rgba8 kBlue{0, 0, 255, 255};

struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        std::random_device rd;
        path = fs::temp_directory_path() / ("trace_export_test_" + std::to_string(rd()) + "_" + std::to_string(++counter));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path operator/(const std::string& name) const { return path / name; }
};

std::vector<unsigned char> readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The delay (centiseconds) of every frame, read from the Graphic Control Extension blocks (21 F9 04 ...).
std::vector<int> gifDelays(const std::vector<unsigned char>& d) {
    std::vector<int> delays;
    for (std::size_t i = 0; i + 7 < d.size(); ++i) {
        if (d[i] == 0x21 && d[i + 1] == 0xF9 && d[i + 2] == 0x04)
            delays.push_back(d[i + 4] | (d[i + 5] << 8));
    }
    return delays;
}

int channelDiff(Rgba8 a, Rgba8 b) {
    return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b), std::abs(a.a - b.a)});
}

bool sameImage(const Image& a, const Image& b) {
    return a.width() == b.width() && a.height() == b.height() &&
           std::equal(a.pixels().begin(), a.pixels().end(), b.pixels().begin(), b.pixels().end());
}

void paintPixel(Document& doc, int x, int y, Rgba8 color) {
    doc.image().set(x, y, color);
    doc.markDirty(IRect{x, y, x + 1, y + 1});
    doc.commit();
}

// 4 x 3, one layer and three frames with a red, a green and a blue pixel at the top left; the durations are
// 150, 250 and 5 ms.
Document threeFrames() {
    Document doc(4, 3, Rgba8{});
    paintPixel(doc, 0, 0, kRed);
    doc.addFrame(Document::FrameFill::Empty);
    paintPixel(doc, 1, 0, kGreen);
    doc.addFrame(Document::FrameFill::Empty);
    paintPixel(doc, 2, 0, kBlue);
    doc.setFrameDuration(0, 150);
    doc.setFrameDuration(1, 250);
    doc.setFrameDuration(2, 5);
    return doc;
}

// A transparent document of `frames` frames at width x height.
Document emptyFrames(int width, int height, int frames) {
    Document doc(width, height, Rgba8{});
    for (int i = 1; i < frames; ++i)
        REQUIRE(doc.addFrame(Document::FrameFill::Empty) == Document::OpResult::Applied);
    return doc;
}

} // namespace

// --- sizes -----------------------------------------------------------------------------------------

TEST_CASE("export sizes describe what would be written") {
    Document doc = threeFrames();

    const ExportSize png = pngExportSize(doc, 5);
    REQUIRE(png.ok);
    CHECK(png.width == 20);
    CHECK(png.height == 15);
    CHECK(png.frames == 1);

    const ExportSize gif = gifExportSize(doc, 0, 2, 2);
    REQUIRE(gif.ok);
    CHECK(gif.width == 8);
    CHECK(gif.height == 6);
    CHECK(gif.frames == 3);
    CHECK(gifExportSize(doc, 2, 1, 1).frames == 2); // reversed

    SheetOptions options;
    options.mode = SheetMode::Row;
    options.padding = 1;
    options.scale = 2;
    const ExportSize sheet = sheetExportSize(doc, 0, 2, options);
    REQUIRE(sheet.ok);
    CHECK(sheet.width == 1 + 3 * (8 + 1));
    CHECK(sheet.height == 1 + 6 + 1);
    CHECK(sheet.frames == 3);
}

TEST_CASE("export sizes refuse bad scales and sizes past 16384") {
    Document doc = emptyFrames(1024, 8, 2);
    CHECK(pngExportSize(doc, 16).ok); // 16384 exactly
    for (const int scale : {0, -1, 33, INT_MAX, INT_MIN}) {
        const ExportSize png = pngExportSize(doc, scale);
        CHECK_FALSE(png.ok);
        CHECK_FALSE(png.error.empty());
        CHECK_FALSE(gifExportSize(doc, 0, 1, scale).ok);
    }
    const ExportSize tooWide = pngExportSize(doc, 17);
    CHECK_FALSE(tooWide.ok);
    CHECK(tooWide.error.find("16384") != std::string::npos);
    CHECK(tooWide.width == 0);
    CHECK_FALSE(gifExportSize(doc, 0, 1, 32).ok);

    SheetOptions row;
    row.mode = SheetMode::Row;
    row.scale = 8;
    CHECK(sheetExportSize(doc, 0, 1, row).ok); // 2 x 8192 is 16384 exactly
    row.scale = 9;
    const ExportSize sheet = sheetExportSize(doc, 0, 1, row);
    CHECK_FALSE(sheet.ok);
    CHECK(sheet.error.find("16384") != std::string::npos);
}

TEST_CASE("a gif over the memory budget is refused before anything is rendered") {
    Document doc = emptyFrames(600, 600, 2);
    CHECK(gifExportSize(doc, 0, 0, 16).ok);    // one 9600 x 9600 frame
    const ExportSize two = gifExportSize(doc, 0, 1, 16);
    CHECK_FALSE(two.ok);
    CHECK_FALSE(two.error.empty());

    TempDir dir;
    CHECK_FALSE(exportGif(dir / "big.gif", doc, 0, 1, 16).empty());
    CHECK_FALSE(fs::exists(dir / "big.gif"));
}

// --- PNG -------------------------------------------------------------------------------------------

TEST_CASE("exportPng writes the composite of the frame at the scale") {
    TempDir dir;
    Document doc = threeFrames();
    REQUIRE(exportPng(dir / "f1.png", doc, 1, 3).empty());

    LoadResult loaded = loadImage(dir / "f1.png");
    REQUIRE(loaded.image.has_value());
    REQUIRE(loaded.image->width() == 12);
    REQUIRE(loaded.image->height() == 9);
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 12; ++x) {
            const Rgba8 expected = (x >= 3 && x < 6 && y < 3) ? kGreen : Rgba8{};
            CHECK(loaded.image->get(x, y).a == expected.a);
            if (expected.a != 0)
                CHECK(loaded.image->get(x, y) == expected);
        }
    }

    // Scale 1 is the composite itself.
    REQUIRE(exportPng(dir / "f0.png", doc, 0, 1).empty());
    LoadResult first = loadImage(dir / "f0.png");
    REQUIRE(first.image.has_value());
    CHECK(sameImage(*first.image, doc.compositeOfFrame(0)));
    CHECK_FALSE(fs::exists(dir / "f0.png.trace-tmp"));
}

TEST_CASE("exportPng refuses a bad frame or scale and touches no file") {
    TempDir dir;
    Document doc = threeFrames();
    for (const int frame : {-1, 3, 99, INT_MAX, INT_MIN})
        CHECK_FALSE(exportPng(dir / "x.png", doc, frame, 1).empty());
    for (const int scale : {0, 33, -4, INT_MAX})
        CHECK_FALSE(exportPng(dir / "x.png", doc, 0, scale).empty());
    CHECK_FALSE(fs::exists(dir / "x.png"));

    Document wide = emptyFrames(1024, 4, 1);
    const std::string message = exportPng(dir / "wide.png", wide, 0, 32);
    CHECK(message.find("16384") != std::string::npos);
    CHECK_FALSE(fs::exists(dir / "wide.png"));

    // A folder that does not exist is a message, not an exception.
    CHECK_FALSE(exportPng(dir / "no" / "such" / "x.png", doc, 0, 1).empty());
}

TEST_CASE("exports use visible layers only") {
    TempDir dir;
    Document doc = threeFrames();
    doc.setActive(0, 0);
    REQUIRE(doc.addLayer("Top") == Document::OpResult::Applied); // layer 1, active
    doc.setActive(1, 0);
    paintPixel(doc, 3, 2, kBlue);

    // Hidden: the layer adds nothing.
    LayerInfo props = doc.layer(1);
    props.visible = false;
    doc.setLayerProps(1, props);
    REQUIRE(exportPng(dir / "hidden.png", doc, 0, 1).empty());
    Image hidden = *loadImage(dir / "hidden.png").image;
    CHECK(hidden.get(3, 2).a == 0);
    CHECK(hidden.get(0, 0) == kRed);

    // Visible: it is in the composite.
    props.visible = true;
    doc.setLayerProps(1, props);
    REQUIRE(exportPng(dir / "shown.png", doc, 0, 1).empty());
    Image shown = *loadImage(dir / "shown.png").image;
    CHECK(shown.get(3, 2) == kBlue);
    CHECK(sameImage(shown, doc.compositeOfFrame(0)));

    // A half-transparent layer is blended with its opacity.
    props.opacity = 128;
    doc.setLayerProps(1, props);
    REQUIRE(exportPng(dir / "half.png", doc, 0, 1).empty());
    Image half = *loadImage(dir / "half.png").image;
    CHECK(half.get(3, 2).a == doc.compositeOfFrame(0).get(3, 2).a);
    CHECK(half.get(3, 2).a < 255);

    // The sheet and the GIF see the same: hide again and the pixel is gone from the sheet.
    props.visible = false;
    props.opacity = 255;
    doc.setLayerProps(1, props);
    SheetOptions options;
    REQUIRE(exportSpriteSheet(dir / "sheet.png", doc, 0, 0, options).empty());
    CHECK(loadImage(dir / "sheet.png").image->get(3, 2).a == 0);
    REQUIRE(exportGif(dir / "hidden.gif", doc, 0, 0, 1).empty());
    CHECK(loadImage(dir / "hidden.gif").image->get(3, 2).a == 0);
}

// --- GIF -------------------------------------------------------------------------------------------

TEST_CASE("exportGif writes every frame with its own delay") {
    TempDir dir;
    Document doc = threeFrames();
    REQUIRE(exportGif(dir / "a.gif", doc, 0, 2, 1).empty());

    LoadResult loaded = loadImage(dir / "a.gif");
    REQUIRE(loaded.image.has_value());
    CHECK(loaded.frameCount == 3);
    CHECK(loaded.image->width() == 4);
    CHECK(loaded.image->height() == 3);
    CHECK(channelDiff(loaded.image->get(0, 0), kRed) <= 8); // the first frame is the one that opens
    CHECK(loaded.image->get(1, 0).a == 0);
    // 150 ms, 250 ms and 5 ms: 15 cs, 25 cs and the minimum.
    CHECK(gifDelays(readAll(dir / "a.gif")) == std::vector<int>{15, 25, kMinGifDelayCs});
    CHECK_FALSE(fs::exists(dir / "a.gif.trace-tmp"));
}

TEST_CASE("exportGif rounds durations to hundredths of a second") {
    TempDir dir;
    Document doc(2, 2, kRed);
    for (int i = 1; i < 6; ++i)
        REQUIRE(doc.addFrame(Document::FrameFill::Duplicate) == Document::OpResult::Applied);
    const int durations[] = {124, 125, 4, 1, 60000, 1000};
    for (int i = 0; i < 6; ++i)
        doc.setFrameDuration(i, durations[i]);
    REQUIRE(exportGif(dir / "r.gif", doc, 0, 5, 1).empty());
    CHECK(gifDelays(readAll(dir / "r.gif")) == std::vector<int>{12, 13, kMinGifDelayCs, kMinGifDelayCs, 6000, 100});
}

TEST_CASE("exportGif frame ranges are swapped and clamped") {
    TempDir dir;
    Document doc = threeFrames();
    const auto frames = [&](const char* name, int first, int last) {
        const fs::path path = dir / name;
        REQUIRE(exportGif(path, doc, first, last, 1).empty());
        return loadImage(path).frameCount;
    };
    CHECK(frames("one.gif", 1, 1) == 1);
    CHECK(frames("two.gif", 1, 2) == 2);
    CHECK(frames("swapped.gif", 2, 0) == 3);
    CHECK(frames("wide.gif", -5, 99) == 3);
    CHECK(frames("extreme.gif", INT_MIN, INT_MAX) == 3);
    CHECK(frames("reversed_extreme.gif", INT_MAX, INT_MIN) == 3);
    CHECK(frames("past.gif", 7, 9) == 1); // all past the end: the last frame
    CHECK(frames("before.gif", -9, -7) == 1); // all before the start: the first frame
    CHECK(gifDelays(readAll(dir / "one.gif")) == std::vector<int>{25});
    CHECK(gifDelays(readAll(dir / "past.gif")) == std::vector<int>{kMinGifDelayCs});
    CHECK(gifDelays(readAll(dir / "before.gif")) == std::vector<int>{15});
}

TEST_CASE("exportGif scales with nearest sampling") {
    TempDir dir;
    Document doc = threeFrames();
    REQUIRE(exportGif(dir / "s.gif", doc, 0, 0, 5).empty());
    LoadResult loaded = loadImage(dir / "s.gif");
    REQUIRE(loaded.image.has_value());
    CHECK(loaded.image->width() == 20);
    CHECK(loaded.image->height() == 15);
    for (int y = 0; y < 15; ++y) {
        for (int x = 0; x < 20; ++x) {
            if (x < 5 && y < 5)
                CHECK(channelDiff(loaded.image->get(x, y), kRed) <= 8);
            else
                CHECK(loaded.image->get(x, y).a == 0);
        }
    }
    for (const int scale : {0, 33, -1})
        CHECK_FALSE(exportGif(dir / "bad.gif", doc, 0, 0, scale).empty());
    CHECK_FALSE(fs::exists(dir / "bad.gif"));
}

TEST_CASE("exportGif alpha threshold decides what is transparent") {
    TempDir dir;
    Document doc(4, 4, Rgba8{});
    paintPixel(doc, 0, 0, Rgba8{255, 0, 0, 40});  // mostly transparent
    paintPixel(doc, 1, 0, Rgba8{255, 0, 0, 200}); // mostly opaque
    paintPixel(doc, 2, 2, kGreen);                // keeps every frame from being fully transparent
    doc.addFrame(Document::FrameFill::Duplicate);

    REQUIRE(exportGif(dir / "t.gif", doc, 0, 1, 1).empty()); // the default threshold is 128
    Image cut = *loadImage(dir / "t.gif").image;
    CHECK(cut.get(0, 0).a == 0);
    CHECK(cut.get(1, 0).a == 255);
    CHECK(cut.get(3, 3).a == 0);

    REQUIRE(exportGif(dir / "low.gif", doc, 0, 1, 1, 30).empty());
    CHECK(loadImage(dir / "low.gif").image->get(0, 0).a == 255);

    REQUIRE(exportGif(dir / "high.gif", doc, 0, 1, 1, 250).empty());
    CHECK(loadImage(dir / "high.gif").image->get(1, 0).a == 0);

    REQUIRE(exportGif(dir / "opaque.gif", doc, 0, 1, 1, 0).empty()); // 0 keeps everything opaque
    CHECK(loadImage(dir / "opaque.gif").image->get(3, 3).a == 255);

    // Out-of-range thresholds are pulled into range instead of failing.
    CHECK(exportGif(dir / "neg.gif", doc, 0, 1, 1, -50).empty());
    CHECK(exportGif(dir / "big.gif", doc, 0, 1, 1, 9999).empty());
}

TEST_CASE("exportGif leaves an existing file alone when it refuses") {
    TempDir dir;
    Document doc = threeFrames();
    const fs::path path = dir / "keep.gif";
    REQUIRE(exportGif(path, doc, 0, 2, 1).empty());
    const std::vector<unsigned char> before = readAll(path);

    CHECK_FALSE(exportGif(path, doc, 0, 2, 0).empty());
    CHECK_FALSE(exportGif(path, doc, 0, 2, 40).empty());
    CHECK(readAll(path) == before);
    CHECK_FALSE(exportGif(dir / "no" / "such" / "x.gif", doc, 0, 0, 1).empty());
}

// --- sprite sheet ----------------------------------------------------------------------------------

TEST_CASE("exportSpriteSheet writes a row with padding and scale") {
    TempDir dir;
    Document doc = threeFrames();
    SheetOptions options;
    options.mode = SheetMode::Row;
    options.padding = 1;
    options.scale = 2;
    REQUIRE(exportSpriteSheet(dir / "row.png", doc, 0, 2, options).empty());

    LoadResult loaded = loadImage(dir / "row.png");
    REQUIRE(loaded.image.has_value());
    const Image& sheet = *loaded.image;
    REQUIRE(sheet.width() == 1 + 3 * (8 + 1));
    REQUIRE(sheet.height() == 1 + 6 + 1);
    // Frame f is at x0 = 1 + f * 9. Its colored pixel is the source pixel (f, 0): 2 x 2 output pixels.
    CHECK(sheet.get(1, 1) == kRed);
    CHECK(sheet.get(2, 2) == kRed);
    CHECK(sheet.get(3, 1).a == 0);
    CHECK(sheet.get(10 + 2, 1) == kGreen);
    CHECK(sheet.get(10 + 3, 2) == kGreen);
    CHECK(sheet.get(19 + 4, 1) == kBlue);
    CHECK(sheet.get(19 + 5, 2) == kBlue);
    // Padding is transparent: the edge, the gap and a spot inside a cell's empty part.
    CHECK(sheet.get(0, 0).a == 0);
    CHECK(sheet.get(9, 1).a == 0);
    CHECK(sheet.get(1, 7).a == 0);
    CHECK(sheet.get(sheet.width() - 1, 3).a == 0);
}

TEST_CASE("exportSpriteSheet column and grid arrangements") {
    TempDir dir;
    Document doc = threeFrames();
    SheetOptions options;
    options.mode = SheetMode::Column;
    REQUIRE(exportSpriteSheet(dir / "col.png", doc, 0, 2, options).empty());
    Image column = *loadImage(dir / "col.png").image;
    CHECK(column.width() == 4);
    CHECK(column.height() == 9);
    CHECK(column.get(0, 0) == kRed);
    CHECK(column.get(1, 3) == kGreen);
    CHECK(column.get(2, 6) == kBlue);

    options.mode = SheetMode::Grid;
    options.columns = 2;
    REQUIRE(exportSpriteSheet(dir / "grid.png", doc, 0, 2, options).empty());
    Image grid = *loadImage(dir / "grid.png").image;
    CHECK(grid.width() == 8);
    CHECK(grid.height() == 6);
    CHECK(grid.get(0, 0) == kRed);
    CHECK(grid.get(4 + 1, 0) == kGreen);
    CHECK(grid.get(2, 3) == kBlue);
    CHECK(grid.get(5, 4).a == 0); // the unused fourth cell
    CHECK(sheetExportSize(doc, 0, 2, options).width == 8);
}

TEST_CASE("exportSpriteSheet sheet equals the composites placed by hand") {
    TempDir dir;
    Document doc = threeFrames();
    doc.setActive(0, 1);
    paintPixel(doc, 3, 2, Rgba8{9, 8, 7, 100}); // translucent pixels are kept exactly
    SheetOptions options;
    options.mode = SheetMode::Row;
    REQUIRE(exportSpriteSheet(dir / "exact.png", doc, 0, 2, options).empty());
    Image sheet = *loadImage(dir / "exact.png").image;
    REQUIRE(sheet.width() == 12);
    for (int f = 0; f < 3; ++f) {
        const Image composite = doc.compositeOfFrame(f);
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 4; ++x)
                CHECK(sheet.get(f * 4 + x, y) == composite.get(x, y));
        }
    }
}

TEST_CASE("exportSpriteSheet ranges are swapped and clamped, and refusals say why") {
    TempDir dir;
    Document doc = threeFrames();
    SheetOptions options;
    options.mode = SheetMode::Row;
    REQUIRE(exportSpriteSheet(dir / "swapped.png", doc, 2, 1, options).empty());
    CHECK(loadImage(dir / "swapped.png").image->width() == 8);
    REQUIRE(exportSpriteSheet(dir / "extreme.png", doc, INT_MIN, INT_MAX, options).empty());
    CHECK(loadImage(dir / "extreme.png").image->width() == 12);
    REQUIRE(exportSpriteSheet(dir / "past.png", doc, 50, 60, options).empty());
    CHECK(loadImage(dir / "past.png").image->width() == 4);

    // Too wide: 17 frames of 1024 in a row.
    Document wide = emptyFrames(1024, 2, 17);
    const std::string message = exportSpriteSheet(dir / "wide.png", wide, 0, 16, options);
    CHECK(message.find("16384") != std::string::npos);
    CHECK_FALSE(fs::exists(dir / "wide.png"));
    // A grid of 4 columns makes it fit.
    options.mode = SheetMode::Grid;
    options.columns = 4;
    CHECK(sheetExportSize(wide, 0, 16, options).ok);

    SheetOptions badScale;
    badScale.scale = 0;
    CHECK_FALSE(exportSpriteSheet(dir / "bad.png", doc, 0, 2, badScale).empty());
    badScale.scale = 33;
    CHECK_FALSE(exportSpriteSheet(dir / "bad.png", doc, 0, 2, badScale).empty());
    CHECK_FALSE(fs::exists(dir / "bad.png"));
    CHECK_FALSE(exportSpriteSheet(dir / "no" / "such" / "x.png", doc, 0, 2, options).empty());
}

TEST_CASE("export sizes match what the exports write") {
    TempDir dir;
    Document doc = threeFrames();
    SheetOptions options;
    options.mode = SheetMode::Grid;
    options.columns = 2;
    options.padding = 3;
    options.scale = 4;
    const ExportSize predicted = sheetExportSize(doc, 0, 2, options);
    REQUIRE(predicted.ok);
    REQUIRE(exportSpriteSheet(dir / "p.png", doc, 0, 2, options).empty());
    Image sheet = *loadImage(dir / "p.png").image;
    CHECK(sheet.width() == predicted.width);
    CHECK(sheet.height() == predicted.height);

    const ExportSize gif = gifExportSize(doc, 0, 2, 3);
    REQUIRE(exportGif(dir / "p.gif", doc, 0, 2, 3).empty());
    LoadResult loaded = loadImage(dir / "p.gif");
    CHECK(loaded.image->width() == gif.width);
    CHECK(loaded.image->height() == gif.height);
    CHECK(loaded.frameCount == gif.frames);
}
