#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "core/cel_grid.hpp"
#include "core/document.hpp"
#include "core/transform.hpp"
#include "io/pixel_project.hpp"
#include "io/project_io.hpp"

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
        path = fs::temp_directory_path() / ("trace_pixel_test_" + std::to_string(rd()) + "_" + std::to_string(++counter));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

bool sameImage(const Image& a, const Image& b) {
    return a.width() == b.width() && a.height() == b.height() &&
           std::equal(a.pixels().begin(), a.pixels().end(), b.pixels().begin(), b.pixels().end());
}

void paintPixel(Document& doc, int x, int y, Rgba8 color) {
    doc.image().set(x, y, color);
    doc.markDirty(IRect{x, y, x + 1, y + 1});
    doc.commit();
}

// A document with three layers and two frames, some properties changed and a few pixels in different cels.
Document buildDocument() {
    Document doc(8, 6, Rgba8{});
    paintPixel(doc, 1, 1, kRed);
    doc.addLayer("Sky");
    paintPixel(doc, 2, 2, kGreen);
    doc.addLayer("Top");
    LayerInfo props = doc.layer(2);
    props.name = "Top layer";
    props.visible = false;
    props.opacity = 100;
    props.locked = true;
    doc.setLayerProps(2, props);
    doc.addFrame(Document::FrameFill::Duplicate);
    doc.setFrameDuration(1, 250);
    paintPixel(doc, 7, 5, kBlue); // on (layer 2, frame 1)
    doc.setActive(1, 1);
    return doc;
}

// Document -> project bytes -> project -> cel grid -> a second document.
bool roundTrip(Document& source, Document& target, int* damaged = nullptr) {
    const ProjectWriteResult written = writeProject(pixelProjectData(source));
    REQUIRE_MESSAGE(!written.bytes.empty(), written.error);
    ProjectReadResult read = readProject(written.bytes, "test.tpix");
    REQUIRE_MESSAGE(read.project.has_value(), read.error);
    PixelProjectLoad load = pixelProjectToCels(std::move(*read.project), "test.tpix");
    REQUIRE_MESSAGE(load.store.has_value(), load.error);
    if (damaged)
        *damaged = load.damagedCels;
    return target.loadStore(std::move(*load.store));
}

// A minimal valid META for hand-built hostile projects.
ProjectData baseProject(int width = 4, int height = 4, int layers = 1, int frames = 1) {
    ProjectData data;
    data.kind = kProjectPixel;
    data.meta.set("doc", "width", std::to_string(width));
    data.meta.set("doc", "height", std::to_string(height));
    data.meta.set("doc", "layers", std::to_string(layers));
    data.meta.set("doc", "frames", std::to_string(frames));
    return data;
}

PixelProjectLoad decode(ProjectData data) {
    return pixelProjectToCels(std::move(data), "evil.tpix");
}

} // namespace

// --- round trip ----------------------------------------------------------------------------------

TEST_CASE("A pixel project round trip keeps layers, frames, cels and the active position") {
    Document source = buildDocument();
    Document back(1, 1);
    REQUIRE(roundTrip(source, back));

    REQUIRE(back.layerCount() == source.layerCount());
    REQUIRE(back.frameCount() == source.frameCount());
    CHECK(back.activeLayer() == 1);
    CHECK(back.activeFrame() == 1);
    for (int l = 0; l < source.layerCount(); ++l) {
        CHECK(back.layer(l).name == source.layer(l).name);
        CHECK(back.layer(l).visible == source.layer(l).visible);
        CHECK(back.layer(l).opacity == source.layer(l).opacity);
        CHECK(back.layer(l).locked == source.layer(l).locked);
        for (int f = 0; f < source.frameCount(); ++f) {
            CAPTURE(l);
            CAPTURE(f);
            CHECK(sameImage(back.cel(l, f), source.cel(l, f)));
        }
    }
    CHECK(back.frame(0).durationMs == source.frame(0).durationMs);
    CHECK(back.frame(1).durationMs == 250);
    CHECK(back.cel(2, 1).get(7, 5) == kBlue);
}

