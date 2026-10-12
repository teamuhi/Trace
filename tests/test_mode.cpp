#include <doctest/doctest.h>

#include <set>
#include <string>

#include "app/mode.hpp"
#include "core/action_text.hpp"

using namespace trace;

TEST_CASE("Mode keys round trip and reject anything else") {
    std::set<std::string> keys;
    for (const Mode mode : kAllModes) {
        const std::string key = modeKey(mode);
        CHECK(keys.insert(key).second); // unique
        const auto back = modeFromKey(key);
        REQUIRE(back.has_value());
        CHECK(*back == mode);
    }
    CHECK(modeKey(Mode::Paint) == std::string("paint")); // what settings.ini stores
    CHECK_FALSE(modeFromKey("").has_value());
    CHECK_FALSE(modeFromKey("Paint").has_value());
    CHECK_FALSE(modeFromKey("pixel art").has_value());
    CHECK_FALSE(modeFromKey("paint ").has_value());
    CHECK_FALSE(modeFromKey("nonsense").has_value());
}

TEST_CASE("The modes array is indexed by the Mode enum") {
    for (int i = 0; i < kModeCount; ++i)
        CHECK(static_cast<int>(kAllModes[static_cast<std::size_t>(i)]) == i);
}

TEST_CASE("Each mode has its own switch action and keymap context") {
    std::set<int> actions;
    std::set<int> contexts;
    for (const Mode mode : kAllModes) {
        const Action action = modeAction(mode);
        CHECK(actions.insert(static_cast<int>(action)).second);
        CHECK(contexts.insert(static_cast<int>(modeContext(mode))).second);

        const ActionInfo& info = actionInfo(action);
        CHECK(std::string(info.group) == "Modes");
        CHECK(info.context == ActionContext::Global); // switching must work from every mode
        // The action is named after the mode: "Paint mode", "Flowchart mode", "Pixel Art mode".
        CHECK(std::string(info.label) == std::string(englishText(modeString(mode))) + " mode");
    }
}
