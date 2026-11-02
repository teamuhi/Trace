#pragma once

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef TRACE_FONTS_DIR
#error "TRACE_FONTS_DIR must point at third_party/fonts"
#endif

namespace trace_test {

// The bytes of a font file from third_party/fonts (empty when it cannot be read).
inline std::vector<std::uint8_t> loadFont(const std::string& name) {
    std::ifstream in(std::string(TRACE_FONTS_DIR) + "/" + name, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Loaded once per run.
inline const std::vector<std::uint8_t>& inter() {
    static const std::vector<std::uint8_t> data = loadFont("Inter.ttf");
    return data;
}
inline const std::vector<std::uint8_t>& mono() {
    static const std::vector<std::uint8_t> data = loadFont("JetBrainsMono-Regular.ttf");
    return data;
}

} // namespace trace_test