TEST_CASE("A loaded project is unmodified, has no history and fresh ids") {
    Document source = buildDocument();
    Document back(1, 1);
    REQUIRE(roundTrip(source, back));
    CHECK_FALSE(back.isModified());
    CHECK_FALSE(back.canUndo());
    CHECK_FALSE(back.canRedo());

    // Editing and layer operations carry on from the loaded state.
    paintPixel(back, 0, 0, kRed);
    CHECK(back.isModified());
    CHECK(back.addLayer("More") == Document::OpResult::Applied);
    CHECK(back.layerCount() == 4);
    CHECK(back.undo());
    CHECK(back.layerCount() == 3);
}

TEST_CASE("Fully transparent cels are not written, and come back transparent") {
    Document doc(16, 16, Rgba8{});
    doc.addLayer();
    doc.addLayer();
    doc.addFrame(Document::FrameFill::Empty); // 3 layers x 2 frames = 6 cels
    paintPixel(doc, 3, 3, kRed);              // one cel with content
    const ProjectData data = pixelProjectData(doc);
    CHECK(data.images.size() == 1);
    CHECK(data.meta.section("cel")->entries.size() == 1);

    Document back(1, 1);
    REQUIRE(roundTrip(doc, back));
    int visible = 0;
    for (int l = 0; l < back.layerCount(); ++l)
        for (int f = 0; f < back.frameCount(); ++f)
            for (const Rgba8& p : back.cel(l, f).pixels())
                visible += p.a != 0 ? 1 : 0;
    CHECK(visible == 1);
}

TEST_CASE("A blank document writes no pictures at all") {
    Document doc(32, 32, Rgba8{});
    const ProjectData data = pixelProjectData(doc);
    CHECK(data.images.empty());
    Document back(1, 1);
    REQUIRE(roundTrip(doc, back));
    CHECK(back.cel(0, 0).width() == 32);
    CHECK(back.layerCount() == 1);
}

TEST_CASE("The largest canvas round trips") {
    Document doc(kMaxPixelCanvasSide, kMaxPixelCanvasSide, Rgba8{});
    paintPixel(doc, kMaxPixelCanvasSide - 1, kMaxPixelCanvasSide - 1, kRed);
    Document back(1, 1);
    REQUIRE(roundTrip(doc, back));
    CHECK(back.cel(0, 0).width() == kMaxPixelCanvasSide);
    CHECK(back.cel(0, 0).get(kMaxPixelCanvasSide - 1, kMaxPixelCanvasSide - 1) == kRed);
}

TEST_CASE("Layer names with odd characters survive") {
    Document doc(4, 4, Rgba8{});
    doc.addLayer("Ünïcode = name ; with # marks");
    Document back(1, 1);
    REQUIRE(roundTrip(doc, back));
    CHECK(back.layer(1).name == "Ünïcode = name ; with # marks");
}

TEST_CASE("Saving and loading a file keeps the document") {
    TempDir dir;
    const fs::path path = dir.path / "art.tpix";
    Document source = buildDocument();
    CHECK(savePixelProject(path, source).empty());

    PixelProjectLoad load = loadPixelProject(path);
    REQUIRE_MESSAGE(load.store.has_value(), load.error);
    CHECK(load.damagedCels == 0);
    Document back(1, 1);
    REQUIRE(back.loadStore(std::move(*load.store)));
    CHECK(sameImage(back.cel(0, 0), source.cel(0, 0)));
    CHECK(sameImage(back.cel(1, 1), source.cel(1, 1)));

    CHECK_FALSE(loadPixelProject(dir.path / "missing.tpix").store.has_value());
    CHECK_FALSE(savePixelProject(dir.path / "no" / "such" / "dir" / "x.tpix", source).empty());
}

// --- hostile and damaged projects ----------------------------------------------------------------

TEST_CASE("A project of another kind is refused") {
    ProjectData data = baseProject();
    data.kind = kProjectFlow;
    const PixelProjectLoad load = decode(std::move(data));
    CHECK_FALSE(load.store.has_value());
    CHECK_FALSE(load.error.empty());
}

