#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include "core/i18n.hpp"
#include "core/ini.hpp"

using namespace trace;

namespace {

// Leaves the global language as it found it, so tests do not depend on their order.
struct LanguageGuard {
    ~LanguageGuard() { setLanguage(nullptr); }
};

std::string packWith(const std::string& body) {
    return "[meta]\ncode = test\nname = Testish\nauthor = Someone\n\n[strings]\n" + body;
}

} // namespace

TEST_CASE("Every key is unique, non-empty and usable as an ini key") {
    std::set<std::string> keys;
    for (int i = 0; i < kStrCount; ++i) {
        const std::string key = stringKey(static_cast<Str>(i));
        CAPTURE(key);
        CHECK_FALSE(key.empty());
        CHECK(key.find('=') == std::string::npos);
        CHECK(key.find(' ') == std::string::npos);
        CHECK(key.front() != ';');
        CHECK(key.front() != '#');
        CHECK(keys.insert(key).second);
    }
}

TEST_CASE("English texts are non-empty, trimmed and have well-formed placeholders") {
    for (int i = 0; i < kStrCount; ++i) {
        const std::string key = stringKey(static_cast<Str>(i));
        const std::string text = englishText(static_cast<Str>(i));
        CAPTURE(key);
        CHECK_FALSE(text.empty());
        CHECK(text.front() != ' ');
        CHECK(text.back() != ' ');
        CHECK(text.find('\r') == std::string::npos);
        // Every '{' is either an escaped "{{" or the start of {digits}; the same for '}'.
        for (std::size_t k = 0; k < text.size(); ++k) {
            if (text[k] == '{' || text[k] == '}') {
                if (k + 1 < text.size() && text[k + 1] == text[k]) {
                    ++k;
                    continue;
                }
                CHECK(text[k] == '{');
                std::size_t j = k + 1;
                while (j < text.size() && text[j] >= '0' && text[j] <= '9')
                    ++j;
                CHECK(j > k + 1);
                CHECK(j < text.size());
                CHECK(text[j] == '}');
                k = j;
            }
        }
    }
}

TEST_CASE("tr returns English until a language is set") {
    CHECK(std::string(tr(Str::MenuFile)) == "File");
    CHECK(std::string(tr(Str::MenuFile)) == englishText(Str::MenuFile));
}

TEST_CASE("substitute replaces numbered placeholders and unescapes braces") {
    CHECK(substitute("{0} x {1}", {"3", "4"}) == "3 x 4");
    CHECK(substitute("{1}{0}{1}", {"a", "b"}) == "bab");
    CHECK(substitute("{{0}} is literal, {0} is not", {"v"}) == "{0} is literal, v is not");
    CHECK(substitute("no placeholders", {"unused"}) == "no placeholders");
    CHECK(substitute("100% {0}", {"done"}) == "100% done");
    CHECK(substitute("%s %d {0}", {"x"}) == "%s %d x"); // printf directives are plain text
}

TEST_CASE("substitute leaves a missing argument visible and ignores malformed braces") {
    CHECK(substitute("{0} {2}", {"a"}) == "a {2}");
    CHECK(substitute("{ 0}", {"a"}) == "{ 0}");
    CHECK(substitute("{a}", {"a"}) == "{a}");
    CHECK(substitute("{", {"a"}).empty() == false);
    CHECK(substitute("{0", {"a"}) == "{0");
    CHECK(substitute("}", {"a"}) == "}");
    CHECK(substitute("{99999999999999999999}", {"a"}).size() > 0);
}

TEST_CASE("placeholderIndices and placeholdersMatch compare the multiset of indices") {
    CHECK(placeholderIndices("{1} and {0} and {1}") == std::vector<int>{0, 1, 1});
    CHECK(placeholderIndices("{{0}}").empty());
    CHECK(placeholdersMatch("{0} of {1}", "{1} von {0}")); // order may change
    CHECK_FALSE(placeholdersMatch("{0} of {1}", "{0} von"));
    CHECK_FALSE(placeholdersMatch("{0}", "{0} {0}"));
    CHECK_FALSE(placeholdersMatch("no placeholder", "{0}"));
    CHECK(placeholdersMatch("none", "keine"));
}

TEST_CASE("escapeText and unescapeText round-trip line breaks and backslashes") {
    const std::string original = "line one\nline two \\ with a slash";
    CHECK(unescapeText(escapeText(original)) == original);
    CHECK(escapeText("a\nb") == "a\\nb");
    CHECK(unescapeText("a\\nb") == "a\nb");
    CHECK(unescapeText("c:\\temp") == "c:\\temp"); // \t is not an escape here
    CHECK(unescapeText("trailing\\") == "trailing\\");
}

