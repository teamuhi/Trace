#include <doctest/doctest.h>

#include "core/document.hpp"

using trace::Document;
using trace::DirtyRegion;
using trace::IRect;
using trace::Rgba8;

TEST_CASE("IRect empty and size handling") {
    CHECK(IRect{}.empty());
    CHECK(IRect{5, 5, 5, 9}.empty());
    CHECK(IRect{5, 5, 3, 9}.empty());
    CHECK(IRect{5, 5, 3, 9}.width() == 0);
    CHECK(IRect{1, 2, 4, 8}.width() == 3);
    CHECK(IRect{1, 2, 4, 8}.height() == 6);
}

TEST_CASE("unionOf and intersectionOf") {
    const IRect a{0, 0, 10, 10};
    const IRect b{5, 5, 20, 8};

    CHECK(trace::unionOf(a, b) == IRect{0, 0, 20, 10});
    CHECK(trace::unionOf(IRect{}, b) == b);
    CHECK(trace::unionOf(a, IRect{}) == a);

    CHECK(trace::intersectionOf(a, b) == IRect{5, 5, 10, 8});
    CHECK(trace::intersectionOf(a, IRect{10, 0, 12, 10}).empty());
}

TEST_CASE("DirtyRegion accumulates a bounding box and take resets it") {
    DirtyRegion d;
    CHECK(d.empty());

    d.addPixel(3, 4);
    d.addPixel(10, 2);
    CHECK(d.bounds() == IRect{3, 2, 11, 5});

    const IRect taken = d.take(IRect{0, 0, 8, 8});
    CHECK(taken == IRect{3, 2, 8, 5}); // clipped to the limit
    CHECK(d.empty());
}

TEST_CASE("A new document is fully dirty exactly once") {
    Document doc(16, 8);
    CHECK(doc.takeDirty() == IRect{0, 0, 16, 8});
    CHECK(doc.takeDirty().empty());
}

TEST_CASE("Dirty rectangles outside the image are clipped away") {
    Document doc(16, 8);
    doc.takeDirty();

    doc.markDirty(IRect{-5, -5, 4, 3});
    CHECK(doc.takeDirty() == IRect{0, 0, 4, 3});

    doc.markDirty(IRect{100, 100, 120, 120});
    CHECK(doc.takeDirty().empty());
}

TEST_CASE("reset replaces the image and marks it dirty") {
    Document doc(4, 4);
    doc.takeDirty();

    doc.reset(32, 24, Rgba8{1, 2, 3, 255});
    CHECK(doc.image().width() == 32);
    CHECK(doc.image().height() == 24);
    CHECK(doc.image().get(0, 0) == Rgba8{1, 2, 3, 255});
    CHECK(doc.takeDirty() == IRect{0, 0, 32, 24});
}

TEST_CASE("fillTestPattern keeps a transparent window and marks the document dirty") {
    Document doc(800, 600);
    doc.takeDirty();
    doc.fillTestPattern();

    CHECK(doc.image().get(0, 0) == Rgba8{230, 30, 30, 255}); // edge marker
    CHECK(doc.image().get(799 - 16, 16).a == 0);              // inside the transparent window
    CHECK(doc.takeDirty() == IRect{0, 0, 800, 600});
}
