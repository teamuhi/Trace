#include <doctest/doctest.h>

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/ini.hpp"
#include "core/keymap.hpp"

using namespace trace;

namespace {

Chord chordOf(const char* text) {
    const auto chord = parseChord(text);
    REQUIRE(chord.has_value());
    return *chord;
}

std::string canonical(const char* text) {
    const auto chord = parseChord(text);
    return chord ? formatChord(*chord) : std::string("<invalid>");
}

} // namespace

TEST_CASE("parseChord normalises case and modifier order") {
    CHECK(canonical("ctrl+n") == "Ctrl+N");
    CHECK(canonical("shift+ctrl+n") == "Ctrl+Shift+N");
    CHECK(canonical("ALT+SHIFT+CONTROL+f5") == "Ctrl+Shift+Alt+F5");
    CHECK(canonical("  Ctrl + Shift + z ") == "Ctrl+Shift+Z");
    CHECK(canonical("p") == "P");
    CHECK(canonical("Ctrl+pageup") == "Ctrl+PageUp");
}

TEST_CASE("parseChord understands keys that contain '+' or are punctuation") {
    CHECK(canonical("Ctrl+Num+") == "Ctrl+Num+");
    CHECK(canonical("ctrl+num-") == "Ctrl+Num-");
    CHECK(canonical("Ctrl+=") == "Ctrl+=");
    CHECK(canonical("Ctrl+,") == "Ctrl+,");
    CHECK(canonical("Ctrl+'") == "Ctrl+'");
    CHECK(canonical("[") == "[");
    CHECK(canonical("Num/") == "Num/");
}

TEST_CASE("parseChord rejects malformed input") {
    for (const char* bad : {"", "   ", "Ctrl", "Ctrl+Shift", "Ctrl+", "+", "Ctrl++", "Ctrl+Ctrl+N", "Shift+shift+A",
                            "Ctrl+Nope", "Ctrl+Escape", "Escape", "Esc", "Ctrl+N+M", "F13", "Ctrl+Num"}) {
        CAPTURE(bad);
        CHECK_FALSE(parseChord(bad).has_value());
    }
}

TEST_CASE("Every key token parses and formatChord round trips") {
    for (const char* token : chordKeyTokens()) {
        CAPTURE(token);
        for (const std::uint8_t mods : {std::uint8_t(0), std::uint8_t(ModCtrl), std::uint8_t(ModCtrl | ModShift | ModAlt)}) {
            const Chord chord{mods, token};
            const auto back = parseChord(formatChord(chord));
            REQUIRE(back.has_value());
            CHECK(*back == chord);
        }
    }
}

TEST_CASE("Action table matches the enum and every default parses") {
    std::set<std::string> keys;
    std::set<std::string> labels;
    for (int i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        const ActionInfo& info = actionInfo(action);
        CAPTURE(info.label);
        CHECK(info.id == action);
        CHECK(keys.insert(info.configKey).second);
        CHECK(labels.insert(info.label).second);
        for (const char* def : info.defaults) {
            if (def)
                CHECK(parseChord(def).has_value());
        }
    }
}

TEST_CASE("No two default chords collide") {
    const Keymap map;
    std::set<std::string> seen;
    for (int i = 0; i < kActionCount; ++i) {
        for (const Chord& chord : map.chords(static_cast<Action>(i))) {
            CAPTURE(formatChord(chord));
            CHECK(seen.insert(formatChord(chord)).second);
        }
    }
}

TEST_CASE("Keymap set, conflicts and reset") {
    Keymap map;
    CHECK(map.isDefault(Action::ZoomIn));

    const Chord ctrlN = chordOf("Ctrl+N");
    CHECK(map.findConflict(ctrlN, Action::ZoomIn) == Action::NewCanvas);
    CHECK_FALSE(map.findConflict(ctrlN, Action::NewCanvas).has_value());

    // Both slots of one action cannot hold the same chord; a bad slot index is refused.
    CHECK_FALSE(map.set(Action::ZoomIn, 1, chordOf("Ctrl+=")));
    CHECK_FALSE(map.set(Action::ZoomIn, 2, chordOf("Ctrl+Shift+F8")));
    CHECK_FALSE(map.set(Action::ZoomIn, -1, chordOf("Ctrl+Shift+F8")));
    CHECK(map.isDefault(Action::ZoomIn));

    CHECK(map.set(Action::ZoomIn, 0, chordOf("Ctrl+Shift+F8")));
    CHECK_FALSE(map.isDefault(Action::ZoomIn));
    CHECK(map.chords(Action::ZoomIn).size() == 2);
    CHECK(map.set(Action::ZoomIn, 1, std::nullopt));
    CHECK(map.chords(Action::ZoomIn).size() == 1);

    map.reset(Action::ZoomIn);
    CHECK(map.isDefault(Action::ZoomIn));

    map.set(Action::ToolPencil, 0, std::nullopt);
    map.resetToDefaults();
    CHECK(map == Keymap());
}

TEST_CASE("Keymap saves only overrides") {
    Keymap map;
    IniFile untouched;
    map.save(untouched);
    CHECK(untouched.sections().empty());

    map.set(Action::ZoomIn, 0, chordOf("shift+ctrl+f8"));
    map.set(Action::ToolPencil, 0, std::nullopt);
    map.set(Action::ToolBrush, 1, chordOf("Alt+B"));

    IniFile ini;
    map.save(ini);
    const IniFile::Section* section = ini.section("keybinds");
    REQUIRE(section != nullptr);
    CHECK(section->entries.size() == 3);
    CHECK(*ini.get("keybinds", "view.zoom_in") == "Ctrl+Shift+F8|Ctrl+Num+");
    CHECK(*ini.get("keybinds", "tools.pencil") == "");
    CHECK(*ini.get("keybinds", "tools.brush") == "B|Alt+B");
    CHECK(ini.get("keybinds", "view.zoom_out") == nullptr);
}

