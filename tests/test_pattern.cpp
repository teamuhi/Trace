#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>

#include "core/pattern.hpp"

using namespace trace;

TEST_CASE("patternSpacingFor keeps a normal window at the requested spacing") {
    CHECK(patternSpacingFor(16.0f, 1920.0f, 1080.0f) == 16.0f);
    CHECK(patternSpacingFor(8.0f, 800.0f, 600.0f) == 8.0f);
}

TEST_CASE("patternSpacingFor doubles the spacing until the cell count fits") {
    // 8 px cells over 8000 x 8000 would be a million cells.
    const float spacing = patternSpacingFor(8.0f, 8000.0f, 8000.0f, 20000);
    CHECK(spacing > 8.0f);
    const double cells = (8000.0 / spacing + 1.0) * (8000.0 / spacing + 1.0);
    CHECK(cells <= 20000.0);
    // ... and it is a power-of-two multiple, so the pattern does not shimmer as the window resizes by a pixel.
    CHECK(spacing / 8.0f == doctest::Approx(std::round(spacing / 8.0f)));
    CHECK(patternSpacingFor(8.0f, 8000.0f, 8000.0f, 20000) == spacing);
}

TEST_CASE("patternSpacingFor survives hostile input") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(patternSpacingFor(nan, 100.0f, 100.0f) >= 1.0f);
    CHECK(patternSpacingFor(-5.0f, 100.0f, 100.0f) >= 1.0f);
    CHECK(patternSpacingFor(0.0f, 100.0f, 100.0f) >= 1.0f);
    CHECK(patternSpacingFor(16.0f, nan, 100.0f) == 16.0f);
    CHECK(patternSpacingFor(16.0f, -1.0f, 100.0f) == 16.0f);
    CHECK(patternSpacingFor(16.0f, 1.0e30f, 1.0e30f) >= 16.0f); // finite, terminates
    CHECK(patternSpacingFor(16.0f, inf, 100.0f) == 16.0f);
    CHECK(patternSpacingFor(16.0f, 100.0f, 100.0f, 0) == 16.0f);
}

TEST_CASE("Every pattern style has a unique ini key") {
    for (int i = 0; i < kPatternStyleCount; ++i) {
        for (int j = i + 1; j < kPatternStyleCount; ++j)
            CHECK(std::string(kPatternStyleKeys[static_cast<std::size_t>(i)]) !=
                  std::string(kPatternStyleKeys[static_cast<std::size_t>(j)]));
    }
}
