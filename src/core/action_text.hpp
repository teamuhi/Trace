#pragma once

#include <string_view>

#include "core/i18n.hpp"
#include "core/keymap.hpp"

namespace trace {

// The translatable name of an action. strings.def lists the ActionXxx entries in the order of the Action
// enum, and a test checks that the English texts match ActionInfo::label.
inline Str actionString(Action action) {
    return static_cast<Str>(static_cast<int>(Str::ActionNewCanvas) + static_cast<int>(action));
}

// The translatable name of an action group ("File", "Edit", "Image", "View", "Tools", "Modes", "Pixel Art").
inline Str groupString(std::string_view group) {
    if (group == "File")
        return Str::GroupFile;
    if (group == "Edit")
        return Str::GroupEdit;
    if (group == "Image")
        return Str::GroupImage;
    if (group == "View")
        return Str::GroupView;
    if (group == "Modes")
        return Str::GroupModes;
    if (group == "Pixel Art")
        return Str::GroupPixelArt;
    return Str::GroupTools;
}

} // namespace trace
