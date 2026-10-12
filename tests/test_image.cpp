#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/image.hpp"

using trace::Image;
using trace::Rgba8;

TEST_CASE("Image construction fills every pixel") {
    const Rgba8 red{255, 0, 0, 255};
    Image img(4, 3, red);

    CHECK(img.width() == 4);
    CHECK(img.height() == 3);
    CHECK_FALSE(img.empty());
    CHECK(img.pixels().size() == 12);
    for (const Rgba8& p : img.pixels())
        CHECK(p == red);
}

TEST_CASE("Default and negative sizes produce an empty image") {
    CHECK(Image().empty());

    const Image negative(-5, 10);
    CHECK(negative.empty());
    CHECK(negative.width() == 0);
    CHECK(negative.height() == 0);
}

TEST_CASE("get and set are bounds safe") {
    Image img(2, 2);
    const Rgba8 green{0, 255, 0, 255};

    CHECK(img.set(1, 1, green));
    CHECK(img.get(1, 1) == green);
    CHECK(img.get(0, 0) == Rgba8{});

    CHECK_FALSE(img.set(-1, 0, green));
    CHECK_FALSE(img.set(0, -1, green));
    CHECK_FALSE(img.set(2, 0, green));
    CHECK_FALSE(img.set(0, 2, green));
    CHECK(img.get(-1, 0) == Rgba8{});
    CHECK(img.get(2, 2) == Rgba8{});
}

TEST_CASE("clear overwrites all pixels") {
    Image img(3, 3, Rgba8{1, 2, 3, 4});
    img.clear(Rgba8{9, 8, 7, 6});

    for (const Rgba8& p : img.pixels())
        CHECK(p == Rgba8{9, 8, 7, 6});
}

TEST_CASE("row returns a span of one scanline") {
    Image img(5, 2);
    img.set(4, 1, Rgba8{10, 20, 30, 40});

    CHECK(img.row(1).size() == 5);
    CHECK(img.row(1)[4] == Rgba8{10, 20, 30, 40});
    CHECK(img.row(-1).empty());
    CHECK(img.row(2).empty());
}