TEST_CASE("A project without a [doc] section has nothing usable") {
    ProjectData data;
    data.kind = kProjectPixel;
    data.meta.set("layer.0", "name", "orphan");
    data.images.push_back(Image(4, 4, kRed));
    const PixelProjectLoad load = decode(std::move(data));
    CHECK_FALSE(load.store.has_value());
    CHECK_FALSE(load.error.empty());
}

TEST_CASE("A canvas beyond 1024 pixels is refused with the limit in the message") {
    for (const auto& [w, h] : {std::pair{1025, 8}, std::pair{8, 1025}, std::pair{16384, 16384}, std::pair{2000000000, 1}}) {
        CAPTURE(w);
        CAPTURE(h);
        const PixelProjectLoad load = decode(baseProject(w, h));
        CHECK_FALSE(load.store.has_value());
        CHECK(load.error.find("1024") != std::string::npos);
    }
}

TEST_CASE("Counts beyond the limits are refused, not trimmed") {
    for (const int layers : {0, -1, kMaxLayers + 1, 1000000}) {
        CAPTURE(layers);
        CHECK_FALSE(decode(baseProject(4, 4, layers, 1)).store.has_value());
    }
    for (const int frames : {0, -7, kMaxFrames + 1}) {
        CAPTURE(frames);
        CHECK_FALSE(decode(baseProject(4, 4, 1, frames)).store.has_value());
    }
    ProjectData garbage = baseProject();
    garbage.meta.set("doc", "layers", "many");
    CHECK_FALSE(decode(std::move(garbage)).store.has_value());
    ProjectData huge = baseProject();
    huge.meta.set("doc", "frames", "99999999999999999999");
    CHECK_FALSE(decode(std::move(huge)).store.has_value());
}

TEST_CASE("A project that would need too much memory is refused before anything is allocated") {
    const PixelProjectLoad load = decode(baseProject(1024, 1024, 200, 1));
    CHECK_FALSE(load.store.has_value());
    CHECK_FALSE(load.error.empty());
    CHECK_FALSE(decode(baseProject(1024, 1024, 256, 999)).store.has_value());
    // Within the budget it opens: 128 layers of 1024 x 1024 is exactly the limit.
    CHECK(fitsDocumentBudget(1024, 1024, 128, 1));
    CHECK_FALSE(fitsDocumentBudget(1024, 1024, 129, 1));
    CHECK_FALSE(fitsDocumentBudget(0, 8, 1, 1));
    CHECK_FALSE(fitsDocumentBudget(8, 8, 0, 1));
}

TEST_CASE("A missing canvas size is taken from the first picture, or refused without one") {
    ProjectData data;
    data.kind = kProjectPixel;
    data.meta.set("doc", "layers", "1");
    data.images.push_back(Image());          // an unreadable chunk comes first
    data.images.push_back(Image(5, 3, kRed));
    data.meta.set("cel", "0,0", "1");
    const PixelProjectLoad load = decode(std::move(data));
    REQUIRE(load.store.has_value());
    CHECK(load.store->pool.begin()->second.width() == 5);
    CHECK(load.store->pool.begin()->second.height() == 3);

    ProjectData none;
    none.kind = kProjectPixel;
    none.meta.set("doc", "layers", "1");
    CHECK_FALSE(decode(std::move(none)).store.has_value());
}

TEST_CASE("Cels that are missing, out of range or the wrong size become transparent and are counted") {
    ProjectData data = baseProject(4, 4, 2, 2);
    data.images.push_back(Image(4, 4, kRed));   // 0: good
    data.images.push_back(Image(2, 2, kGreen)); // 1: wrong size
    data.images.push_back(Image());             // 2: undecodable
    data.meta.set("cel", "0,0", "0");
    data.meta.set("cel", "1,0", "1");   // wrong size
    data.meta.set("cel", "0,1", "2");   // empty picture
    data.meta.set("cel", "1,1", "42");  // no such picture
    data.meta.set("cel", "5,5", "0");   // no such cel: ignored, not counted
    data.meta.set("cel", "-1,0", "0");
    data.meta.set("cel", "x,y", "0");
    PixelProjectLoad load = decode(std::move(data));
    REQUIRE(load.store.has_value());
    CHECK(load.damagedCels == 3);

    Document doc(1, 1);
    REQUIRE(doc.loadStore(std::move(*load.store)));
    CHECK(doc.cel(0, 0).get(0, 0) == kRed);
    for (const auto& [l, f] : {std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}}) {
        CAPTURE(l);
        CAPTURE(f);
        REQUIRE(doc.cel(l, f).width() == 4);
        CHECK(doc.cel(l, f).get(1, 1).a == 0);
    }
}