TEST_CASE("isValidUtf8 accepts text and rejects broken sequences") {
    CHECK(isValidUtf8(""));
    CHECK(isValidUtf8("plain ascii"));
    CHECK(isValidUtf8("\xC3\xA4\xC3\xB6")); // a-umlaut o-umlaut
    CHECK(isValidUtf8("\xE6\x97\xA5\xE6\x9C\xAC")); // two CJK characters
    CHECK(isValidUtf8("\xF0\x9F\x98\x80"));         // an emoji
    CHECK_FALSE(isValidUtf8("\xC3"));               // cut off
    CHECK_FALSE(isValidUtf8("\xC3\x28"));           // bad continuation
    CHECK_FALSE(isValidUtf8("\xC0\xAF"));           // overlong
    CHECK_FALSE(isValidUtf8("\xED\xA0\x80"));       // a surrogate
    CHECK_FALSE(isValidUtf8("\xF4\x90\x80\x80"));   // past U+10FFFF
    CHECK_FALSE(isValidUtf8("\xFF"));
    CHECK_FALSE(isValidUtf8("\x80"));
}

TEST_CASE("Language codes are restricted to safe file name characters") {
    CHECK(isValidLanguageCode("fil"));
    CHECK(isValidLanguageCode("pt-BR"));
    CHECK(isValidLanguageCode("zh_CN"));
    CHECK_FALSE(isValidLanguageCode(""));
    CHECK_FALSE(isValidLanguageCode("../etc"));
    CHECK_FALSE(isValidLanguageCode("a b"));
    CHECK_FALSE(isValidLanguageCode("a/b"));
    CHECK_FALSE(isValidLanguageCode("a.b"));
    CHECK_FALSE(isValidLanguageCode(std::string(33, 'a')));
}

TEST_CASE("A pack translates the keys it names and English stays for the rest") {
    LanguageGuard guard;
    LanguagePack pack;
    const std::string text = packWith(std::string(stringKey(Str::MenuFile)) + " = Datei\n" +
                                      stringKey(Str::MenuEdit) + " = Bearbeiten\nunknown.key = ignored\n");
    REQUIRE(parseLanguagePack(text, pack).empty());
    CHECK(pack.info.code == "test");
    CHECK(pack.info.name == "Testish");
    CHECK(pack.info.author == "Someone");
    CHECK(pack.translated == 2);
    CHECK(pack.rejected == 0);

    setLanguage(&pack);
    CHECK(std::string(tr(Str::MenuFile)) == "Datei");
    CHECK(std::string(tr(Str::MenuEdit)) == "Bearbeiten");
    CHECK(std::string(tr(Str::MenuView)) == "View");
    REQUIRE(currentLanguage() != nullptr);
    CHECK(currentLanguage()->code == "test");

    setLanguage(nullptr);
    CHECK(std::string(tr(Str::MenuFile)) == "File");
    CHECK(currentLanguage() == nullptr);
}

TEST_CASE("trf substitutes into the translated text") {
    LanguageGuard guard;
    LanguagePack pack;
    REQUIRE(parseLanguagePack(packWith(std::string(stringKey(Str::ToastCropped)) + " = {1} mal {0} zugeschnitten\n"), pack).empty());
    setLanguage(&pack);
    CHECK(trf(Str::ToastCropped, {"10", "20"}) == "20 mal 10 zugeschnitten");
}

TEST_CASE("A translation with different placeholders is dropped and counted") {
    LanguagePack pack;
    const std::string text = packWith(std::string(stringKey(Str::ToastCropped)) + " = Zugeschnitten auf {0}\n" +
                                      stringKey(Str::MenuFile) + " = Datei\n" + stringKey(Str::ToastSaved) +
                                      " = {0} gespeichert {1}\n");
    REQUIRE(parseLanguagePack(text, pack).empty());
    CHECK(pack.translated == 1);
    CHECK(pack.rejected == 2);
    CHECK(pack.text[static_cast<std::size_t>(Str::ToastCropped)].empty());
    CHECK(pack.text[static_cast<std::size_t>(Str::MenuFile)] == "Datei");
}

