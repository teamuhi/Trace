#pragma once

#include "core/i18n.hpp"
#include "tools/tool.hpp"

namespace trace {

// The translatable name of a tool. strings.def lists ToolXxx in the order of ToolId; Toolbox::name stays
// English because settings.ini stores it.
inline Str toolString(ToolId id) {
    static_assert(kToolCount == 9, "add the new tool's name to strings.def and check the order");
    return static_cast<Str>(static_cast<int>(Str::ToolPencil) + static_cast<int>(id));
}

} // namespace trace
