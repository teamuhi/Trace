#include <doctest/doctest.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "core/animation.hpp"

using namespace trace;

namespace {

constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kGreen{0, 255, 0, 255};
constexpr Rgba8 kBlue{0, 0, 255, 255};

std::vector<int> frameIndices(const std::vector<OnionFrame>& frames) {
    std::vector<int> out;
    for (const OnionFrame& f : frames)
        out.push_back(f.frame);
    return out;
}

SheetOptions sheetOptions(SheetMode mode, int columns = 4, int padding = 0, int scale = 1) {
    SheetOptions options;
    options.mode = mode;
    options.columns = columns;
    options.padding = padding;
    options.scale = scale;
    return options;
}

bool play(PlaybackState& state, double dt, const std::vector<int>& durations, bool loop) {
    bool finished = true; // the function must reset it
    state = advancePlayback(state, dt, durations, loop, finished);
    return finished;
}

} // namespace

// --- onion skin ------------------------------------------------------------------------------------

TEST_CASE("onionFrames lists the nearest frames first, previous before next") {
    const std::vector<OnionFrame> frames = onionFrames(5, 10, 2, 2, false);
    REQUIRE(frames.size() == 4);
    CHECK(frames[0].frame == 4);
    CHECK(frames[0].distance == 1);
    CHECK(frames[0].previous);
    CHECK(frames[1].frame == 6);
    CHECK(frames[1].distance == 1);
    CHECK_FALSE(frames[1].previous);
    CHECK(frames[2].frame == 3);
    CHECK(frames[2].distance == 2);
    CHECK(frames[2].previous);
    CHECK(frames[3].frame == 7);
    CHECK(frames[3].distance == 2);
    CHECK_FALSE(frames[3].previous);

    CHECK(frameIndices(onionFrames(5, 10, 1, 0, false)) == std::vector<int>{4});
    CHECK(frameIndices(onionFrames(5, 10, 0, 2, false)) == std::vector<int>{6, 7});
    CHECK(onionFrames(5, 10, 0, 0, false).empty());
    CHECK(frameIndices(onionFrames(5, 10, 3, 1, false)) == std::vector<int>{4, 6, 3, 2});
}

TEST_CASE("onionFrames without loop has nothing before the first frame or after the last") {
    CHECK(frameIndices(onionFrames(0, 10, 2, 2, false)) == std::vector<int>{1, 2});
    CHECK(frameIndices(onionFrames(9, 10, 2, 2, false)) == std::vector<int>{8, 7});
    CHECK(frameIndices(onionFrames(1, 10, 3, 1, false)) == std::vector<int>{0, 2});
    CHECK(onionFrames(0, 1, 3, 3, false).empty()); // a lone frame has no neighbours
    CHECK(onionFrames(0, 1, 3, 3, true).empty());  // and does not ghost itself when looping
}

TEST_CASE("onionFrames with loop wraps around the ends") {
    const std::vector<OnionFrame> atStart = onionFrames(0, 10, 2, 1, true);
    REQUIRE(atStart.size() == 3);
    CHECK(atStart[0].frame == 9);
    CHECK(atStart[0].previous);
    CHECK(atStart[0].distance == 1);
    CHECK(atStart[1].frame == 1);
    CHECK_FALSE(atStart[1].previous);
    CHECK(atStart[2].frame == 8);
    CHECK(atStart[2].distance == 2);
    CHECK(atStart[2].previous);

    CHECK(frameIndices(onionFrames(9, 10, 1, 2, true)) == std::vector<int>{8, 0, 1});
}

TEST_CASE("onionFrames never repeats a frame or ghosts the active one in a short animation") {
    // Two frames: the other one is both the previous and the next; it appears once, as the previous.
    const std::vector<OnionFrame> two = onionFrames(0, 2, 3, 3, true);
    REQUIRE(two.size() == 1);
    CHECK(two[0].frame == 1);
    CHECK(two[0].distance == 1);
    CHECK(two[0].previous);

    // Three frames: both neighbours once, then the wrapped-around repeats are dropped.
    CHECK(frameIndices(onionFrames(0, 3, 4, 4, true)) == std::vector<int>{2, 1});
    CHECK(frameIndices(onionFrames(1, 3, 8, 8, true)) == std::vector<int>{0, 2});

    // Big counts on a long animation never list a frame twice.
    const std::vector<OnionFrame> many = onionFrames(2, 6, 8, 8, true);
    std::vector<int> seen = frameIndices(many);
    CHECK(seen.size() == 5);
    std::sort(seen.begin(), seen.end());
    CHECK(seen == std::vector<int>{0, 1, 3, 4, 5});
}

