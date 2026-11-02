#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <set>

#include "core/document.hpp"
#include "core/timeline_edit.hpp"
#include "tools/toolbox.hpp"

using namespace trace;

TEST_CASE("frameDropIndex gives the index moveFrame takes") {
    // Four frames 0 1 2 3. Dropping frame 0 on the left half of 2 puts it between 1 and 2: index 1.
    CHECK(frameDropIndex(0, 2, false) == 1);
    CHECK(frameDropIndex(0, 2, true) == 2);
    // Dropping frame 3 on the left half of 1 puts it before 1: index 1; on the right half after 1: index 2.
    CHECK(frameDropIndex(3, 1, false) == 1);
    CHECK(frameDropIndex(3, 1, true) == 2);
    // Next to itself it stays where it is.
    CHECK(frameDropIndex(2, 2, false) == 2);
    CHECK(frameDropIndex(2, 2, true) == 2);
    CHECK(frameDropIndex(3, 3, true) == 3); // the last cell's right half is not one past the end
    CHECK(frameDropIndex(2, 3, false) == 2);
    CHECK(frameDropIndex(2, 1, true) == 2);
}

TEST_CASE("frameDropIndex agrees with Document::moveFrame") {
    for (int from = 0; from < 4; ++from) {
        for (int target = 0; target < 4; ++target) {
            for (const bool right : {false, true}) {
                Document doc(4, 4);
                for (int i = 0; i < 3; ++i)
                    REQUIRE(doc.addFrame() == Document::OpResult::Applied);
                // 110, 120, ...: distinct per frame and none equal to the default of 100, which would change nothing.
                for (int i = 0; i < 4; ++i)
                    REQUIRE(doc.setFrameDuration(i, 110 + i * 10) == Document::OpResult::Applied);
                const int to = frameDropIndex(from, target, right);
                REQUIRE(to >= 0);
                REQUIRE(to < 4);
                if (from == to)
                    continue;
                REQUIRE(doc.moveFrame(from, to) == Document::OpResult::Applied);
                // The dragged frame now sits at `to`, and its neighbour on the dropped side is the target.
                CHECK(doc.frame(to).durationMs == 110 + from * 10);
                const int neighbour = right ? to - 1 : to + 1;
                if (target != from)
                    CHECK(doc.frame(neighbour).durationMs == 110 + target * 10);
            }
        }
    }
}

TEST_CASE("steppedFrame wraps or stops at the ends") {
    CHECK(steppedFrame(1, 5, 1, true) == 2);
    CHECK(steppedFrame(4, 5, 1, true) == 0);
    CHECK(steppedFrame(0, 5, -1, true) == 4);
    CHECK(steppedFrame(4, 5, 1, false) == 4);
    CHECK(steppedFrame(0, 5, -1, false) == 0);
    CHECK(steppedFrame(2, 5, -7, true) == 0);
    CHECK(steppedFrame(2, 5, 100, false) == 4);
    CHECK(steppedFrame(0, 1, 1, true) == 0);
    // Bad input.
    CHECK(steppedFrame(3, 0, 1, true) == 0);
    CHECK(steppedFrame(-4, 3, 0, false) == 0);
    CHECK(steppedFrame(99, 3, 0, false) == 2);
}

TEST_CASE("visibleCells covers the view with a cell of margin") {
    // 10 cells of 100 px in a 250 px view, not scrolled: cells 0..2 are shown, 3 is the margin.
    const CellRange start = visibleCells(0.0f, 250.0f, 100.0f, 10);
    CHECK(start.first == 0);
    CHECK(start.end == 4);
    // Scrolled by 450: cells 4..6 are shown (450 to 700), 3 and 7 are the margin, so the range ends at 8.
    const CellRange middle = visibleCells(450.0f, 250.0f, 100.0f, 10);
    CHECK(middle.first == 3);
    CHECK(middle.end == 8);
    // Scrolled to the end.
    const CellRange end = visibleCells(750.0f, 250.0f, 100.0f, 10);
    CHECK(end.end == 10);
    CHECK(end.first < end.end);
    // Fewer cells than the view holds.
    const CellRange few = visibleCells(0.0f, 1000.0f, 100.0f, 3);
    CHECK(few.first == 0);
    CHECK(few.end == 3);
}

TEST_CASE("visibleCells survives bad input") {
    CHECK(visibleCells(0.0f, 100.0f, 10.0f, 0).end == 0);
    CHECK(visibleCells(0.0f, 100.0f, 0.0f, 5).end == 0);
    CHECK(visibleCells(0.0f, 0.0f, 10.0f, 5).end == 0);
    CHECK(visibleCells(std::numeric_limits<float>::quiet_NaN(), 100.0f, 10.0f, 5).end == 0);
    CHECK(visibleCells(0.0f, std::numeric_limits<float>::infinity(), 10.0f, 5).end == 0);
    const CellRange negative = visibleCells(-50.0f, 100.0f, 10.0f, 5);
    CHECK(negative.first == 0);
    // A scroll far past the end still gives a valid (possibly empty) range.
    const CellRange far = visibleCells(1.0e9f, 100.0f, 10.0f, 5);
    CHECK(far.first <= far.end);
    CHECK(far.end <= 5);
}

