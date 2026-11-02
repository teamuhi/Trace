#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "core/palette.hpp"

namespace trace {

// Small pure helpers behind the Palette window: reordering, removal and the selection index that has to follow
// them, plus the "has the palette changed since it was saved" bookkeeping. Header-only so they are unit-tested
// without ImGui.

// Where a swatch dragged from `from` ends up when dropped on the left half (`after` false) or the right half
// (`after` true) of swatch `target`: the index movePaletteColor() takes, that is the position after the dragged
// color has been taken out of the list. Dropping a swatch on itself is the caller's no-op (from == target).
inline int paletteDropIndex(int from, int target, bool after) noexcept {
    if (after)
        return from < target ? target : target + 1;
    return from < target ? target - 1 : target;
}

// Moves the color at `from` so that it sits at index `to`. False, with the palette untouched, for an index out
// of range or when nothing would move.
inline bool movePaletteColor(Palette& palette, int from, int to) {
    const int count = static_cast<int>(palette.colors.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return false;
    const auto begin = palette.colors.begin();
    if (from < to)
        std::rotate(begin + from, begin + from + 1, begin + to + 1);
    else
        std::rotate(begin + to, begin + from, begin + from + 1);
    return true;
}

// Where the swatch that was at `index` is after movePaletteColor(from, to). A negative index (no selection)
// stays negative.
inline int indexAfterMove(int index, int from, int to) noexcept {
    if (index < 0)
        return index;
    if (index == from)
        return to;
    if (from < to && index > from && index <= to)
        return index - 1;
    if (from > to && index >= to && index < from)
        return index + 1;
    return index;
}

// Removes the color at `index`. False when the index is out of range.
inline bool removePaletteColor(Palette& palette, int index) {
    if (index < 0 || index >= static_cast<int>(palette.colors.size()))
        return false;
    palette.colors.erase(palette.colors.begin() + index);
    return true;
}

// The selection after removePaletteColor(removed): the removed swatch leaves no selection, later ones shift down.
inline int indexAfterRemove(int selected, int removed) noexcept {
    if (selected < 0 || selected == removed)
        return -1;
    return selected > removed ? selected - 1 : selected;
}

// A selection index that no longer fits the palette (it shrank, or was replaced) is no selection.
inline int clampSelection(int selected, std::size_t count) noexcept {
    return selected >= 0 && static_cast<std::size_t>(selected) < count ? selected : -1;
}

// The first swatch with the same red, green and blue as `color` (alpha is not compared: palette colors are
// opaque), or -1.
inline int findPaletteColor(const Palette& palette, Rgba8 color) noexcept {
    for (std::size_t i = 0; i < palette.colors.size(); ++i) {
        const Rgba8& c = palette.colors[i];
        if (c.r == color.r && c.g == color.g && c.b == color.b)
            return static_cast<int>(i);
    }
    return -1;
}

// A file name stem for saving a palette called `name`: characters Windows and Unix reject in a file name become
// '_', the ends are trimmed, and an empty result is "palette".
inline std::string paletteFileStem(std::string_view name) {
    std::string stem;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        const bool bad = u < 0x20 || u == 0x7F || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
                         c == '\\' || c == '|' || c == '?' || c == '*';
        stem += bad ? '_' : c;
    }
    const auto isSpace = [](char c) { return c == ' ' || c == '.'; }; // a trailing dot or space is invalid on Windows
    while (!stem.empty() && isSpace(stem.back()))
        stem.pop_back();
    std::size_t start = 0;
    while (start < stem.size() && stem[start] == ' ')
        ++start;
    stem.erase(0, start);
    return stem.empty() ? std::string("palette") : stem;
}

// The palette a Pixel Art document carries, with a revision so that a palette change counts as a modification
// for the unsaved-changes guard (the document's own undo history knows nothing about the palette).
struct PaletteState {
    Palette palette = defaultPalette();
    std::uint64_t revision = 0;
    std::uint64_t savedRevision = 0;

    // The palette was edited.
    void touch() noexcept { ++revision; }
    bool modified() const noexcept { return revision != savedRevision; }
    void markSaved() noexcept { savedRevision = revision; }
    // A different palette that counts as unmodified (New, Open).
    void reset(Palette next) {
        palette = std::move(next);
        ++revision;
        savedRevision = revision;
    }
};

} // namespace trace