TEST_CASE("One picture used by two cels is copied for both") {
    ProjectData data = baseProject(4, 4, 2, 1);
    data.images.push_back(Image(4, 4, kBlue));
    data.meta.set("cel", "0,0", "0");
    data.meta.set("cel", "1,0", "0");
    PixelProjectLoad load = decode(std::move(data));
    REQUIRE(load.store.has_value());
    CHECK(load.damagedCels == 0);
    Document doc(1, 1);
    REQUIRE(doc.loadStore(std::move(*load.store)));
    CHECK(doc.cel(0, 0).get(2, 2) == kBlue);
    CHECK(doc.cel(1, 0).get(2, 2) == kBlue);
}

TEST_CASE("Unknown keys and sections are ignored and missing metadata gets defaults") {
    ProjectData data = baseProject(4, 4, 2, 2);
    data.meta.set("future", "feature", "yes");
    data.meta.set("doc", "palette", "ignored");
    data.meta.set("layer.0", "blend", "multiply");
    data.meta.set("layer.7", "name", "beyond the count");
    PixelProjectLoad load = decode(std::move(data));
    REQUIRE(load.store.has_value());
    Document doc(1, 1);
    REQUIRE(doc.loadStore(std::move(*load.store)));
    CHECK(doc.layerCount() == 2);
    CHECK(doc.frameCount() == 2);
    CHECK(doc.layer(0).visible);
    CHECK(doc.layer(0).opacity == 255);
    CHECK_FALSE(doc.layer(0).locked);
    CHECK(doc.layer(1).name == "Layer 2"); // no name in the file: named after its id
    CHECK(doc.frame(0).durationMs == kDefaultFrameMs);
    CHECK(doc.activeLayer() == 0);
    CHECK(doc.activeFrame() == 0);
}

TEST_CASE("Out-of-range layer, frame and active values are clamped") {
    ProjectData data = baseProject(4, 4, 2, 2);
    data.meta.set("layer.0", "opacity", "999");
    data.meta.set("layer.1", "opacity", "-5");
    data.meta.set("layer.0", "visible", "maybe");
    data.meta.set("layer.1", "locked", "yes");
    data.meta.set("frame.0", "duration_ms", "0");
    data.meta.set("frame.1", "duration_ms", "999999");
    data.meta.set("doc", "active_layer", "17");
    data.meta.set("doc", "active_frame", "-3");
    PixelProjectLoad load = decode(std::move(data));
    REQUIRE(load.store.has_value());
    Document doc(1, 1);
    REQUIRE(doc.loadStore(std::move(*load.store)));
    CHECK(doc.layer(0).opacity == 255);
    CHECK(doc.layer(1).opacity == 0);
    CHECK(doc.layer(0).visible); // not a boolean: the default stays
    CHECK(doc.layer(1).locked);
    CHECK(doc.frame(0).durationMs == kMinFrameMs);
    CHECK(doc.frame(1).durationMs == kMaxFrameMs);
    CHECK(doc.activeLayer() == 1);
    CHECK(doc.activeFrame() == 0);
}

TEST_CASE("Hostile layer names are cleaned") {
    ProjectData data = baseProject(4, 4, 2, 1);
    data.meta.set("layer.0", "name", std::string("  ") + std::string(300, 'x') + "  ");
    data.meta.set("layer.1", "name", "\x01\x02   \x7f");
    PixelProjectLoad load = decode(std::move(data));
    REQUIRE(load.store.has_value());
    Document doc(1, 1);
    REQUIRE(doc.loadStore(std::move(*load.store)));
    CHECK(doc.layer(0).name.size() == kMaxLayerNameBytes);
    CHECK(doc.layer(1).name == "Layer 2");
}