TEST_CASE("onionFrames caps the counts at 8 and refuses bad input") {
    const std::vector<OnionFrame> frames = onionFrames(50, 100, 1000, 1000, false);
    CHECK(frames.size() == 16);
    int farthest = 0;
    for (const OnionFrame& f : frames)
        farthest = std::max(farthest, f.distance);
    CHECK(farthest == 8);
    CHECK(onionFrames(50, 100, INT_MAX, 0, false).size() == 8);
    CHECK(onionFrames(50, 100, 0, INT_MAX, true).size() == 8);

    CHECK(onionFrames(0, 0, 2, 2, true).empty());
    CHECK(onionFrames(0, -3, 2, 2, true).empty());
    CHECK(onionFrames(-1, 5, 2, 2, true).empty());
    CHECK(onionFrames(5, 5, 2, 2, true).empty());
    CHECK(onionFrames(INT_MAX, 5, 2, 2, true).empty());
    CHECK(onionFrames(INT_MIN, 5, 2, 2, true).empty());
    CHECK(onionFrames(2, 5, -1, 2, true).empty());
    CHECK(onionFrames(2, 5, 2, -1, true).empty());
    CHECK(onionFrames(2, INT_MAX, 1, 1, true).size() == 2); // a huge count does not allocate per frame
}

// --- playback --------------------------------------------------------------------------------------

TEST_CASE("advancePlayback stays on a frame until its duration has passed") {
    const std::vector<int> durations = {100, 50, 200};
    PlaybackState state;
    CHECK_FALSE(play(state, 30, durations, true));
    CHECK(state.frame == 0);
    CHECK(state.elapsedMs == doctest::Approx(30));
    CHECK_FALSE(play(state, 69, durations, true));
    CHECK(state.frame == 0);
    CHECK(state.elapsedMs == doctest::Approx(99));
    CHECK_FALSE(play(state, 1, durations, true));
    CHECK(state.frame == 1);
    CHECK(state.elapsedMs == doctest::Approx(0));
    CHECK_FALSE(play(state, 0, durations, true)); // no time, nothing moves
    CHECK(state.frame == 1);
}

TEST_CASE("advancePlayback skips several frames in one step") {
    const std::vector<int> durations = {100, 50, 200};
    PlaybackState state;
    CHECK_FALSE(play(state, 120, durations, false));
    CHECK(state.frame == 1);
    CHECK(state.elapsedMs == doctest::Approx(20));
    CHECK_FALSE(play(state, 140, durations, false)); // 20 + 140 = 160: frame 1 ends at 50, leaving 110 into frame 2
    CHECK(state.frame == 2);
    CHECK(state.elapsedMs == doctest::Approx(110));
}

TEST_CASE("advancePlayback loops without losing time or repeating frames") {
    const std::vector<int> durations = {100, 50, 200};
    PlaybackState state{2, 150};
    CHECK_FALSE(play(state, 100, durations, true)); // 50 left of the last frame, then 50 into the first
    CHECK(state.frame == 0);
    CHECK(state.elapsedMs == doctest::Approx(50));

    state = PlaybackState{};
    CHECK_FALSE(play(state, 500, durations, true)); // one cycle is 350; 150 in lands exactly on the third frame
    CHECK(state.frame == 2);
    CHECK(state.elapsedMs == doctest::Approx(0));

    state = PlaybackState{};
    CHECK_FALSE(play(state, 3.0 * 350 + 120, durations, true));
    CHECK(state.frame == 1);
    CHECK(state.elapsedMs == doctest::Approx(20));

    // One frame loops onto itself.
    state = PlaybackState{};
    CHECK_FALSE(play(state, 250, std::vector<int>{100}, true));
    CHECK(state.frame == 0);
    CHECK(state.elapsedMs == doctest::Approx(50));
}

