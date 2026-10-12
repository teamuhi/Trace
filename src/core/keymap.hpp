#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace trace {

class IniFile;

enum class Action {
    // File
    NewCanvas,
    OpenFile,
    SaveFile,
    SaveFileAs,
    OpenSettings,
    // Edit
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    SelectAll,
    Deselect,
    DeleteSelection,
    // Image
    ResizeImage,
    CanvasSize,
    CropToSelection,
    // View
    ZoomIn,
    ZoomOut,
    FitToWindow,
    ActualSize,
    TogglePixelGrid,
    ToggleRulers,
    ToggleGuides,
    ToggleSnapToGuides,
    // Tools
    ToolPencil,
    ToolBrush,
    ToolEraser,
    ToolLine,
    ToolRectangle,
    ToolEllipse,
    ToolFill,
    ToolPicker,
    ToolSelect,
    SwapColors,
    DecreaseSize,
    IncreaseSize,
    Count
};
inline constexpr int kActionCount = static_cast<int>(Action::Count);
inline constexpr int kChordSlots = 2;

struct ActionInfo {
    Action id;
    const char* configKey;             // key inside the [keybinds] section of settings.ini
    const char* label;                 // shown on the Keybinds page and in the README table
    const char* group;                 // File, Edit, Image, View or Tools
    const char* defaults[kChordSlots]; // chord strings, nullptr for an empty slot
};

const ActionInfo& actionInfo(Action action);

enum ChordMod : std::uint8_t { ModCtrl = 1, ModShift = 2, ModAlt = 4 };

struct Chord {
    std::uint8_t mods = 0; // ChordMod bitmask
    std::string key;       // canonical token, see chordKeyTokens()

    bool operator==(const Chord& other) const noexcept { return mods == other.mods && key == other.key; }
};

// Every key a chord can use, in canonical spelling: A-Z, 0-9, F1-F12, punctuation, navigation keys and
// the numeric keypad (Num0-Num9, Num+, Num-, Num*, Num/). Escape is deliberately absent: it is reserved
// for cancelling a gesture or a key capture.
std::span<const char* const> chordKeyTokens();

// Case-insensitive, any modifier order ("shift+ctrl+n" -> Ctrl+Shift+N). Rejects empty input, modifiers
// alone, repeated modifiers and unknown or reserved keys.
std::optional<Chord> parseChord(std::string_view text);

// Canonical text: modifiers in the order Ctrl+Shift+Alt, then the key.
std::string formatChord(const Chord& chord);

// Two chord slots per action; an empty slot is unbound. Pure data, no input handling.
class Keymap {
public:
    using Slots = std::array<std::optional<Chord>, kChordSlots>;

    Keymap() { resetToDefaults(); }

    void resetToDefaults();
    void reset(Action action);

    const Slots& slots(Action action) const { return slots_[index(action)]; }
    std::vector<Chord> chords(Action action) const; // bound slots only

    // Returns false (and changes nothing) for a bad slot index or when the other slot of the same action
    // already holds `chord`.
    bool set(Action action, int slot, std::optional<Chord> chord);

    // The first action other than `ignore` that uses `chord` in any slot.
    std::optional<Action> findConflict(const Chord& chord, Action ignore) const;

    bool isDefault(Action action) const;

    // Reads only [keybinds]: `configKey = chordA | chordB`. A missing key means the default, an empty value
    // means unbound, and an unparsable value falls back to the default for that action. A chord already
    // used by an earlier action is dropped so no chord ever triggers two actions.
    void load(const IniFile& ini);

    // Writes only the actions that differ from their defaults.
    void save(IniFile& ini) const;

    bool operator==(const Keymap& other) const { return slots_ == other.slots_; }

private:
    static std::size_t index(Action action) { return static_cast<std::size_t>(action); }

    std::array<Slots, kActionCount> slots_;
};

// The default slots of one action, parsed from ActionInfo::defaults.
Keymap::Slots defaultSlots(Action action);

} // namespace trace
