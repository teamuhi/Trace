#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace trace {

// SDL and ImGui exchange paths as UTF-8 strings. std::filesystem::path(std::string) would interpret them in
// the Windows ANSI code page and mangle non-ASCII names, so convert explicitly.
inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

inline std::string utf8FromPath(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace trace