TEST_CASE("advancePlayback without loop stops on the last frame and says so") {
    const std::vector<int> durations = {100, 50, 200};
    PlaybackState state{2, 0};
    CHECK_FALSE(play(state, 199, durations, false));
    CHECK(state.frame == 2);
    CHECK(state.elapsedMs == doctest::Approx(199));
    CHECK(play(state, 1, durations, false));
    CHECK(state.frame == 2);
    CHECK(state.elapsedMs == doctest::Approx(200));
    CHECK(play(state, 5000, durations, false)); // stays put and keeps saying so
    CHECK(state.frame == 2);
    CHECK(state.elapsedMs == doctest::Approx(200));

    state = PlaybackState{};
    CHECK(play(state, 1e9, durations, false)); // a huge step skips straight to the end
    CHECK(state.frame == 2);
    CHECK(state.elapsedMs == doctest::Approx(200));

    state = PlaybackState{};
    CHECK(play(state, 1e300, durations, false));
    CHECK(state.frame == 2);

    state = PlaybackState{};
    CHECK(play(state, 5, std::vector<int>{10}, false) == false); // a single frame is not over after 5 of 10 ms
    CHECK(play(state, 5, std::vector<int>{10}, false));
}

TEST_CASE("advancePlayback survives huge and odd steps while looping") {
    const std::vector<int> durations = {100, 50, 200};
    for (const double dt : {1e12, 1e15, 1e100, 1e300, std::numeric_limits<double>::max()}) {
        PlaybackState state;
        bool finished = true;
        state = advancePlayback(state, dt, durations, true, finished);
        CHECK_FALSE(finished);
        CHECK(state.frame >= 0);
        CHECK(state.frame < 3);
        CHECK(state.elapsedMs >= 0.0);
        CHECK(state.elapsedMs <= durations[static_cast<std::size_t>(state.frame)]);
    }
}

TEST_CASE("advancePlayback ignores negative and non-finite steps and cleans a bad state") {
    const std::vector<int> durations = {100, 50, 200};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const double dt : {-1.0, -1e9, nan, inf, -inf}) {
        PlaybackState state{1, 20};
        CHECK_FALSE(play(state, dt, durations, true));
        CHECK(state.frame == 1);
        CHECK(state.elapsedMs == doctest::Approx(20));
        CHECK_FALSE(play(state, dt, durations, false));
        CHECK(state.frame == 1);
    }

    PlaybackState state{99, 0}; // an out-of-range frame is clamped first
    CHECK_FALSE(play(state, 10, durations, true));
    CHECK(state.frame == 2);
    CHECK(state.elapsedMs == doctest::Approx(10));
    state = PlaybackState{-4, 0};
    CHECK_FALSE(play(state, 10, durations, true));
    CHECK(state.frame == 0);
    state = PlaybackState{INT_MAX, 0};
    CHECK_FALSE(play(state, nan, durations, true));
    CHECK(state.frame == 2);

    state = PlaybackState{0, nan}; // a broken elapsed time restarts the frame
    CHECK_FALSE(play(state, 10, durations, true));
    CHECK(state.elapsedMs == doctest::Approx(10));
    state = PlaybackState{0, -50};
    CHECK_FALSE(play(state, 10, durations, true));
    CHECK(state.elapsedMs == doctest::Approx(10));
}

TEST_CASE("advancePlayback with nothing to play or odd durations") {
    PlaybackState state{3, 40};
    CHECK_FALSE(play(state, 100, std::vector<int>{}, true));
    CHECK(state.frame == 0);
    CHECK(state.elapsedMs == 0.0);
    CHECK_FALSE(play(state, 100, std::vector<int>{}, false));

    // Durations are clamped to 1..60000 ms.
    state = PlaybackState{};
    CHECK_FALSE(play(state, 1, std::vector<int>{0, -7, 100}, true)); // 0 counts as 1 ms
    CHECK(state.frame == 1);
    CHECK_FALSE(play(state, 1, std::vector<int>{0, -7, 100}, true));
    CHECK(state.frame == 2);
    state = PlaybackState{};
    CHECK(play(state, 70000, std::vector<int>{1000000, 5}, false)); // the first frame counts as 60000 ms
    CHECK(state.frame == 1);
    CHECK(state.elapsedMs == doctest::Approx(5)); // 10000 ms into a 5 ms last frame: over

    // Zero-length frames in a loop cannot spin forever.
    state = PlaybackState{};
    CHECK_FALSE(play(state, 1e6, std::vector<int>{0, 0, 0}, true));
    CHECK(state.frame >= 0);
    CHECK(state.frame < 3);
}