TEST_CASE("fpsNumber is compact") {
    CHECK(fpsNumber(100) == "10");
    CHECK(fpsNumber(80) == "12.5");
    CHECK(fpsNumber(1000) == "1");
    CHECK(fpsNumber(40) == "25");
    CHECK(fpsNumber(1) == "1000");
    CHECK(fpsNumber(3) == "333");
    CHECK(fpsNumber(60000) == "0.02");
    CHECK(fpsNumber(30) == "33.3");
    // Out-of-range durations are clamped.
    CHECK(fpsNumber(0) == "1000");
    CHECK(fpsNumber(-5) == "1000");
    CHECK(fpsNumber(1000000) == "0.02");
}

TEST_CASE("mixHash tells inputs apart") {
    std::set<std::uint64_t> seen;
    for (std::uint64_t v = 0; v < 200; ++v)
        seen.insert(mixHash(kHashSeed, v));
    CHECK(seen.size() == 200);
    CHECK(mixHash(mixHash(kHashSeed, 1), 2) != mixHash(mixHash(kHashSeed, 2), 1));
}

TEST_CASE("clampedFrameMs limits a long gap") {
    CHECK(clampedFrameMs(0.016) == doctest::Approx(16.0));
    CHECK(clampedFrameMs(5.0) == 250.0);
    CHECK(clampedFrameMs(-1.0) == 0.0);
    CHECK(clampedFrameMs(std::numeric_limits<double>::quiet_NaN()) == 0.0);
    CHECK(clampedFrameMs(std::numeric_limits<double>::infinity()) == 0.0);
}

TEST_CASE("playbackStartFrame restarts a finished animation only without a loop") {
    CHECK(playbackStartFrame(2, 5, true) == 2);
    CHECK(playbackStartFrame(4, 5, true) == 4);
    CHECK(playbackStartFrame(4, 5, false) == 0);
    CHECK(playbackStartFrame(2, 5, false) == 2);
    CHECK(playbackStartFrame(0, 1, false) == 0);
    CHECK(playbackStartFrame(9, 0, true) == 0);
    CHECK(playbackStartFrame(9, 3, true) == 2);
}

TEST_CASE("Playback advances on the frame durations") {
    const std::array<int, 3> durations{100, 100, 100};
    Playback playback;
    CHECK_FALSE(playback.playing());
    CHECK(playback.tick(1000.0, durations, true) == 0); // not playing: nothing moves

    playback.start(0);
    CHECK(playback.playing());
    CHECK(playback.tick(50.0, durations, true) == 0);
    CHECK(playback.tick(60.0, durations, true) == 1);
    CHECK(playback.tick(100.0, durations, true) == 2);
    CHECK(playback.tick(100.0, durations, true) == 0); // wraps while looping
    CHECK(playback.playing());
    playback.stop();
    CHECK_FALSE(playback.playing());
}

TEST_CASE("Playback without a loop stops on the last frame") {
    const std::array<int, 3> durations{100, 100, 100};
    Playback playback;
    playback.start(1);
    CHECK(playback.tick(100.0, durations, false) == 2);
    CHECK(playback.playing());
    CHECK(playback.tick(100.0, durations, false) == 2);
    CHECK_FALSE(playback.playing());
    CHECK(playback.frame() == 2);
}

TEST_CASE("Playback with no durations or a negative step is harmless") {
    Playback playback;
    playback.start(0);
    CHECK(playback.tick(-5.0, std::array<int, 2>{100, 100}, true) == 0);
    CHECK(playback.playing());
    playback.tick(10.0, std::span<const int>{}, true);
    CHECK(playback.frame() == 0);
}

TEST_CASE("Toolbox refuses editing presses while editing is blocked") {
    Document doc(8, 8, Rgba8{});
    Toolbox tools;
    tools.select(ToolId::Pencil);
    PointerEvent ev;
    ev.pos = {2.5f, 2.5f};

    tools.setEditingBlocked(true);
    tools.press(doc, ev);
    CHECK_FALSE(tools.gestureActive());
    CHECK(tools.lastRefusal() == Toolbox::Refusal::Playing);
    CHECK(tools.refusalCount() == 1);
    CHECK(doc.image().get(2, 2).a == 0);
    CHECK_FALSE(doc.canUndo());

    // Reading pixels is not editing. (It takes the transparent pixel under it as the primary color.)
    tools.select(ToolId::Picker);
    tools.press(doc, ev);
    CHECK(tools.lastRefusal() == Toolbox::Refusal::None);
    tools.release(doc, ev);

    tools.setEditingBlocked(false);
    tools.settings().primary = Rgba8{10, 20, 30, 255}; // the pick left it transparent
    tools.select(ToolId::Pencil);
    tools.press(doc, ev);
    CHECK(tools.gestureActive());
    tools.release(doc, ev);
    CHECK(doc.image().get(2, 2).a != 0);
}
