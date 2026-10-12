#include "app/theme_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/color_text.hpp"
#include "core/i18n.hpp"
#include "core/ini.hpp"
#include "io/path_utf8.hpp"

namespace trace {

namespace {

constexpr std::uintmax_t kMaxThemeFileBytes = 1024 * 1024;

constexpr const char* sectionFor(bool dark) {
    return dark ? "theme.dark" : "theme.light";
}

ImVec4 toVec(Rgba8 c) {
    return ImVec4(static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
                  static_cast<float>(c.b) / 255.0f, static_cast<float>(c.a) / 255.0f);
}

Rgba8 fromVec(const ImVec4& v) {
    const auto q = [](float x) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(x * 255.0f)), 0, 255));
    };
    return Rgba8{q(v.x), q(v.y), q(v.z), q(v.w)};
}

} // namespace

void readThemeOverrides(const IniFile& ini, ThemeOverrides& out) {
    const std::span<const TokenInfo> infos = tokenInfos();
    for (const bool dark : {false, true}) {
        for (std::size_t i = 0; i < infos.size(); ++i) {
            const std::string* value = ini.get(sectionFor(dark), infos[i].key);
            if (!value)
                continue;
            if (const auto parsed = parseColor(*value)) // a bad value keeps the built-in color
                out.side(dark)[i] = toVec(*parsed);
        }
    }
}

void writeThemeOverrides(IniFile& ini, const ThemeOverrides& overrides) {
    const std::span<const TokenInfo> infos = tokenInfos();
    for (const bool dark : {false, true}) {
        for (std::size_t i = 0; i < infos.size(); ++i) {
            if (const auto& value = overrides.side(dark)[i])
                ini.set(sectionFor(dark), infos[i].key, formatColor(fromVec(*value)));
        }
    }
}

std::string saveThemeFile(const std::string& pathUtf8, const ThemeOverrides& overrides) {
    IniFile ini;
    writeThemeOverrides(ini, overrides);
    // An unmodified theme still writes something loadable: the marker section makes the file self-describing.
    ini.set("trace", "kind", "theme");

    std::ofstream out(pathFromUtf8(pathUtf8), std::ios::binary | std::ios::trunc);
    out << ini.serialize();
    out.flush();
    if (!out)
        return trf(Str::ErrThemeWrite, {pathUtf8});
    return {};
}

std::string loadThemeFile(const std::string& pathUtf8, ThemeOverrides& out) {
    namespace fs = std::filesystem;
    const fs::path path = pathFromUtf8(pathUtf8);
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec)
        return tr(Str::ErrThemeRead);
    if (size > kMaxThemeFileBytes)
        return tr(Str::ErrThemeTooLarge);

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return tr(Str::ErrThemeRead);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    ThemeOverrides loaded;
    readThemeOverrides(IniFile::parse(text), loaded);
    if (loaded.empty())
        return tr(Str::ErrThemeEmpty);
    out = loaded;
    return {};
}

} // namespace trace
