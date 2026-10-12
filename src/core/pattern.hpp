#pragma once

#include <array>

namespace trace {

// Pattern drawn behind the image (the canvas backdrop) or behind transparent pixels.
enum class PatternStyle { Solid, Checker, Grid, Dots, Diagonal, Cross };
inline constexpr int kPatternStyleCount = 6;

// Names used in settings.ini (English identifiers, never translated).
inline constexpr std::array<const char*, kPatternStyleCount> kPatternStyleKeys = {"solid", "checker", "grid",
                                                                                   "dots",  "diagonal", "cross"};

inline constexpr int kMinPatternSpacing = 8;
inline constexpr int kMaxPatternSpacing = 64;
inline constexpr int kDefaultPatternSpacing = 16;

// The spacing to really draw with: `spacing` doubled until a width x height area holds at most `maxMarks`
// cells. Keeps a huge window (or a tiny spacing) from producing a drawing list that costs real time each
// frame. Non-finite or non-positive input returns the smallest sane spacing.
float patternSpacingFor(float spacing, float width, float height, int maxMarks = 20000) noexcept;

} // namespace trace
