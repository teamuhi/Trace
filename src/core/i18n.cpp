#include "core/i18n.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "core/ini.hpp"

namespace trace {

namespace {

struct Entry {
    const char* key;
    const char* english;
};

constexpr Entry kEntries[] = {
#define S(id, key, english) {key, english},
#include "core/strings.def"
#undef S
};
static_assert(sizeof(kEntries) / sizeof(kEntries[0]) == static_cast<std::size_t>(kStrCount));

std::vector<std::string> g_text; // current pack, indexed by Str; empty entries fall back to English
std::unique_ptr<LanguageInfo> g_info;

} // namespace

const char* stringKey(Str s) {
    return kEntries[static_cast<std::size_t>(s)].key;
}

const char* englishText(Str s) {
    return kEntries[static_cast<std::size_t>(s)].english;
}

const char* tr(Str s) {
    const std::size_t i = static_cast<std::size_t>(s);
    if (i < g_text.size() && !g_text[i].empty())
        return g_text[i].c_str();
    return kEntries[i].english;
}

std::vector<int> placeholderIndices(std::string_view pattern) {
    std::vector<int> out;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if ((c == '{' || c == '}') && i + 1 < pattern.size() && pattern[i + 1] == c) {
            ++i; // an escaped brace
            continue;
        }
        if (c != '{')
            continue;
        std::size_t j = i + 1;
        int value = 0;
        bool digits = false;
        while (j < pattern.size() && pattern[j] >= '0' && pattern[j] <= '9' && value < 10000) {
            value = value * 10 + (pattern[j] - '0');
            digits = true;
            ++j;
        }
        if (digits && j < pattern.size() && pattern[j] == '}') {
            out.push_back(value);
            i = j;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool placeholdersMatch(std::string_view english, std::string_view translated) {
    return placeholderIndices(english) == placeholderIndices(translated);
}

std::string substitute(std::string_view pattern, std::initializer_list<std::string_view> args) {
    std::string out;
    out.reserve(pattern.size() + 16);
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if ((c == '{' || c == '}') && i + 1 < pattern.size() && pattern[i + 1] == c) {
            out += c;
            ++i;
            continue;
        }
        if (c == '{') {
            std::size_t j = i + 1;
            std::size_t value = 0;
            bool digits = false;
            while (j < pattern.size() && pattern[j] >= '0' && pattern[j] <= '9' && value < 10000) {
                value = value * 10 + static_cast<std::size_t>(pattern[j] - '0');
                digits = true;
                ++j;
            }
            if (digits && j < pattern.size() && pattern[j] == '}') {
                if (value < args.size())
                    out += *(args.begin() + static_cast<std::ptrdiff_t>(value));
                else
                    out.append(pattern.substr(i, j - i + 1)); // no such argument: leave it visible
                i = j;
                continue;
            }
        }
        out += c;
    }
    return out;
}

std::string trf(Str s, std::initializer_list<std::string_view> args) {
    return substitute(tr(s), args);
}

std::string escapeText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '\n')
            out += "\\n";
        else if (c == '\\')
            out += "\\\\";
        else
            out += c;
    }
    return out;
}

std::string unescapeText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == 'n' || text[i + 1] == '\\')) {
            out += text[i + 1] == 'n' ? '\n' : '\\';
            ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

bool isValidUtf8(std::string_view text) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(text.data());
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const std::uint8_t c = p[i];
        if (c < 0x80) {
            ++i;
            continue;
        }
        std::size_t extra = 0;
        std::uint32_t minimum = 0;
        std::uint32_t code = 0;
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            minimum = 0x80;
            code = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            minimum = 0x800;
            code = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            minimum = 0x10000;
            code = c & 0x07;
        } else {
            return false;
        }
        if (i + extra >= n)
            return false; // cut off in the middle of a character
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((p[i + k] & 0xC0) != 0x80)
                return false;
            code = (code << 6) | (p[i + k] & 0x3F);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
            return false; // overlong, out of range or a surrogate
        i += extra + 1;
    }
    return true;
}

bool isValidLanguageCode(std::string_view code) {
    if (code.empty() || code.size() > 32)
        return false;
    return std::all_of(code.begin(), code.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::string parseLanguagePack(std::string_view fileText, LanguagePack& out) {
    if (fileText.size() > kMaxLanguageFileBytes)
        return "This file is too large to be a language pack.";
    if (!isValidUtf8(fileText))
        return "This file is not valid UTF-8 text.";

    const IniFile ini = IniFile::parse(fileText);
    const std::string* code = ini.get("meta", "code");
    const std::string* name = ini.get("meta", "name");
    if (!code || !isValidLanguageCode(*code))
        return "The pack needs a [meta] code made of letters, digits, '-' and '_'.";
    if (!name || name->empty() || name->size() > 64)
        return "The pack needs a [meta] name (at most 64 characters).";

    LanguagePack pack;
    pack.info.code = *code;
    pack.info.name = *name;
    if (const std::string* author = ini.get("meta", "author"))
        pack.info.author = author->substr(0, 128);
    pack.text.assign(static_cast<std::size_t>(kStrCount), std::string());

    for (int i = 0; i < kStrCount; ++i) {
        const Str id = static_cast<Str>(i);
        const std::string* value = ini.get("strings", stringKey(id));
        if (!value || value->empty())
            continue;
        std::string text = unescapeText(*value);
        if (!placeholdersMatch(englishText(id), text)) {
            ++pack.rejected;
            continue;
        }
        pack.text[static_cast<std::size_t>(i)] = std::move(text);
        ++pack.translated;
    }
    if (pack.translated == 0)
        return "The pack has no usable translations.";
    out = std::move(pack);
    return {};
}

void setLanguage(const LanguagePack* pack) {
    if (!pack || pack->text.size() != static_cast<std::size_t>(kStrCount)) {
        g_text.clear();
        g_info.reset();
        return;
    }
    g_text = pack->text;
    g_info = std::make_unique<LanguageInfo>(pack->info);
}

const LanguageInfo* currentLanguage() {
    return g_info.get();
}

std::string exportLanguageTemplate() {
    std::string out;
    out += "; Trace language pack.\n";
    out += "; Put your translation after the '=' of each line in [strings] and leave a line empty or delete it to\n";
    out += "; keep the English text. Keep {0}, {1}, ... exactly as they are (they are replaced by numbers and\n";
    out += "; names); a line whose placeholders differ is ignored. Write \\n for a line break.\n";
    out += "; Save as <code>.ini in the Trace languages folder, or use Settings > General > Import language.\n";
    out += "\n[meta]\ncode = xx\nname = Your language, in your language\nauthor = \n\n[strings]\n";
    for (int i = 0; i < kStrCount; ++i) {
        const Str id = static_cast<Str>(i);
        out += stringKey(id);
        out += " = ";
        out += escapeText(englishText(id));
        out += '\n';
    }
    return out;
}

} // namespace trace
