#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace trace {

// Every user-visible string is an entry of strings.def. The enum value is what code uses; the key is what a
// language pack file uses; the English text is built in and is also the fallback for anything a pack lacks.
enum class Str : int {
#define S(id, key, english) id,
#include "core/strings.def"
#undef S
    Count
};
inline constexpr int kStrCount = static_cast<int>(Str::Count);

const char* stringKey(Str s);
const char* englishText(Str s);

// The text in the current language, null-terminated (ImGui needs that). The pointer stays valid until the
// language is changed, which happens between frames.
const char* tr(Str s);

// tr() with `{0}`, `{1}`, ... replaced by the arguments. `{{` and `}}` are literal braces. An index with no
// argument is left as written. Never interprets printf-style formats, so a language pack cannot inject any.
std::string trf(Str s, std::initializer_list<std::string_view> args);
std::string substitute(std::string_view pattern, std::initializer_list<std::string_view> args);

// The `{n}` indices that appear in `pattern`, sorted (an index used twice appears twice).
std::vector<int> placeholderIndices(std::string_view pattern);

// A translation is only usable when it uses exactly the placeholders of the English text.
bool placeholdersMatch(std::string_view english, std::string_view translated);

// File form of a text: `\n` is a line break, `\\` a backslash. Other text passes through unchanged.
std::string escapeText(std::string_view text);
std::string unescapeText(std::string_view text);

bool isValidUtf8(std::string_view text);

struct LanguageInfo {
    std::string code;   // file name stem and settings value: letters, digits, '-' and '_' only
    std::string name;   // shown in the language list, in the language itself
    std::string author;
};

// A parsed language pack. `text[i]` is the translation of Str(i), empty when the pack has none.
struct LanguagePack {
    LanguageInfo info;
    std::vector<std::string> text;
    int translated = 0; // entries that are used
    int rejected = 0;   // entries dropped because their placeholders did not match
};

inline constexpr std::size_t kMaxLanguageFileBytes = 1024 * 1024;

// Parses a pack. Returns an error message for the user, empty on success; `out` is only filled on success.
// Unknown keys are ignored, and a translation with different placeholders is dropped (and counted).
std::string parseLanguagePack(std::string_view fileText, LanguagePack& out);

// True for a usable language code ("fil", "pt-BR", "zh_CN").
bool isValidLanguageCode(std::string_view code);

// Switches the language used by tr(). nullptr (or a pack with no strings) is English.
void setLanguage(const LanguagePack* pack);
const LanguageInfo* currentLanguage(); // null for English

// A file with every key and its English text, for translators.
std::string exportLanguageTemplate();

} // namespace trace
