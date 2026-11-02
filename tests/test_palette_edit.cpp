#include <doctest/doctest.h>

#include <string>

#include "core/palette_edit.hpp"

using namespace trace;

namespace {

// Six distinct colors whose red channel is their index.
Palette sixColors() {
    Palette p;
    for (int i = 0; i < 6; ++i)
        p.colors.push_back(Rgba8{static_cast<std::uint8_t>(i), 0, 0, 255});
    return p;
}

int redAt(const Palette& p, int i) {
    return p.colors[static_cast<std::size_t>(i)].r;
}

} // namespace

TEST_CASE("movePaletteColor puts the color at the requested index") {
    Palette p = sixColors();
    CHECK(movePaletteColor(p, 1, 4));
    CHECK(redAt(p, 4) == 1);
    CHECK(redAt(p, 1) == 2); // the ones in between shift down
    CHECK(redAt(p, 0) == 0);
    CHECK(redAt(p, 5) == 5);

    p = sixColors();
    CHECK(movePaletteColor(p, 5, 0));
    CHECK(redAt(p, 0) == 5);
    CHECK(redAt(p, 1) == 0);
    CHECK(redAt(p, 5) == 4);
}

TEST_CASE("movePaletteColor refuses indices out of range and no-op moves") {
    Palette p = sixColors();
    const Palette before = p;
    CHECK_FALSE(movePaletteColor(p, 2, 2));
    CHECK_FALSE(movePaletteColor(p, -1, 2));
    CHECK_FALSE(movePaletteColor(p, 2, 6));
    CHECK_FALSE(movePaletteColor(p, 6, 0));
    CHECK(p == before);

    Palette empty;
    CHECK_FALSE(movePaletteColor(empty, 0, 0));
}

TEST_CASE("paletteDropIndex lands the color on the side of the swatch it was dropped on") {
    const int count = 6;
    for (int from = 0; from < count; ++from) {
        for (int target = 0; target < count; ++target) {
            if (from == target)
                continue;
            for (const bool after : {false, true}) {
                CAPTURE(from);
                CAPTURE(target);
                CAPTURE(after);
                Palette p = sixColors();
                const int to = paletteDropIndex(from, target, after);
                if (to == from) { // dropped right where it already is: nothing moves
                    CHECK_FALSE(movePaletteColor(p, from, to));
                    continue;
                }
                REQUIRE(movePaletteColor(p, from, to));
                // The dragged color is now right before or right after the target color.
                const int movedAt = to;
                int targetAt = -1;
                for (int i = 0; i < count; ++i) {
                    if (redAt(p, i) == target)
                        targetAt = i;
                }
                CHECK(redAt(p, movedAt) == from);
                CHECK(movedAt == (after ? targetAt + 1 : targetAt - 1));
            }
        }
    }
}

TEST_CASE("indexAfterMove tracks a selected swatch through any move") {
    for (int from = 0; from < 6; ++from) {
        for (int to = 0; to < 6; ++to) {
            if (from == to)
                continue;
            for (int sel = 0; sel < 6; ++sel) {
                CAPTURE(from);
                CAPTURE(to);
                CAPTURE(sel);
                Palette p = sixColors();
                REQUIRE(movePaletteColor(p, from, to));
                const int now = indexAfterMove(sel, from, to);
                REQUIRE(now >= 0);
                REQUIRE(now < 6);
                CHECK(redAt(p, now) == sel); // still the same color
            }
        }
    }
    CHECK(indexAfterMove(-1, 0, 3) == -1);
}

TEST_CASE("removePaletteColor and the selection that follows it") {
    Palette p = sixColors();
    CHECK(removePaletteColor(p, 2));
    CHECK(p.colors.size() == 5);
    CHECK(redAt(p, 2) == 3);
    CHECK_FALSE(removePaletteColor(p, 5));
    CHECK_FALSE(removePaletteColor(p, -1));
    CHECK(p.colors.size() == 5);

    CHECK(indexAfterRemove(2, 2) == -1);
    CHECK(indexAfterRemove(4, 2) == 3);
    CHECK(indexAfterRemove(1, 2) == 1);
    CHECK(indexAfterRemove(-1, 2) == -1);

    Palette single;
    single.colors.push_back(Rgba8{1, 2, 3, 255});
    CHECK(removePaletteColor(single, 0));
    CHECK(single.colors.empty());
    CHECK_FALSE(removePaletteColor(single, 0));
}

TEST_CASE("clampSelection drops an index the palette no longer has") {
    CHECK(clampSelection(3, 6) == 3);
    CHECK(clampSelection(6, 6) == -1);
    CHECK(clampSelection(0, 0) == -1);
    CHECK(clampSelection(-1, 6) == -1);
}

TEST_CASE("findPaletteColor compares only red, green and blue") {
    const Palette p = sixColors();
    CHECK(findPaletteColor(p, Rgba8{3, 0, 0, 255}) == 3);
    CHECK(findPaletteColor(p, Rgba8{3, 0, 0, 0}) == 3);
    CHECK(findPaletteColor(p, Rgba8{3, 1, 0, 255}) == -1);
    CHECK(findPaletteColor(Palette{}, Rgba8{}) == -1);
}

TEST_CASE("paletteFileStem makes a safe file name") {
    CHECK(paletteFileStem("Sweetie 16") == "Sweetie 16");
    CHECK(paletteFileStem("a/b:c*d") == "a_b_c_d");
    CHECK(paletteFileStem("  ") == "palette");
    CHECK(paletteFileStem("") == "palette");
    CHECK(paletteFileStem("name. .") == "name");
    CHECK(paletteFileStem("  lead") == "lead");
    CHECK(paletteFileStem(std::string("tab\there")) == "tab_here");
}

TEST_CASE("PaletteState counts an edit as a modification until it is saved") {
    PaletteState state;
    CHECK_FALSE(state.modified());
    state.touch();
    CHECK(state.modified());
    state.markSaved();
    CHECK_FALSE(state.modified());

    state.touch();
    state.reset(Palette{});
    CHECK_FALSE(state.modified()); // New and Open start clean
    CHECK(state.palette.colors.empty());
}
