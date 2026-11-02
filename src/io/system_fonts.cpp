#include "io/system_fonts.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>

#include "core/font_check.hpp"
#include "core/font_names.hpp"
#include "io/path_utf8.hpp"

namespace trace {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kHeadBytes = 8192;               // enough for the table directory of a face
constexpr std::uint32_t kMaxNameTableBytes = 1u << 20; // a name table is a few kilobytes; more is damage

std::string lowerAscii(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isFontFile(const fs::path& path) {
    const std::string ext = lowerAscii(utf8FromPath(path.extension()));
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc";
}

bool readNames(const fs::path& path, FontNames& names) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::vector<std::uint8_t> head(kHeadBytes);
    in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(in.gcount()));
    const auto range = findFontNameTable(head);
    if (!range || range->length < 6 || range->length > kMaxNameTableBytes)
        return false;
    in.clear();
    in.seekg(static_cast<std::streamoff>(range->offset), std::ios::beg);
    std::vector<std::uint8_t> table(range->length);
    in.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(table.size()));
    if (static_cast<std::size_t>(in.gcount()) != table.size())
        return false;
    return parseFontNameTable(table, names);
}

// Regular first, then the usual order of styles, then alphabetical.
int styleRank(const std::string& style) {
    const std::string s = lowerAscii(style);
    if (s == "regular" || s == "normal" || s == "book" || s == "roman")
        return 0;
    if (s == "italic" || s == "oblique")
        return 1;
    if (s == "bold")
        return 2;
    if (s == "bold italic" || s == "bold oblique")
        return 3;
    return 4;
}

} // namespace

std::vector<fs::path> systemFontDirectories() {
    std::vector<fs::path> dirs;
    const auto env = [](const char* name) -> std::string {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t length = 0;
        std::string result;
        if (_dupenv_s(&value, &length, name) == 0 && value != nullptr)
            result = value;
        std::free(value);
        return result;
#else
        const char* v = std::getenv(name);
        return v ? std::string(v) : std::string();
#endif
    };
#if defined(_WIN32)
    const std::string windir = env("WINDIR").empty() ? env("SystemRoot") : env("WINDIR");
    dirs.push_back(windir.empty() ? fs::path("C:/Windows/Fonts") : fs::path(windir) / "Fonts");
    if (const std::string local = env("LOCALAPPDATA"); !local.empty())
        dirs.push_back(fs::path(local) / "Microsoft" / "Windows" / "Fonts");
#elif defined(__APPLE__)
    dirs.push_back("/System/Library/Fonts");
    dirs.push_back("/Library/Fonts");
    if (const std::string home = env("HOME"); !home.empty())
        dirs.push_back(fs::path(home) / "Library" / "Fonts");
#else
    dirs.push_back("/usr/share/fonts");
    dirs.push_back("/usr/local/share/fonts");
    if (const std::string home = env("HOME"); !home.empty()) {
        dirs.push_back(fs::path(home) / ".fonts");
        dirs.push_back(fs::path(home) / ".local" / "share" / "fonts");
    }
#endif
    return dirs;
}

std::vector<SystemFont> scanFontFiles(std::span<const fs::path> directories) {
    std::vector<SystemFont> fonts;
    std::set<std::string> seen; // lowercase "family\nstyle"
    std::size_t looked = 0;
    for (const fs::path& dir : directories) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec))
            continue;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::error_code fileEc;
            if (!it->is_regular_file(fileEc) || fileEc || !isFontFile(it->path()))
                continue;
            if (++looked > kMaxFontFiles)
                break;
            FontNames names;
            if (!readNames(it->path(), names))
                continue;
            if (!seen.insert(lowerAscii(names.family) + "\n" + lowerAscii(names.style)).second)
                continue;
            fonts.push_back(SystemFont{std::move(names.family), std::move(names.style), utf8FromPath(it->path())});
        }
        if (looked > kMaxFontFiles)
            break;
    }
    std::sort(fonts.begin(), fonts.end(), [](const SystemFont& a, const SystemFont& b) {
        const std::string fa = lowerAscii(a.family);
        const std::string fb = lowerAscii(b.family);
        if (fa != fb)
            return fa < fb;
        const int ra = styleRank(a.style);
        const int rb = styleRank(b.style);
        if (ra != rb)
            return ra < rb;
        return a.style < b.style;
    });
    return fonts;
}

bool readFontData(const std::string& pathUtf8, std::vector<std::uint8_t>& data, std::uintmax_t maxBytes) {
    data.clear();
    const fs::path path = pathFromUtf8(pathUtf8);
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec || size < 256 || size > maxBytes)
        return false;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    const bool collection = data.size() >= 4 && data[0] == 't' && data[1] == 't' && data[2] == 'c' && data[3] == 'f';
    if (!collection && !looksLikeFont(data))
        data.clear();
    return !data.empty();
}

} // namespace trace
