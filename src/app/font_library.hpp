#pragma once

#include <cstdint>
#include <future>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "io/system_fonts.hpp"

namespace trace {

// One style of a family ("Bold Italic") and the file it comes from; `path` is empty for a font built into the program.
struct FontStyleEntry {
    std::string style;
    std::string path;
};

struct FontFamily {
    std::string name;
    std::vector<FontStyleEntry> styles; // Regular first
    bool builtIn = false;
};

// The fonts the Text tool can draw with: the two built into the program (Inter, JetBrains Mono) and every font
// installed on the system, found by a scan that runs on a thread of its own so the program never waits for it. The
// bytes of a font are read from its file when it is first drawn with and kept for a while (a few fonts at most).
class FontLibrary {
public:
    FontLibrary();

    // Starts the scan of the system's font folders. Safe to call more than once; only the first call scans.
    void startScan();

    // True once the scan is over (also when it was never started: there is nothing to wait for). Call each frame to
    // pick up the result.
    bool ready();

    // The built-in families first, then the installed ones sorted by name. Grows once, when the scan ends.
    const std::vector<FontFamily>& families() const noexcept { return families_; }

    // The family called `name` (ignoring case), or null.
    const FontFamily* find(std::string_view name) const;

    // The style to use for `wanted` inside `family`: the same name when the family has it, else Regular, else the
    // first one. `family` must have at least one style.
    static const FontStyleEntry& pickStyle(const FontFamily& family, std::string_view wanted);

    // The bytes of the font file for a family and style, loaded on demand. A family that is not known (yet) waits for
    // the scan to finish first. An unknown or unreadable font gives the built-in Inter, so the Text tool always has
    // something to draw with. The span stays valid until the next call.
    std::span<const std::uint8_t> bytes(std::string_view family, std::string_view style);

private:
    struct Cached {
        std::string path;
        std::vector<std::uint8_t> data;
    };

    void merge(const std::vector<SystemFont>& fonts);
    std::span<const std::uint8_t> builtInBytes(std::string_view family) const;

    std::vector<FontFamily> families_;
    std::vector<Cached> cache_; // most recently used last
    std::future<std::vector<SystemFont>> scan_;
    bool scanStarted_ = false;
    bool scanMerged_ = false;
};

// The one library of the program.
FontLibrary& fontLibrary();

} // namespace trace