TEST_CASE("Keymap load reads overrides, empty values and falls back on bad tokens") {
    IniFile ini;
    ini.set("keybinds", "view.zoom_in", "Ctrl+Shift+F8");   // one chord: the second slot becomes unbound
    ini.set("keybinds", "tools.pencil", "");                // explicitly unbound
    ini.set("keybinds", "tools.brush", "|Alt+B");           // only the second slot
    ini.set("keybinds", "tools.eraser", "Ctrl+Nonsense");   // bad token: default
    ini.set("keybinds", "tools.line", "Ctrl+L|Ctrl+L");     // duplicate chord in one action: default
    ini.set("keybinds", "tools.fill", "A|B|C");             // too many slots: default
    ini.set("keybinds", "not.an.action", "Ctrl+Q");         // unknown key: ignored
    ini.set("other", "view.zoom_out", "Ctrl+Shift+O");      // wrong section: ignored

    Keymap map;
    map.load(ini);
    CHECK(map.chords(Action::ZoomIn) == std::vector<Chord>{chordOf("Ctrl+Shift+F8")});
    CHECK(map.chords(Action::ToolPencil).empty());
    CHECK_FALSE(map.slots(Action::ToolBrush)[0].has_value());
    CHECK(map.slots(Action::ToolBrush)[1] == chordOf("Alt+B"));
    CHECK(map.isDefault(Action::ToolEraser));
    CHECK(map.isDefault(Action::ToolLine));
    CHECK(map.isDefault(Action::ToolFill));
    CHECK(map.isDefault(Action::ZoomOut));
}

TEST_CASE("Keymap save then load round trips") {
    Keymap original;
    original.set(Action::ZoomIn, 0, chordOf("Ctrl+Shift+F8"));
    original.set(Action::ZoomIn, 1, std::nullopt);
    original.set(Action::NewCanvas, 0, std::nullopt);
    original.set(Action::TogglePixelGrid, 1, chordOf("F2"));
    original.set(Action::SwapColors, 0, chordOf("Num*"));

    IniFile ini;
    original.save(ini);
    Keymap loaded;
    loaded.load(IniFile::parse(ini.serialize()));
    CHECK(loaded == original);
}

TEST_CASE("Keymap load drops a chord that an earlier action already uses") {
    IniFile ini;
    ini.set("keybinds", "tools.pencil", "Ctrl+N"); // New canvas comes first and keeps Ctrl+N

    Keymap map;
    map.load(ini);
    CHECK(map.chords(Action::NewCanvas) == std::vector<Chord>{chordOf("Ctrl+N")});
    CHECK(map.chords(Action::ToolPencil).empty());
}

// The README documents the default shortcuts, so a change to either side must change the other.
TEST_CASE("README keyboard shortcut table matches the default keymap") {
    std::ifstream file(TRACE_README_PATH);
    REQUIRE_MESSAGE(file.is_open(), "cannot open " << TRACE_README_PATH);

    // label -> chords as written in the README, from the '## Keyboard shortcuts' section only
    std::map<std::string, std::vector<std::string>> documented;
    std::size_t rows = 0;
    bool inSection = false;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("## ", 0) == 0) {
            inSection = line == "## Keyboard shortcuts";
            continue;
        }
        if (!inSection || line.rfind('|', 0) != 0)
            continue;

        // | Label | `Chord` / `Chord` |
        const std::size_t second = line.find('|', 1);
        if (second == std::string::npos)
            continue;
        std::string label = line.substr(1, second - 1);
        label.erase(0, label.find_first_not_of(' '));
        label.erase(label.find_last_not_of(' ') + 1);

        std::vector<std::string> chords;
        const std::size_t end = line.find('|', second + 1);
        const std::string cell = line.substr(second + 1, end == std::string::npos ? end : end - second - 1);
        for (std::size_t pos = cell.find('`'); pos != std::string::npos;) {
            const std::size_t close = cell.find('`', pos + 1);
            if (close == std::string::npos)
                break;
            chords.push_back(cell.substr(pos + 1, close - pos - 1));
            pos = cell.find('`', close + 1);
        }
        if (chords.empty())
            continue; // header and separator rows

        ++rows;
        documented[label] = chords;
    }

    REQUIRE_MESSAGE(rows > 0, "no rows found under '## Keyboard shortcuts' in README.md");
    CHECK_MESSAGE(rows == documented.size(), "README lists a shortcut label twice");

    const Keymap defaults;
    std::size_t expectedRows = 0;
    for (int i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        const ActionInfo& info = actionInfo(action);
        std::vector<std::string> expected;
        for (const Chord& chord : defaults.chords(action))
            expected.push_back(formatChord(chord));
        if (expected.empty())
            continue;
        ++expectedRows;

        const std::string label = info.label;
        CAPTURE(label);
        const auto it = documented.find(info.label);
        REQUIRE_MESSAGE(it != documented.end(), "README is missing a row for '" << info.label << "'");
        CHECK(it->second == expected);
    }
    CHECK_MESSAGE(documented.size() == expectedRows, "README has a row that is not a default action");
}