TEST_CASE("Truncated and corrupted project files never crash the reader") {
    Document doc = buildDocument();
    const ProjectWriteResult written = writeProject(pixelProjectData(doc));
    REQUIRE(!written.bytes.empty());

    const auto attempt = [](const std::vector<unsigned char>& bytes) {
        ProjectReadResult read = readProject(bytes, "fuzz.tpix");
        if (!read.project)
            return;
        PixelProjectLoad load = pixelProjectToCels(std::move(*read.project), "fuzz.tpix");
        if (load.store) {
            Document back(1, 1);
            back.loadStore(std::move(*load.store)); // may be refused, must not crash
        }
    };

    for (std::size_t len = 0; len < written.bytes.size(); len += std::max<std::size_t>(1, written.bytes.size() / 97))
        attempt(std::vector<unsigned char>(written.bytes.begin(), written.bytes.begin() + static_cast<std::ptrdiff_t>(len)));

    std::mt19937 rng(12345);
    for (int round = 0; round < 300; ++round) {
        std::vector<unsigned char> copy = written.bytes;
        const int flips = 1 + static_cast<int>(rng() % 6);
        for (int i = 0; i < flips; ++i)
            copy[rng() % copy.size()] = static_cast<unsigned char>(rng());
        attempt(copy);
    }
    for (int round = 0; round < 50; ++round) { // pure noise behind a valid header
        std::vector<unsigned char> noise(written.bytes.begin(), written.bytes.begin() + 16);
        for (int i = 0; i < 200; ++i)
            noise.push_back(static_cast<unsigned char>(rng()));
        attempt(noise);
    }
}

// --- Document::loadStore -------------------------------------------------------------------------

namespace {

CelStore twoByOne() {
    CelStore store;
    store.layout.layers = {LayerInfo{1, "A", true, 255, false}, LayerInfo{2, "B", true, 255, false}};
    store.layout.frames = {FrameInfo{}};
    store.layout.grid = {1, 2};
    store.pool.emplace(1, Image(3, 3));
    store.pool.emplace(2, Image(3, 3));
    return store;
}

} // namespace

TEST_CASE("loadStore accepts a consistent grid and continues numbering after its ids") {
    Document doc(5, 5);
    REQUIRE(doc.loadStore(twoByOne()));
    CHECK(doc.layerCount() == 2);
    CHECK(doc.image().width() == 3);
    CHECK(doc.addLayer("C") == Document::OpResult::Applied);
    CHECK(doc.layer(doc.activeLayer()).id == 3);
    CHECK(doc.celId(doc.activeLayer(), 0) == 3);
}

TEST_CASE("loadStore refuses inconsistent input and leaves the document alone") {
    Document doc(5, 5);
    paintPixel(doc, 1, 1, kRed);
    const std::uint64_t generation = doc.generation();

    const auto refused = [&](CelStore store) {
        CHECK_FALSE(doc.loadStore(std::move(store)));
        CHECK(doc.image().width() == 5);
        CHECK(doc.image().get(1, 1) == kRed);
        CHECK(doc.generation() == generation);
    };

    refused(CelStore{});                                       // nothing at all
    { CelStore s = twoByOne(); s.layout.grid = {1, 1}; refused(std::move(s)); }       // the same cel twice
    { CelStore s = twoByOne(); s.layout.grid = {1, 0}; refused(std::move(s)); }       // no cel
    { CelStore s = twoByOne(); s.layout.grid = {1, 9}; refused(std::move(s)); }       // a cel that is not in the pool
    { CelStore s = twoByOne(); s.pool.emplace(7, Image(3, 3)); refused(std::move(s)); } // an unused cel
    { CelStore s = twoByOne(); s.pool.at(2) = Image(4, 3); refused(std::move(s)); }   // sizes differ
    { CelStore s = twoByOne(); s.pool.at(2) = Image(); refused(std::move(s)); }       // empty cel
    { CelStore s = twoByOne(); s.layout.layers[1].id = 1; refused(std::move(s)); }    // repeated layer id
    { CelStore s = twoByOne(); s.layout.layers[0].id = 0; refused(std::move(s)); }    // layer id zero
    { CelStore s = twoByOne(); s.layout.activeLayer = 2; refused(std::move(s)); }     // active out of range
    { CelStore s = twoByOne(); s.layout.activeFrame = -1; refused(std::move(s)); }
    { CelStore s = twoByOne(); s.layout.frames.clear(); refused(std::move(s)); }      // no frames
}