TEST_CASE("advancePlayback in small ticks matches one big step") {
    const std::vector<int> durations = {80, 120, 40, 300};
    PlaybackState ticks;
    for (int i = 0; i < 100; ++i)
        play(ticks, 16.0, durations, true);
    PlaybackState big;
    play(big, 1600.0, durations, true);
    CHECK(ticks.frame == big.frame);
    CHECK(ticks.elapsedMs == doctest::Approx(big.elapsedMs).epsilon(1e-6));
}

// --- sprite sheet layout ---------------------------------------------------------------------------

TEST_CASE("sheetLayout lays frames out in a row") {
    const SheetLayout layout = sheetLayout(3, 16, 8, sheetOptions(SheetMode::Row));
    REQUIRE(layout.ok);
    CHECK(layout.error.empty());
    CHECK(layout.width == 48);
    CHECK(layout.height == 8);
    CHECK(layout.cellWidth == 16);
    CHECK(layout.cellHeight == 8);
    REQUIRE(layout.cells.size() == 3);
    CHECK(layout.cells[0] == IRect{0, 0, 16, 8});
    CHECK(layout.cells[1] == IRect{16, 0, 32, 8});
    CHECK(layout.cells[2] == IRect{32, 0, 48, 8});
}

TEST_CASE("sheetLayout puts padding around the edge and between cells, and scales") {
    const SheetLayout row = sheetLayout(3, 4, 3, sheetOptions(SheetMode::Row, 4, 2, 2));
    REQUIRE(row.ok);
    CHECK(row.cellWidth == 8);
    CHECK(row.cellHeight == 6);
    CHECK(row.width == 2 + 3 * (8 + 2));
    CHECK(row.height == 2 + 6 + 2);
    REQUIRE(row.cells.size() == 3);
    CHECK(row.cells[0] == IRect{2, 2, 10, 8});
    CHECK(row.cells[1] == IRect{12, 2, 20, 8});
    CHECK(row.cells[2] == IRect{22, 2, 30, 8});

    const SheetLayout column = sheetLayout(3, 4, 5, sheetOptions(SheetMode::Column, 99, 1));
    REQUIRE(column.ok);
    CHECK(column.width == 1 + 4 + 1);
    CHECK(column.height == 1 + 3 * (5 + 1));
    REQUIRE(column.cells.size() == 3);
    CHECK(column.cells[0] == IRect{1, 1, 5, 6});
    CHECK(column.cells[1] == IRect{1, 7, 5, 12});
    CHECK(column.cells[2] == IRect{1, 13, 5, 18});
}

TEST_CASE("sheetLayout grid wraps after the given number of columns") {
    const SheetLayout grid = sheetLayout(7, 2, 2, sheetOptions(SheetMode::Grid, 3, 1));
    REQUIRE(grid.ok);
    CHECK(grid.width == 1 + 3 * 3);
    CHECK(grid.height == 1 + 3 * 3); // 7 frames in rows of 3 make 3 rows
    REQUIRE(grid.cells.size() == 7);
    CHECK(grid.cells[0] == IRect{1, 1, 3, 3});
    CHECK(grid.cells[2] == IRect{7, 1, 9, 3});
    CHECK(grid.cells[3] == IRect{1, 4, 3, 6});
    CHECK(grid.cells[6] == IRect{1, 7, 3, 9});

    // Exact multiples need no spare row.
    const SheetLayout even = sheetLayout(6, 2, 2, sheetOptions(SheetMode::Grid, 3));
    REQUIRE(even.ok);
    CHECK(even.width == 6);
    CHECK(even.height == 4);
}

TEST_CASE("sheetLayout clamps columns and padding") {
    // Columns below 1 act as 1, above the frame count as the frame count.
    const SheetLayout one = sheetLayout(4, 3, 3, sheetOptions(SheetMode::Grid, 0));
    REQUIRE(one.ok);
    CHECK(one.width == 3);
    CHECK(one.height == 12);
    const SheetLayout negative = sheetLayout(4, 3, 3, sheetOptions(SheetMode::Grid, INT_MIN));
    REQUIRE(negative.ok);
    CHECK(negative.width == 3);
    const SheetLayout all = sheetLayout(4, 3, 3, sheetOptions(SheetMode::Grid, 1000));
    REQUIRE(all.ok);
    CHECK(all.width == 12);
    CHECK(all.height == 3);
    const SheetLayout huge = sheetLayout(4, 3, 3, sheetOptions(SheetMode::Grid, INT_MAX));
    REQUIRE(huge.ok);
    CHECK(huge.width == 12);

    // Padding is limited to 0..64.
    const SheetLayout padded = sheetLayout(2, 4, 4, sheetOptions(SheetMode::Row, 4, 1000));
    const SheetLayout limit = sheetLayout(2, 4, 4, sheetOptions(SheetMode::Row, 4, 64));
    REQUIRE(padded.ok);
    CHECK(padded.width == limit.width);
    CHECK(padded.width == 64 + 2 * (4 + 64));
    const SheetLayout none = sheetLayout(2, 4, 4, sheetOptions(SheetMode::Row, 4, -5));
    REQUIRE(none.ok);
    CHECK(none.width == 8);
    CHECK(none.cells[0] == IRect{0, 0, 4, 4});
}

