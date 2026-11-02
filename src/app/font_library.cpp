#include "app/font_library.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <utility>

#include "app/embedded_fonts.hpp"

namespace trace {

namespace {

constexpr const char* kSansName = "Inter";
constexpr const char* kMonoName = "JetBrains Mono";
constexpr std::size_t kMaxCachedFonts = 6;
constexpr std::size_t kMaxCachedBytes = 96u * 1024u * 1024u;

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

} // namespace

FontLibrary::FontLibrary() {
    families_.push_back(FontFamily{kSansName, {FontStyleEntry{"Regular", {}}}, true});
    families_.push_back(FontFamily{kMonoName, {FontStyleEntry{"Regular", {}}}, true});
}

void FontLibrary::startScan() {
    if (scanStarted_)
        return;
    scanStarted_ = true;
    scan_ = std::async(std::launch::async, [] {
        const std::vector<std::filesystem::path> dirs = systemFontDirectories();
        return scanFontFiles(dirs);
    });
}

bool FontLibrary::ready() {
    if (!scanStarted_ || scanMerged_)
        return true;
    if (scan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return false;
    merge(scan_.get());
    scanMerged_ = true;
    return true;
}

void FontLibrary::merge(const std::vector<SystemFont>& fonts) {
    for (const SystemFont& font : fonts) {
        // The built-in families are listed once, from the program; an installed copy of the same name is left out.
        FontFamily* family = nullptr;
        for (FontFamily& existing : families_) {
            if (equalsIgnoreCase(existing.name, font.family)) {
                family = &existing;
                break;
            }
        }
        if (family != nullptr && family->builtIn)
            continue;
        if (family == nullptr) {
            families_.push_back(FontFamily{font.family, {}, false});
            family = &families_.back();
        }
        family->styles.push_back(FontStyleEntry{font.style, font.path});
    }
}

const FontFamily* FontLibrary::find(std::string_view name) const {
    for (const FontFamily& family : families_) {
        if (equalsIgnoreCase(family.name, name))
            return &family;
    }
    return nullptr;
}

const FontStyleEntry& FontLibrary::pickStyle(const FontFamily& family, std::string_view wanted) {
    for (const FontStyleEntry& entry : family.styles) {
        if (equalsIgnoreCase(entry.style, wanted))
            return entry;
    }
    for (const FontStyleEntry& entry : family.styles) {
        if (equalsIgnoreCase(entry.style, "Regular"))
            return entry;
    }
    return family.styles.front();
}

std::span<const std::uint8_t> FontLibrary::builtInBytes(std::string_view family) const {
    return equalsIgnoreCase(family, kMonoName) ? monoFontBytes() : interFontBytes();
}

std::span<const std::uint8_t> FontLibrary::bytes(std::string_view family, std::string_view style) {
    const FontFamily* found = find(family);
    if (found == nullptr && scanStarted_ && !scanMerged_) {
        scan_.wait(); // a saved font that is not a built-in one: it may be installed, so see the whole list
        ready();
        found = find(family);
    }
    if (found == nullptr)
        return interFontBytes();
    if (found->builtIn)
        return builtInBytes(found->name);

    const FontStyleEntry& entry = pickStyle(*found, style);
    for (std::size_t i = 0; i < cache_.size(); ++i) {
        if (cache_[i].path == entry.path) {
            if (i + 1 != cache_.size())
                std::rotate(cache_.begin() + static_cast<std::ptrdiff_t>(i),
                            cache_.begin() + static_cast<std::ptrdiff_t>(i) + 1, cache_.end());
            return cache_.back().data;
        }
    }

    Cached loaded;
    loaded.path = entry.path;
    if (!readFontData(entry.path, loaded.data))
        return interFontBytes(); // the file moved or is unreadable
    std::size_t total = loaded.data.size();
    for (const Cached& each : cache_)
        total += each.data.size();
    while (!cache_.empty() && (cache_.size() >= kMaxCachedFonts || total > kMaxCachedBytes)) {
        total -= cache_.front().data.size();
        cache_.erase(cache_.begin());
    }
    cache_.push_back(std::move(loaded));
    return cache_.back().data;
}

FontLibrary& fontLibrary() {
    static FontLibrary library;
    return library;
}

} // namespace trace
