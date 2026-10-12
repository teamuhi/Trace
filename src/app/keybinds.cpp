#include "app/keybinds.hpp"

#include <cassert>
#include <cstring>

namespace trace {

namespace {

struct KeyEntry {
    const char* token;
    ImGuiKey key;
};

// Every token in chordKeyTokens() needs a row here.
constexpr KeyEntry kKeys[] = {
    {"A", ImGuiKey_A},  {"B", ImGuiKey_B},  {"C", ImGuiKey_C},  {"D", ImGuiKey_D},  {"E", ImGuiKey_E},
    {"F", ImGuiKey_F},  {"G", ImGuiKey_G},  {"H", ImGuiKey_H},  {"I", ImGuiKey_I},  {"J", ImGuiKey_J},
    {"K", ImGuiKey_K},  {"L", ImGuiKey_L},  {"M", ImGuiKey_M},  {"N", ImGuiKey_N},  {"O", ImGuiKey_O},
    {"P", ImGuiKey_P},  {"Q", ImGuiKey_Q},  {"R", ImGuiKey_R},  {"S", ImGuiKey_S},  {"T", ImGuiKey_T},
    {"U", ImGuiKey_U},  {"V", ImGuiKey_V},  {"W", ImGuiKey_W},  {"X", ImGuiKey_X},  {"Y", ImGuiKey_Y},
    {"Z", ImGuiKey_Z},  {"0", ImGuiKey_0},  {"1", ImGuiKey_1},  {"2", ImGuiKey_2},  {"3", ImGuiKey_3},
    {"4", ImGuiKey_4},  {"5", ImGuiKey_5},  {"6", ImGuiKey_6},  {"7", ImGuiKey_7},  {"8", ImGuiKey_8},
    {"9", ImGuiKey_9},  {"F1", ImGuiKey_F1}, {"F2", ImGuiKey_F2}, {"F3", ImGuiKey_F3}, {"F4", ImGuiKey_F4},
    {"F5", ImGuiKey_F5}, {"F6", ImGuiKey_F6}, {"F7", ImGuiKey_F7}, {"F8", ImGuiKey_F8}, {"F9", ImGuiKey_F9},
    {"F10", ImGuiKey_F10}, {"F11", ImGuiKey_F11}, {"F12", ImGuiKey_F12},
    {"=", ImGuiKey_Equal}, {"-", ImGuiKey_Minus}, {"[", ImGuiKey_LeftBracket}, {"]", ImGuiKey_RightBracket},
    {",", ImGuiKey_Comma}, {".", ImGuiKey_Period}, {"/", ImGuiKey_Slash}, {";", ImGuiKey_Semicolon},
    {"'", ImGuiKey_Apostrophe}, {"`", ImGuiKey_GraveAccent},
    {"Space", ImGuiKey_Space}, {"Enter", ImGuiKey_Enter}, {"Tab", ImGuiKey_Tab},
    {"Backspace", ImGuiKey_Backspace}, {"Delete", ImGuiKey_Delete}, {"Insert", ImGuiKey_Insert},
    {"Home", ImGuiKey_Home}, {"End", ImGuiKey_End}, {"PageUp", ImGuiKey_PageUp},
    {"PageDown", ImGuiKey_PageDown}, {"Up", ImGuiKey_UpArrow}, {"Down", ImGuiKey_DownArrow},
    {"Left", ImGuiKey_LeftArrow}, {"Right", ImGuiKey_RightArrow},
    {"Num0", ImGuiKey_Keypad0}, {"Num1", ImGuiKey_Keypad1}, {"Num2", ImGuiKey_Keypad2},
    {"Num3", ImGuiKey_Keypad3}, {"Num4", ImGuiKey_Keypad4}, {"Num5", ImGuiKey_Keypad5},
    {"Num6", ImGuiKey_Keypad6}, {"Num7", ImGuiKey_Keypad7}, {"Num8", ImGuiKey_Keypad8},
    {"Num9", ImGuiKey_Keypad9}, {"Num+", ImGuiKey_KeypadAdd}, {"Num-", ImGuiKey_KeypadSubtract},
    {"Num*", ImGuiKey_KeypadMultiply}, {"Num/", ImGuiKey_KeypadDivide},
};

bool tableCoversAllTokens() {
    for (const char* token : chordKeyTokens()) {
        bool found = false;
        for (const KeyEntry& entry : kKeys)
            found = found || std::strcmp(entry.token, token) == 0;
        if (!found)
            return false;
    }
    return true;
}

bool g_capturing = false;
int g_captureEndFrame = -1; // frame in which the last capture ended

} // namespace

ImGuiKeyChord toImGui(const Chord& chord) {
    for (const KeyEntry& entry : kKeys) {
        if (chord.key != entry.token)
            continue;
        ImGuiKeyChord result = entry.key;
        if (chord.mods & ModCtrl)
            result |= ImGuiMod_Ctrl;
        if (chord.mods & ModShift)
            result |= ImGuiMod_Shift;
        if (chord.mods & ModAlt)
            result |= ImGuiMod_Alt;
        return result;
    }
    return 0;
}

const char* tokenFor(ImGuiKey key) {
    for (const KeyEntry& entry : kKeys) {
        if (entry.key == key)
            return entry.token;
    }
    return nullptr;
}

bool triggered(const Keymap& keymap, Action action, bool repeat) {
    assert(tableCoversAllTokens());
    // The key press that finishes a capture must not also fire the action it was just bound to.
    if (g_capturing || ImGui::GetFrameCount() == g_captureEndFrame)
        return false;

    const bool typing = ImGui::GetIO().WantTextInput;
    const ImGuiInputFlags flags = ImGuiInputFlags_RouteGlobal | (repeat ? ImGuiInputFlags_Repeat : 0);

    bool fired = false;
    for (const auto& slot : keymap.slots(action)) {
        if (!slot)
            continue;
        if (typing && !(slot->mods & (ModCtrl | ModAlt)))
            continue; // a plain or Shift chord is just typing
        const ImGuiKeyChord chord = toImGui(*slot);
        if (chord != 0 && ImGui::Shortcut(chord, flags))
            fired = true;
    }
    return fired;
}

bool triggeredIn(const Keymap& keymap, Action action, ModeContext mode, bool repeat) {
    return contextActive(actionInfo(action).context, mode) && triggered(keymap, action, repeat);
}

std::string displayChord(const Chord& chord) {
    const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
    std::string out;
    if (chord.mods & ModCtrl)
        out += mac ? "Cmd+" : "Ctrl+";
    if (chord.mods & ModShift)
        out += "Shift+";
    if (chord.mods & ModAlt)
        out += mac ? "Option+" : "Alt+";
    return out + chord.key;
}

std::string displayShortcut(const Keymap& keymap, Action action) {
    for (const auto& slot : keymap.slots(action)) {
        if (slot)
            return displayChord(*slot);
    }
    return std::string();
}

bool actionMenuItem(const Keymap& keymap, const char* label, Action action, bool* selected, bool enabled) {
    const std::string shortcut = displayShortcut(keymap, action);
    return ImGui::MenuItem(label, shortcut.empty() ? nullptr : shortcut.c_str(), selected, enabled);
}

void setCapturing(bool capturing) {
    if (g_capturing && !capturing && ImGui::GetCurrentContext())
        g_captureEndFrame = ImGui::GetFrameCount();
    g_capturing = capturing;
}

bool isCapturing() {
    return g_capturing;
}

CaptureResult pollCapture(Chord& out) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        return CaptureResult::Cancelled;

    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeySuper)
        return CaptureResult::None; // chords never include Super, so a bound key could not fire

    for (const KeyEntry& entry : kKeys) {
        if (!ImGui::IsKeyPressed(entry.key, false))
            continue;
        out.mods = static_cast<std::uint8_t>((io.KeyCtrl ? ModCtrl : 0) | (io.KeyShift ? ModShift : 0) |
                                             (io.KeyAlt ? ModAlt : 0));
        out.key = entry.token;
        return CaptureResult::Captured;
    }
    return CaptureResult::None;
}

} // namespace trace
