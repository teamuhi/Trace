#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include "core/sparse_image.hpp"

using namespace trace;

namespace {

constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kBlue{0, 0, 255, 255};

bool sameImage(const Image& a, const Image& b) {
    return a.width() == b.width() && a.height() == b.height() &&
           std::equal(a.pixels().begin(), a.pixels().end(), b.pixels().begin());
}

} // namespace

TEST_CASE("SparseImage of an empty image is empty") {
    const SparseImage none{Image()};
    CHECK(none.empty());
    CHECK(none.bytes() == 0);
    CHECK(none.get(0, 0) == Rgba8{});
}

TEST_CASE("SparseImage stores nothing for a blank image and reads it back exactly") {
    const Image blank(300, 200);
    const SparseImage sparse(blank);
    CHECK(sparse.width() == 300);
    CHECK(sparse.height() == 200);
    CHECK(sparse.bytes() == 0);
    CHECK(sameImage(sparse.toImage(), blank));
    CHECK(sparse.equalsRect(blank, 0, 0, 300, 200));
}

TEST_CASE("SparseImage round-trips a drawing, edges and partial tiles included") {
    Image img(150, 130); // 64 x 64 tiles: the last column and row are partial
    img.set(0, 0, kRed);
    img.set(63, 63, kBlue);
    img.set(64, 64, kRed);
    img.set(149, 129, kBlue);
    img.set(70, 5, Rgba8{12, 34, 56, 0}); // transparent but not zero: must survive
    const SparseImage sparse(img);
    CHECK(sameImage(sparse.toImage(), img));
    CHECK(sparse.get(149, 129) == kBlue);
    CHECK(sparse.get(70, 5) == Rgba8{12, 34, 56, 0});
    CHECK(sparse.get(1, 1) == Rgba8{});
    CHECK(sparse.get(-1, 0) == Rgba8{});
    CHECK(sparse.get(150, 0) == Rgba8{});
    CHECK(sparse.bytes() > 0);
    CHECK(sparse.bytes() < 150u * 130u * sizeof(Rgba8) * 2); // at most a few tiles of the 9 here
}

TEST_CASE("SparseImage of a mostly blank board costs far less than the board") {
    Image board(4096, 4096);
    board.set(2000, 2000, kRed);
    board.set(2001, 2000, kRed);
    const SparseImage sparse(board);
    CHECK(sparse.bytes() == 64u * 64u * sizeof(Rgba8)); // the one tile holding the dot
    CHECK(sparse.bytes() * 1000 < 4096u * 4096u * sizeof(Rgba8));
}

TEST_CASE("equalsRect compares against stored and absent tiles") {
    Image img(200, 200);
    img.set(100, 100, kRed);
    SparseImage sparse(img);
    CHECK(sparse.equalsRect(img, 0, 0, 200, 200));
    CHECK(sparse.equalsRect(img, 90, 90, 20, 20));

    Image changed = img;
    changed.set(100, 101, kBlue); // inside a stored tile
    CHECK_FALSE(sparse.equalsRect(changed, 64, 64, 64, 64));
    CHECK(sparse.equalsRect(changed, 0, 0, 64, 64)); // a different tile is untouched

    Image drawnOnBlank = img;
    drawnOnBlank.set(5, 5, kBlue); // in a tile the sparse image does not store
    CHECK_FALSE(sparse.equalsRect(drawnOnBlank, 0, 0, 64, 64));
    CHECK(sparse.equalsRect(drawnOnBlank, 64, 64, 64, 64));
}

TEST_CASE("copyRect reads across tile edges, with blank parts as zero") {
    Image img(200, 200);
    for (int y = 60; y < 70; ++y)
        for (int x = 60; x < 70; ++x)
            img.set(x, y, kRed);
    const SparseImage sparse(img);
    std::vector<Rgba8> out(20u * 20u, kBlue);
    sparse.copyRect(55, 55, 20, 20, out.data()); // straddles four tiles, one of them absent
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x)
            CHECK(out[static_cast<std::size_t>(y * 20 + x)] == img.get(55 + x, 55 + y));
}

TEST_CASE("assignRect updates a rectangle, allocates what it needs and frees what became blank") {
    Image img(200, 200);
    SparseImage sparse(img);
    CHECK(sparse.bytes() == 0);

    for (int y = 60; y < 70; ++y)
        for (int x = 60; x < 70; ++x)
            img.set(x, y, kRed);
    sparse.assignRect(img, 60, 60, 10, 10); // straddles four tiles
    CHECK(sameImage(sparse.toImage(), img));
    CHECK(sparse.bytes() == 4u * 64u * 64u * sizeof(Rgba8));

    Image erased = img;
    erased.clear();
    sparse.assignRect(erased, 0, 0, 200, 200);
    CHECK(sparse.bytes() == 0);
    CHECK(sameImage(sparse.toImage(), erased));
}

TEST_CASE("assignRect only touches its rectangle") {
    Image img(128, 64);
    img.set(5, 5, kRed);
    img.set(100, 5, kBlue);
    SparseImage sparse(img);

    Image other = img;
    other.set(5, 5, kBlue);
    other.set(100, 5, kRed);
    sparse.assignRect(other, 0, 0, 64, 64); // the left tile only
    CHECK(sparse.get(5, 5) == kBlue);
    CHECK(sparse.get(100, 5) == kBlue); // the right tile still has the old pixel
}

TEST_CASE("reserveRect makes room without changing any pixel") {
    Image img(200, 200);
    SparseImage sparse(img);
    sparse.reserveRect(60, 60, 10, 10);
    CHECK(sameImage(sparse.toImage(), img));
    CHECK(sparse.bytes() == 4u * 64u * 64u * sizeof(Rgba8));
    sparse.assignRect(img, 60, 60, 10, 10); // still blank: the reserved tiles go back
    CHECK(sparse.bytes() == 0);
}
