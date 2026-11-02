#include <doctest/doctest.h>

#include "core/panel_layout.hpp"

using namespace trace;

TEST_CASE("toolbarLayout: everything in one row when it fits") {
    const ToolbarLayout l = toolbarLayout(200.0f, 4, 30.0f, 4.0f);
    CHECK(l.columns == 4);
    CHECK(l.rows == 1);
    CHECK(l.buttonWidth == 47.0f); // (200 - 3 * 4) / 4
}

TEST_CASE("toolbarLayout: wraps in a narrow panel") {
    const ToolbarLayout l = toolbarLayout(70.0f, 4, 30.0f, 4.0f);
    CHECK(l.columns == 2);
    CHECK(l.rows == 2);
    CHECK(l.buttonWidth == 33.0f);
}

TEST_CASE("toolbarLayout: edge cases") {
    CHECK(toolbarLayout(100.0f, 0, 30.0f, 4.0f).columns == 0);
    CHECK(toolbarLayout(100.0f, -3, 30.0f, 4.0f).rows == 0);
    const ToolbarLayout tiny = toolbarLayout(5.0f, 3, 30.0f, 4.0f); // narrower than one button
    CHECK(tiny.columns == 1);
    CHECK(tiny.rows == 3);
    CHECK(tiny.buttonWidth >= 1.0f);
    const ToolbarLayout none = toolbarLayout(-20.0f, 2, 0.0f, -1.0f); // nonsense input stays sane
    CHECK(none.columns >= 1);
    CHECK(none.buttonWidth >= 1.0f);
    CHECK(toolbarLayout(1000.0f, 1, 30.0f, 4.0f).buttonWidth == 1000.0f);
}
