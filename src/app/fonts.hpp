#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct ImFont;

namespace trace {

enum class FontChoice { Classic, Inter, Mono, Custom };
inline constexpr int kFontChoiceCount = 4;
inline constexpr std::array<const char*, kFontChoiceCount> kFontChoiceKeys = {"classic", "inter", "mono", "custom"};

inline constexpr int kMinFontSize = 10;
inline constexpr int kMaxFontSize = 24;
inline constexpr int kDefaultFontSize = 14;

// The interface fonts: the classic pixel font ImGui ships, Inter and JetBrains Mono (compiled into the
// program), and one optional font file chosen by the user. ImGui loads glyphs on demand, so a font that
// covers another script just works for text in that script.
class Fonts {
public:
    // Adds the built-in fonts to the ImGui atlas. Call once, after the ImGui context exists.
    void init();

    // Makes `choice` the font of the interface and loads `customPath` (UTF-8) when it changed. Falls back
    // to Inter when the custom font is missing or unusable, in which case `error` says why and
    // `usedChoice` is not Custom. Cheap when nothing changed. Call between frames.
    void apply(FontChoice choice, const std::string& customPath, std::string& error, FontChoice& usedChoice);

    // The file name of the loaded custom font (empty when none).
    const std::string& customName() const noexcept { return customName_; }

private:
    // Reads and checks a font file. Returns an error message, empty on success.
    static std::string readFontFile(const std::string& pathUtf8, std::vector<std::uint8_t>& data);

    ImFont* classic_ = nullptr;
    ImFont* inter_ = nullptr;
    ImFont* mono_ = nullptr;
    ImFont* custom_ = nullptr;
    std::vector<std::uint8_t> customData_; // ImGui reads from it for as long as the font exists
    std::string customPath_;               // the path the loaded font came from
    std::string customName_;
};

} // namespace trace