TEST_CASE("loadStore enforces the layer and frame limits") {
    CelStore store;
    store.layout.frames = {FrameInfo{}};
    CelId id = 1;
    for (int i = 0; i < kMaxLayers + 1; ++i) {
        store.layout.layers.push_back(LayerInfo{static_cast<std::uint32_t>(i + 1), "L", true, 255, false});
        store.layout.grid.push_back(id);
        store.pool.emplace(id++, Image(1, 1));
    }
    Document doc(2, 2);
    CHECK_FALSE(doc.loadStore(std::move(store)));
}

TEST_CASE("loadStore cleans names and clamps durations") {
    CelStore store = twoByOne();
    store.layout.layers[0].name = "  tidy\t ";
    store.layout.layers[1].name = std::string(500, 'n');
    store.layout.frames[0].durationMs = -40;
    Document doc(2, 2);
    REQUIRE(doc.loadStore(std::move(store)));
    CHECK(doc.layer(0).name == "tidy");
    CHECK(doc.layer(1).name.size() == kMaxLayerNameBytes);
    CHECK(doc.frame(0).durationMs == kMinFrameMs);
}

TEST_CASE("Document::revision changes whenever the picture on screen can have changed") {
    Document doc(4, 4, Rgba8{});
    std::uint64_t seen = doc.revision();
    const auto changed = [&] {
        const bool differs = doc.revision() != seen;
        seen = doc.revision();
        return differs;
    };
    CHECK_FALSE(changed());
    paintPixel(doc, 1, 1, kRed);
    CHECK(changed());
    doc.addLayer();
    CHECK(changed());
    doc.undo();
    CHECK(changed());
    doc.setActive(0, 0);
    (void)changed();
    doc.displayImage(); // reading does not count
    CHECK_FALSE(changed());
    REQUIRE(doc.loadStore(twoByOne()));
    CHECK(changed());
}

// --- thumbnails ----------------------------------------------------------------------------------

TEST_CASE("thumbnailOf copies a small image and shrinks a large one keeping the aspect") {
    const Image small(10, 6, kRed);
    CHECK(sameImage(thumbnailOf(small, 64), small));

    Image big(1024, 512, Rgba8{});
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 1024; ++x)
            big.set(x, y, kRed);
    const Image thumb = thumbnailOf(big, 64);
    CHECK(thumb.width() == 64);
    CHECK(thumb.height() == 32);
    for (const Rgba8& p : thumb.pixels())
        CHECK(p == kRed);

    CHECK(thumbnailOf(Image(), 64).empty());
    const Image tall = thumbnailOf(Image(3, 4000, kRed), 64);
    CHECK(tall.height() == 64);
    CHECK(tall.width() == 1); // never below one pixel
    CHECK(thumbnailOf(big, 0).width() == 1);      // a bad size is raised to 1
}

TEST_CASE("thumbnailOf averages by alpha, so transparent pixels never tint the color") {
    Image img(128, 128, Rgba8{});
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 64; ++x)
            img.set(x, y, kGreen); // left half green, right half transparent
    const Image thumb = thumbnailOf(img, 8);
    REQUIRE(thumb.width() == 8);
    CHECK(thumb.get(0, 3) == kGreen);
    CHECK(thumb.get(7, 3).a == 0);
    for (const Rgba8& p : thumb.pixels()) {
        if (p.a != 0)
            CHECK(p.g == 255); // the color of a covered pixel is the real color
    }
}
