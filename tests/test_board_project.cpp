#include <doctest/doctest.h>

#include <filesystem>
#include <random>
#include <string>

#include "core/document.hpp"
#include "io/board_project.hpp"
#include "io/pixel_project.hpp"
#include "io/project_io.hpp"

using namespace trace;
namespace fs = std::filesystem;

namespace {

constexpr Rgba8 kRed{255, 0, 0, 255};

struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        std::random_device rd;
        path = fs::temp_directory_path() / ("trace_board_test_" + std::to_string(rd()) + "_" + std::to_string(++counter));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

} // namespace

TEST_CASE("A whiteboard saves and loads with its pixels, origin, layers and paper") {
    TempDir dir;
    Document doc(64, 48, Rgba8{});
    doc.setEndless(true);
    doc.image().set(5, 6, kRed);
    doc.markDirty(IRect{5, 6, 6, 7});
    doc.commit();
    REQUIRE(doc.growCanvas(32, 16, 0, 0) == Document::OpResult::Applied);
    REQUIRE(doc.addLayer("ink") == Document::OpResult::Applied);

    const fs::path file = dir.path / "b.tboard";
    REQUIRE(saveBoardProject(file, doc, true).empty());
    CHECK(sniffProjectKind(file) == kProjectBoard);

    BoardProjectLoad loaded = loadBoardProject(file);
    REQUIRE(loaded.store.has_value());
    CHECK(loaded.dark);
    CHECK(loaded.damagedCels == 0);

    Document back;
    REQUIRE(back.loadStore(std::move(*loaded.store)));
    CHECK(back.image().width() == 96);
    CHECK(back.image().height() == 64);
    CHECK(back.originX() == -32);
    CHECK(back.originY() == -16);
    CHECK(back.layerCount() == 2);
    CHECK(back.cel(0, 0).get(5 + 32, 6 + 16) == kRed);
}

TEST_CASE("A whiteboard file is not a Pixel Art project and the reverse") {
    TempDir dir;
    Document doc(8, 8, Rgba8{});
    const fs::path board = dir.path / "b.tboard";
    const fs::path pixel = dir.path / "p.tpix";
    REQUIRE(saveBoardProject(board, doc, false).empty());
    REQUIRE(savePixelProject(pixel, doc).empty());

    CHECK_FALSE(loadPixelProject(board).store.has_value());
    CHECK_FALSE(loadBoardProject(pixel).store.has_value());
    BoardProjectLoad light = loadBoardProject(board);
    REQUIRE(light.store.has_value());
    CHECK_FALSE(light.dark);
}

TEST_CASE("A whiteboard whose size is beyond the limits is refused") {
    ProjectData data;
    data.kind = kProjectBoard;
    data.meta.set("doc", "width", "16384");
    data.meta.set("doc", "height", "16384");
    data.meta.set("doc", "layers", "4"); // 1 Gi pixels: far over the memory budget
    BoardProjectLoad loaded = boardProjectToCels(std::move(data), "evil.tboard");
    CHECK_FALSE(loaded.store.has_value());
    CHECK_FALSE(loaded.error.empty());

    ProjectData wide;
    wide.kind = kProjectBoard;
    wide.meta.set("doc", "width", "20000");
    wide.meta.set("doc", "height", "10");
    CHECK_FALSE(boardProjectToCels(std::move(wide), "wide.tboard").store.has_value());
}
