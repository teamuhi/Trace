#include <doctest/doctest.h>

#include <climits>

#include "core/dialog_logic.hpp"

using namespace trace;

TEST_CASE("Anchor grid cells map to anchors and back") {
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const Anchor a = anchorAt(column, row);
            CHECK(anchorColumn(a) == column);
            CHECK(anchorRow(a) == row);
            CHECK(static_cast<int>(a) == row * 3 + column);
        }
    }
    CHECK(anchorAt(0, 0) == Anchor::TopLeft);
    CHECK(anchorAt(1, 1) == Anchor::Center);
    CHECK(anchorAt(2, 2) == Anchor::BottomRight);
    CHECK(anchorAt(-4, 9) == Anchor::BottomLeft); // out of range cells clamp to the edge
    CHECK(anchorAt(7, -1) == Anchor::TopRight);
}

TEST_CASE("steppedValue moves a number field and stays inside its limits") {
    CHECK(steppedValue(10, 1, 1, 100) == 11);
    CHECK(steppedValue(10, -1, 1, 100) == 9);
    CHECK(steppedValue(1, -1, 1, 100) == 1);
    CHECK(steppedValue(100, 1, 1, 100) == 100);
    CHECK(steppedValue(50, 100, 1, 100) == 100);
    CHECK(steppedValue(50, -100, 1, 100) == 1);
    CHECK(steppedValue(INT_MAX, 100, 1, 1024) == 1024); // no overflow
    CHECK(steppedValue(INT_MIN, -100, 1, 1024) == 1);
    CHECK(steppedValue(5, 1, 3, 3) == 3);
    CHECK(steppedValue(5, 1, 9, 3) == 9); // a reversed range collapses to its lower limit
}

TEST_CASE("sizeChangeText shows the old and the new size") {
    CHECK(sizeChangeText(16, 16, 32, 24) == "16 x 16 -> 32 x 24");
    CHECK(sizeChangeText(16, 16, 16, 16) == "16 x 16");
    CHECK(sizeChangeText(16, 16, 16, 17) == "16 x 16 -> 16 x 17");
}

TEST_CASE("keptPictureRect is the part of the old picture left on the new canvas") {
    // Growing: the whole picture stays, placed by the anchor.
    KeptRect r = keptPictureRect(4, 4, 10, 8, Anchor::TopLeft);
    CHECK((r.x0 == 0 && r.y0 == 0 && r.x1 == 4 && r.y1 == 4));
    r = keptPictureRect(4, 4, 10, 8, Anchor::BottomRight);
    CHECK((r.x0 == 6 && r.y0 == 4 && r.x1 == 10 && r.y1 == 8));
    r = keptPictureRect(4, 4, 10, 8, Anchor::Center);
    CHECK((r.x0 == 3 && r.y0 == 2 && r.x1 == 7 && r.y1 == 6));

    // Shrinking: the picture is cut to the canvas.
    r = keptPictureRect(10, 8, 4, 4, Anchor::TopLeft);
    CHECK((r.x0 == 0 && r.y0 == 0 && r.x1 == 4 && r.y1 == 4));
    r = keptPictureRect(10, 8, 4, 4, Anchor::Center);
    CHECK((r.x0 == 0 && r.y0 == 0 && r.x1 == 4 && r.y1 == 4));

    // One axis grows while the other shrinks.
    r = keptPictureRect(8, 8, 12, 4, Anchor::Bottom);
    CHECK((r.x0 == 2 && r.y0 == 0 && r.x1 == 10 && r.y1 == 4));

    // Same size: the picture fills the canvas.
    r = keptPictureRect(5, 5, 5, 5, Anchor::Right);
    CHECK((r.x0 == 0 && r.y0 == 0 && r.x1 == 5 && r.y1 == 5));
    CHECK_FALSE(r.empty());
}