TEST_CASE("Line breaks in a pack are written as backslash n") {
    LanguagePack pack;
    REQUIRE(parseLanguagePack(packWith(std::string(stringKey(Str::PaletteTransparentTip)) + " = A\\nB\n"), pack).empty());
    CHECK(pack.text[static_cast<std::size_t>(Str::PaletteTransparentTip)] == "A\nB");
}

TEST_CASE("A pack needs a valid [meta] code and name and at least one usable string") {
    LanguagePack pack;
    const std::string line = std::string(stringKey(Str::MenuFile)) + " = Datei\n";
    CHECK_FALSE(parseLanguagePack("[strings]\n" + line, pack).empty());
    CHECK_FALSE(parseLanguagePack("[meta]\nname = X\n[strings]\n" + line, pack).empty());
    CHECK_FALSE(parseLanguagePack("[meta]\ncode = ../x\nname = X\n[strings]\n" + line, pack).empty());
    CHECK_FALSE(parseLanguagePack("[meta]\ncode = x\n[strings]\n" + line, pack).empty());
    CHECK_FALSE(parseLanguagePack("[meta]\ncode = x\nname = " + std::string(65, 'n') + "\n[strings]\n" + line, pack).empty());
    CHECK_FALSE(parseLanguagePack(packWith(""), pack).empty());
    CHECK_FALSE(parseLanguagePack(packWith("unknown.key = nothing useful\n"), pack).empty());
    CHECK(pack.text.empty()); // nothing was filled in by the failures
}

TEST_CASE("Oversized and non-UTF-8 packs are rejected") {
    LanguagePack pack;
    std::string big = packWith(std::string(stringKey(Str::MenuFile)) + " = Datei\n");
    big.append(kMaxLanguageFileBytes, ';');
    CHECK_FALSE(parseLanguagePack(big, pack).empty());

    const std::string bad = packWith(std::string(stringKey(Str::MenuFile)) + " = Dat\xC3\n");
    CHECK_FALSE(parseLanguagePack(bad, pack).empty());
}

TEST_CASE("The exported template loads back with every English text unchanged") {
    const std::string text = exportLanguageTemplate();
    LanguagePack pack;
    REQUIRE(parseLanguagePack(text, pack).empty());
    CHECK(pack.translated == kStrCount);
    CHECK(pack.rejected == 0);
    for (int i = 0; i < kStrCount; ++i) {
        CAPTURE(stringKey(static_cast<Str>(i)));
        CHECK(pack.text[static_cast<std::size_t>(i)] == englishText(static_cast<Str>(i)));
    }
}

TEST_CASE("A pack never leaks strings from an earlier one") {
    LanguageGuard guard;
    LanguagePack first;
    LanguagePack second;
    REQUIRE(parseLanguagePack(packWith(std::string(stringKey(Str::MenuFile)) + " = Eins\n"), first).empty());
    REQUIRE(parseLanguagePack(packWith(std::string(stringKey(Str::MenuEdit)) + " = Zwei\n"), second).empty());
    setLanguage(&first);
    setLanguage(&second);
    CHECK(std::string(tr(Str::MenuFile)) == "File");
    CHECK(std::string(tr(Str::MenuEdit)) == "Zwei");
}

// The packs that ship with the program must stay valid as strings.def evolves: no dropped translations
// (a placeholder changed), no keys that no longer exist, and every English text still has a translation.
TEST_CASE("Shipped language packs are complete and consistent with strings.def") {
    namespace fs = std::filesystem;
    const fs::path dir = TRACE_LANGUAGES_DIR;
    REQUIRE_MESSAGE(fs::is_directory(dir), "missing " << TRACE_LANGUAGES_DIR);

    int packs = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".ini")
            continue;
        ++packs;
        CAPTURE(entry.path().string());

        std::ifstream in(entry.path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        LanguagePack pack;
        const std::string error = parseLanguagePack(text, pack);
        REQUIRE_MESSAGE(error.empty(), error);
        CHECK(pack.rejected == 0);
        CHECK(pack.translated == kStrCount);
        CHECK(entry.path().stem().string() == pack.info.code);

        // Keys that strings.def no longer has would be silently ignored; flag them.
        std::set<std::string> known;
        for (int i = 0; i < kStrCount; ++i)
            known.insert(stringKey(static_cast<Str>(i)));
        const IniFile ini = IniFile::parse(text);
        if (const IniFile::Section* strings = ini.section("strings")) {
            for (const IniFile::Entry& e : strings->entries)
                CHECK_MESSAGE(known.count(e.key) == 1, "unknown key " << e.key);
        }
    }
    CHECK(packs >= 1);
}