TEST_CASE("sheetLayout refuses what cannot be a sheet") {
    const SheetOptions row = sheetOptions(SheetMode::Row);
    for (const SheetLayout& layout :
         {sheetLayout(0, 8, 8, row), sheetLayout(-1, 8, 8, row), sheetLayout(1, 0, 8, row), sheetLayout(1, 8, 0, row),
          sheetLayout(1, -4, 8, row), sheetLayout(1, 8, 8, sheetOptions(SheetMode::Row, 4, 0, 0)),
          sheetLayout(1, 8, 8, sheetOptions(SheetMode::Row, 4, 0, 33)),
          sheetLayout(1, 8, 8, sheetOptions(SheetMode::Row, 4, 0, -2)),
          sheetLayout(1, 8, 8, sheetOptions(static_cast<SheetMode>(77)))}) {
        CHECK_FALSE(layout.ok);
        CHECK_FALSE(layout.error.empty());
        CHECK(layout.cells.empty());
        CHECK(layout.width == 0);
        CHECK(layout.height == 0);
    }
}

TEST_CASE("sheetLayout refuses sheets past 16384 pixels on a side") {
    // Exactly at the limit is fine.
    const SheetLayout exact = sheetLayout(1, kMaxExportSide, 1, sheetOptions(SheetMode::Row));
    REQUIRE(exact.ok);
    CHECK(exact.width == kMaxExportSide);
    CHECK(sheetLayout(2, 8192, 8, sheetOptions(SheetMode::Row)).ok);
    CHECK(sheetLayout(4, 4096, 4096, sheetOptions(SheetMode::Grid, 4)).ok);

    // One pixel more is not, and the message names the limit and the size.
    const SheetLayout over = sheetLayout(1, kMaxExportSide + 1, 1, sheetOptions(SheetMode::Row));
    CHECK_FALSE(over.ok);
    CHECK(over.error.find("16384") != std::string::npos);
    CHECK(over.error.find("16385") != std::string::npos);
    CHECK(over.cells.empty());

    // Padding counts: two cells of 8192 plus any padding no longer fit.
    CHECK_FALSE(sheetLayout(2, 8192, 8, sheetOptions(SheetMode::Row, 4, 1)).ok);
    // Scale counts: 600 * 32 = 19200.
    CHECK_FALSE(sheetLayout(1, 600, 1, sheetOptions(SheetMode::Row, 4, 0, 32)).ok);
    CHECK(sheetLayout(1, 512, 512, sheetOptions(SheetMode::Row, 4, 0, 32)).ok);
    // A column of tall frames.
    CHECK_FALSE(sheetLayout(17, 4, 1024, sheetOptions(SheetMode::Column)).ok);
    CHECK(sheetLayout(16, 4, 1024, sheetOptions(SheetMode::Column)).ok);
    // The grid is refused on its height too.
    CHECK_FALSE(sheetLayout(17, 4, 1024, sheetOptions(SheetMode::Grid, 1)).ok);
}

