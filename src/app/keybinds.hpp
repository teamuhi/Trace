#pragma once

#include <imgui.h>

#include <string>

#include "core/keymap.hpp"
#include "tools/tool.hpp"

namespace trace {

// ImGui side of the keymap: turns the pure-data chords of core/keymap.hpp into key chords, polls them and
// captures new ones.

// The tool-selection action for a tool (the Action and ToolId enums list the tools in the same order).
inline Action toolAction(ToolId id) {
    return static_cast<Action>(static_cast<int>(Action::ToolPencil) + static_cast<int>(id));
}

// 0 when the chord's key has no ImGui equivalent.
ImGuiKeyChord toImGui(const Chord& chord);

// Canonical chord token for an ImGui key, or nullptr when the key cannot be bound.
const char* tokenFor(ImGuiKey key);

// True on the frame one of the action's chords is pressed. Chords without Ctrl or Alt are ignored while a
// text field wants input, and every chord is ignored while a capture is in progress. `repeat` lets a held
// key fire again (brush size).
bool triggered(const Keymap& keymap, Action action, bool repeat = false);

// Menu and tooltip text for the first bound chord of the action, "" when unbound. Ctrl reads Cmd on macOS.
std::string displayShortcut(const Keymap& keymap, Action action);
std::string displayChord(const Chord& chord);

// The settings window sets this while it waits for a new chord so no shortcut fires meanwhile.
void setCapturing(bool capturing);
bool isCapturing();

enum class CaptureResult { None, Captured, Cancelled };

// Watches this frame's key presses. Modifiers alone are ignored until a real key arrives; Escape cancels.
CaptureResult pollCapture(Chord& out);

} // namespace trace
