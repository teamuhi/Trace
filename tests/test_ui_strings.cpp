#include <doctest/doctest.h>

#include <string>

#include "core/action_text.hpp"
#include "tools/tool_text.hpp"
#include "tools/toolbox.hpp"

using namespace trace;

TEST_CASE("Action names in strings.def follow the Action enum and match the README labels") {
    for (int i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        const ActionInfo& info = actionInfo(action);
        CAPTURE(info.configKey);
        CHECK(std::string(englishText(actionString(action))) == info.label);
        CHECK(std::string(englishText(groupString(info.group))) == info.group);
    }
}

TEST_CASE("Tool names in strings.def follow the ToolId enum") {
    for (int i = 0; i < kToolCount; ++i) {
        const ToolId id = static_cast<ToolId>(i);
        CHECK(std::string(englishText(toolString(id))) == Toolbox::name(id));
    }
}