TEST_CASE("sheetLayout does the size math in 64 bits") {
    const SheetOptions row = sheetOptions(SheetMode::Row);
    const SheetOptions column = sheetOptions(SheetMode::Column);
    const SheetOptions grid = sheetOptions(SheetMode::Grid, INT_MAX, 64, 32);

    CHECK_FALSE(sheetLayout(INT_MAX, 1, 1, row).ok);
    CHECK_FALSE(sheetLayout(INT_MAX, 1, 1, column).ok);
    CHECK_FALSE(sheetLayout(INT_MAX, 1, 1, sheetOptions(SheetMode::Grid, 1000)).ok);
    CHECK_FALSE(sheetLayout(INT_MAX, INT_MAX, INT_MAX, grid).ok);
    CHECK_FALSE(sheetLayout(1, INT_MAX, INT_MAX, sheetOptions(SheetMode::Row, 4, 64, 32)).ok);
    CHECK_FALSE(sheetLayout(65536, 65536, 1, row).ok); // 2^32 would wrap an int to 0
    CHECK_FALSE(sheetLayout(65536, 1, 65536, column).ok);
    CHECK_FALSE(sheetLayout(65537, 65536, 1, row).ok);
    CHECK_FALSE(sheetLayout(131072, 32768, 1, sheetOptions(SheetMode::Row, 4, 0, 32)).ok);
    CHECK_FALSE(sheetLayout(1 << 21, 1, 1, sheetOptions(SheetMode::Grid, 1 << 12)).ok); // too many frames for any sheet
    CHECK_FALSE(sheetLayout(INT_MAX, 1, 1, sheetOptions(SheetMode::Grid, 16384)).ok);
    const SheetLayout big = sheetLayout(1, INT_MAX, INT_MAX, sheetOptions(SheetMode::Row, 4, 64, 32));
    CHECK_FALSE(big.ok);
    CHECK(big.error.find("16384") != std::string::npos);
}

// --- building the sheet picture --------------------------------------------------------------------

TEST_CASE("buildSpriteSheet places every scaled frame in its cell on a transparent background") {
    Image a(2, 2, kRed);
    a.set(1, 0, kBlue);
    const std::vector<Image> frames = {a, Image(2, 2, kGreen)};
    const SheetLayout layout = sheetLayout(2, 2, 2, sheetOptions(SheetMode::Row, 4, 1, 2));
    REQUIRE(layout.ok);
    REQUIRE(layout.width == 11);
    REQUIRE(layout.height == 6);

    const Image sheet = buildSpriteSheet(frames, layout, 2);
    REQUIRE(sheet.width() == 11);
    REQUIRE(sheet.height() == 6);
    // Frame 0 at x 1..4, y 1..4: red with its top right source pixel blue, each pixel 2 x 2.
    CHECK(sheet.get(1, 1) == kRed);
    CHECK(sheet.get(2, 2) == kRed);
    CHECK(sheet.get(3, 1) == kBlue);
    CHECK(sheet.get(4, 2) == kBlue);
    CHECK(sheet.get(1, 3) == kRed);
    CHECK(sheet.get(4, 4) == kRed);
    // Frame 1 at x 6..9.
    CHECK(sheet.get(6, 1) == kGreen);
    CHECK(sheet.get(9, 4) == kGreen);
    // Padding stays transparent: the edge and the gap between cells.
    CHECK(sheet.get(0, 0) == Rgba8{});
    CHECK(sheet.get(0, 3) == Rgba8{});
    CHECK(sheet.get(5, 2) == Rgba8{});
    CHECK(sheet.get(10, 5) == Rgba8{});
    CHECK(sheet.get(3, 5) == Rgba8{});
    CHECK(sheet.get(1, 0) == Rgba8{});
}

TEST_CASE("buildSpriteSheet fills a grid row by row and leaves unused cells empty") {
    const std::vector<Image> frames = {Image(1, 1, kRed), Image(1, 1, kGreen), Image(1, 1, kBlue)};
    const SheetLayout layout = sheetLayout(3, 1, 1, sheetOptions(SheetMode::Grid, 2));
    REQUIRE(layout.ok);
    const Image sheet = buildSpriteSheet(frames, layout, 1);
    REQUIRE(sheet.width() == 2);
    REQUIRE(sheet.height() == 2);
    CHECK(sheet.get(0, 0) == kRed);
    CHECK(sheet.get(1, 0) == kGreen);
    CHECK(sheet.get(0, 1) == kBlue);
    CHECK(sheet.get(1, 1) == Rgba8{});

    // Semi-transparent pixels are copied as they are, not blended with the background.
    const Rgba8 faint{10, 20, 30, 40};
    const std::vector<Image> faintFrames = {Image(1, 1, faint)};
    const Image single = buildSpriteSheet(faintFrames, sheetLayout(1, 1, 1, sheetOptions(SheetMode::Row)), 1);
    CHECK(single.get(0, 0) == faint);

    // A column places frames top to bottom.
    const SheetLayout column = sheetLayout(3, 1, 1, sheetOptions(SheetMode::Column));
    const Image tall = buildSpriteSheet(frames, column, 1);
    REQUIRE(tall.height() == 3);
    CHECK(tall.get(0, 0) == kRed);
    CHECK(tall.get(0, 1) == kGreen);
    CHECK(tall.get(0, 2) == kBlue);
}

TEST_CASE("buildSpriteSheet returns an empty image for any mismatch") {
    const std::vector<Image> frames = {Image(2, 2, kRed), Image(2, 2, kGreen)};
    const SheetLayout layout = sheetLayout(2, 2, 2, sheetOptions(SheetMode::Row));
    REQUIRE(layout.ok);
    CHECK_FALSE(buildSpriteSheet(frames, layout, 1).empty());

    CHECK(buildSpriteSheet(frames, layout, 2).empty());  // the layout was made for scale 1
    CHECK(buildSpriteSheet(frames, layout, 0).empty());
    CHECK(buildSpriteSheet(frames, layout, 33).empty());
    CHECK(buildSpriteSheet({}, layout, 1).empty());
    CHECK(buildSpriteSheet(std::vector<Image>{Image(2, 2, kRed)}, layout, 1).empty()); // too few frames
    CHECK(buildSpriteSheet(std::vector<Image>{Image(2, 2), Image(2, 2), Image(2, 2)}, layout, 1).empty());
    CHECK(buildSpriteSheet(std::vector<Image>{Image(2, 2), Image(3, 2)}, layout, 1).empty()); // sizes differ
    CHECK(buildSpriteSheet(std::vector<Image>{Image(3, 3), Image(3, 3)}, layout, 1).empty()); // not the layout's size
    CHECK(buildSpriteSheet(std::vector<Image>{Image(), Image()}, layout, 1).empty());

    const SheetLayout refused = sheetLayout(0, 2, 2, sheetOptions(SheetMode::Row));
    CHECK(buildSpriteSheet(frames, refused, 1).empty());

    SheetLayout tampered = layout; // a hand-edited layout cannot write outside the picture
    tampered.cells[1] = IRect{3, 0, 5, 2};
    CHECK(buildSpriteSheet(frames, tampered, 1).empty());
    tampered = layout;
    tampered.cells[0] = IRect{-1, 0, 1, 2};
    CHECK(buildSpriteSheet(frames, tampered, 1).empty());
    tampered = layout;
    tampered.width = kMaxExportSide + 1;
    CHECK(buildSpriteSheet(frames, tampered, 1).empty());
}

// --- scaledNearest ---------------------------------------------------------------------------------

TEST_CASE("scaledNearest enlarges every pixel into a block") {
    Image src(2, 2, kRed);
    src.set(1, 0, kGreen);
    src.set(0, 1, kBlue);
    src.set(1, 1, Rgba8{1, 2, 3, 4});

    const Image big = scaledNearest(src, 3);
    REQUIRE(big.width() == 6);
    REQUIRE(big.height() == 6);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x)
            CHECK(big.get(x, y) == src.get(x / 3, y / 3));
    }

    const Image same = scaledNearest(src, 1);
    REQUIRE(same.width() == 2);
    CHECK(same.get(1, 0) == kGreen);
    CHECK(same.get(1, 1) == Rgba8{1, 2, 3, 4});

    const Image row = scaledNearest(Image(3, 1, kRed), 32);
    CHECK(row.width() == 96);
    CHECK(row.height() == 32);
}

TEST_CASE("scaledNearest refuses bad factors, empty sources and results past 16384") {
    const Image src(4, 4, kRed);
    CHECK(scaledNearest(src, 0).empty());
    CHECK(scaledNearest(src, -1).empty());
    CHECK(scaledNearest(src, 33).empty());
    CHECK(scaledNearest(src, INT_MAX).empty());
    CHECK(scaledNearest(src, INT_MIN).empty());
    CHECK(scaledNearest(Image(), 2).empty());
    CHECK(scaledNearest(Image(), 1).empty());

    CHECK_FALSE(scaledNearest(Image(1024, 1, kRed), 16).empty());   // 16384 exactly
    CHECK(scaledNearest(Image(1025, 1, kRed), 16).empty());
    CHECK(scaledNearest(Image(1, 1025, kRed), 16).empty());
    CHECK(scaledNearest(Image(1025, 1, kRed), 1).width() == 1025); // a copy is never refused for size
    CHECK(scaledNearest(Image(kMaxExportSide, 1, kRed), 2).empty());
}
