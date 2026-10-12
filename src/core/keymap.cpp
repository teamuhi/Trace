#include "core/keymap.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <utility>

#include "core/ini.hpp"

namespace trace {

namespace {

constexpr char kSection[] = "keybinds";
constexpr char kSlotSeparator = '|'; // not a key token, unlike ',' and '/'

// Keep the README "Keyboard shortcuts" table in sync with these defaults. A test enforces it.
constexpr ActionInfo kActions[] = {
    {Action::NewCanvas, "file.new", "New canvas", "File", {"Ctrl+N", nullptr}},
    {Action::OpenFile, "file.open", "Open", "File", {"Ctrl+O", nullptr}},
    {Action::SaveFile, "file.save", "Save", "File", {"Ctrl+S", nullptr}},
    {Action::SaveFileAs, "file.save_as", "Save as", "File", {"Ctrl+Shift+S", nullptr}},
    {Action::OpenSettings, "file.settings", "Settings", "File", {"Ctrl+,", nullptr}},
    {Action::Undo, "edit.undo", "Undo", "Edit", {"Ctrl+Z", nullptr}},
    {Action::Redo, "edit.redo", "Redo", "Edit", {"Ctrl+Y", "Ctrl+Shift+Z"}},
    {Action::Cut, "edit.cut", "Cut", "Edit", {"Ctrl+X", nullptr}},
    {Action::Copy, "edit.copy", "Copy", "Edit", {"Ctrl+C", nullptr}},
    {Action::Paste, "edit.paste", "Paste", "Edit", {"Ctrl+V", nullptr}},
    {Action::SelectAll, "edit.select_all", "Select all", "Edit", {"Ctrl+A", nullptr}},
    {Action::Deselect, "edit.deselect", "Deselect", "Edit", {"Ctrl+D", nullptr}},
    {Action::DeleteSelection, "edit.delete", "Delete selection", "Edit", {"Delete", nullptr}},
    {Action::ResizeImage, "image.resize", "Resize image", "Image", {"Ctrl+Alt+I", nullptr}},
    {Action::CanvasSize, "image.canvas_size", "Canvas size", "Image", {"Ctrl+Alt+C", nullptr}},
    {Action::CropToSelection, "image.crop", "Crop to selection", "Image", {"Ctrl+Shift+X", nullptr}},
    {Action::ZoomIn, "view.zoom_in", "Zoom in", "View", {"Ctrl+=", "Ctrl+Num+"}},
    {Action::ZoomOut, "view.zoom_out", "Zoom out", "View", {"Ctrl+-", "Ctrl+Num-"}},
    {Action::FitToWindow, "view.fit", "Fit to window", "View", {"Ctrl+0", nullptr}},
    {Action::ActualSize, "view.actual_size", "Actual size", "View", {"Ctrl+1", nullptr}},
    {Action::TogglePixelGrid, "view.pixel_grid", "Pixel grid", "View", {"Ctrl+'", nullptr}},
    {Action::ToggleRulers, "view.rulers", "Rulers", "View", {"Ctrl+R", nullptr}},
    {Action::ToggleGuides, "view.guides", "Guides", "View", {"Ctrl+;", nullptr}},
    {Action::ToggleSnapToGuides, "view.snap_to_guides", "Snap to guides", "View", {"Ctrl+Shift+;", nullptr}},
    {Action::ToolPencil, "tools.pencil", "Pencil", "Tools", {"P", nullptr}},
    {Action::ToolBrush, "tools.brush", "Brush", "Tools", {"B", nullptr}},
    {Action::ToolEraser, "tools.eraser", "Eraser", "Tools", {"E", nullptr}},
    {Action::ToolLine, "tools.line", "Line", "Tools", {"L", nullptr}},
    {Action::ToolRectangle, "tools.rectangle", "Rectangle", "Tools", {"R", nullptr}},
    {Action::ToolEllipse, "tools.ellipse", "Ellipse", "Tools", {"O", nullptr}},
    {Action::ToolFill, "tools.fill", "Fill", "Tools", {"F", nullptr}},
    {Action::ToolPicker, "tools.picker", "Picker", "Tools", {"K", nullptr}},
    {Action::ToolSelect, "tools.select", "Select", "Tools", {"S", nullptr}},
    {Action::SwapColors, "tools.swap_colors", "Swap colors", "Tools", {"X", nullptr}},
    {Action::DecreaseSize, "tools.size_down", "Decrease size", "Tools", {"[", nullptr}},
    {Action::IncreaseSize, "tools.size_up", "Increase size", "Tools", {"]", nullptr}},
};
static_assert(std::size(kActions) == static_cast<std::size_t>(kActionCount), "one ActionInfo per Action");

constexpr const char* kKeyTokens[] = {
    "A",      "B",         "C",      "D",      "E",      "F",    "G",      "H",        "I",    "J",    "K",
    "L",      "M",         "N",      "O",      "P",      "Q",    "R",      "S",        "T",    "U",    "V",
    "W",      "X",         "Y",      "Z",      "0",      "1",    "2",      "3",        "4",    "5",    "6",
    "7",      "8",         "9",      "F1",     "F2",     "F3",   "F4",     "F5",       "F6",   "F7",   "F8",
    "F9",     "F10",       "F11",    "F12",    "=",      "-",    "[",      "]",        ",",    ".",    "/",
    ";",      "'",         "`",      "Space",  "Enter",  "Tab",  "Backspace", "Delete", "Insert", "Home", "End",
    "PageUp", "PageDown",  "Up",     "Down",   "Left",   "Right", "Num0",  "Num1",     "Num2", "Num3", "Num4",
    "Num5",   "Num6",      "Num7",   "Num8",   "Num9",   "Num+", "Num-",   "Num*",     "Num/",
};

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// The modifier named by `word`, or 0.
std::uint8_t modifierFor(std::string_view word) {
    if (equalsIgnoreCase(word, "ctrl") || equalsIgnoreCase(word, "control"))
        return ModCtrl;
    if (equalsIgnoreCase(word, "shift"))
        return ModShift;
    if (equalsIgnoreCase(word, "alt") || equalsIgnoreCase(word, "option"))
        return ModAlt;
    return 0;
}

const char* canonicalKey(std::string_view word) {
    for (const char* token : kKeyTokens) {
        if (equalsIgnoreCase(word, token))
            return token;
    }
    return nullptr;
}

} // namespace

const ActionInfo& actionInfo(Action action) {
    return kActions[static_cast<std::size_t>(action)];
}

std::span<const char* const> chordKeyTokens() {
    return kKeyTokens;
}

std::optional<Chord> parseChord(std::string_view text) {
    Chord chord;
    text = trim(text);
    // Peel leading "Modifier+" words. What is left is the key, which may itself contain '+' ("Num+").
    for (;;) {
        const std::size_t plus = text.find('+');
        if (plus == std::string_view::npos)
            break;
        const std::uint8_t mod = modifierFor(trim(text.substr(0, plus)));
        if (mod == 0)
            break;
        if (chord.mods & mod)
            return std::nullopt;
        chord.mods = static_cast<std::uint8_t>(chord.mods | mod);
        text = trim(text.substr(plus + 1));
    }

    const char* key = canonicalKey(text);
    if (!key)
        return std::nullopt;
    chord.key = key;
    return chord;
}

std::string formatChord(const Chord& chord) {
    std::string out;
    if (chord.mods & ModCtrl)
        out += "Ctrl+";
    if (chord.mods & ModShift)
        out += "Shift+";
    if (chord.mods & ModAlt)
        out += "Alt+";
    return out + chord.key;
}

Keymap::Slots defaultSlots(Action action) {
    Keymap::Slots slots;
    const ActionInfo& info = actionInfo(action);
    for (int i = 0; i < kChordSlots; ++i) {
        if (info.defaults[i])
            slots[static_cast<std::size_t>(i)] = parseChord(info.defaults[i]);
    }
    return slots;
}

void Keymap::resetToDefaults() {
    for (int i = 0; i < kActionCount; ++i)
        reset(static_cast<Action>(i));
}

void Keymap::reset(Action action) {
    slots_[index(action)] = defaultSlots(action);
}

std::vector<Chord> Keymap::chords(Action action) const {
    std::vector<Chord> out;
    for (const auto& slot : slots_[index(action)]) {
        if (slot)
            out.push_back(*slot);
    }
    return out;
}

bool Keymap::set(Action action, int slot, std::optional<Chord> chord) {
    if (slot < 0 || slot >= kChordSlots)
        return false;
    Slots& slots = slots_[index(action)];
    if (chord && slots[static_cast<std::size_t>(1 - slot)] == chord)
        return false;
    slots[static_cast<std::size_t>(slot)] = std::move(chord);
    return true;
}

std::optional<Action> Keymap::findConflict(const Chord& chord, Action ignore) const {
    for (int i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        if (action == ignore)
            continue;
        for (const auto& slot : slots_[index(action)]) {
            if (slot && *slot == chord)
                return action;
        }
    }
    return std::nullopt;
}

bool Keymap::isDefault(Action action) const {
    return slots_[index(action)] == defaultSlots(action);
}

void Keymap::load(const IniFile& ini) {
    resetToDefaults();

    for (int i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        const std::string* value = ini.get(kSection, actionInfo(action).configKey);
        if (!value)
            continue;

        Slots parsed;
        bool valid = true;
        std::string_view rest = *value;
        for (std::size_t slot = 0; slot < kChordSlots && valid; ++slot) {
            const std::size_t bar = rest.find(kSlotSeparator);
            const std::string_view piece = trim(rest.substr(0, bar));
            rest = bar == std::string_view::npos ? std::string_view() : rest.substr(bar + 1);
            if (piece.empty())
                continue;
            parsed[slot] = parseChord(piece);
            valid = parsed[slot].has_value();
        }
        // Extra slots, or both slots holding the same chord, make the entry unusable.
        valid = valid && trim(rest).empty() && !(parsed[0] && parsed[0] == parsed[1]);
        if (valid)
            slots_[index(action)] = parsed;
    }

    // A hand-edited file can bind one chord twice. The earlier action keeps it.
    for (std::size_t i = 1; i < slots_.size(); ++i) {
        for (auto& chord : slots_[i]) {
            if (!chord)
                continue;
            const auto usedEarlier = [&](const Slots& earlier) {
                return std::find(earlier.begin(), earlier.end(), chord) != earlier.end();
            };
            if (std::any_of(slots_.begin(), slots_.begin() + static_cast<std::ptrdiff_t>(i), usedEarlier))
                chord.reset();
        }
    }
}

void Keymap::save(IniFile& ini) const {
    for (int i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        if (isDefault(action))
            continue;

        const Slots& slots = slots_[index(action)];
        std::string value = slots[0] ? formatChord(*slots[0]) : std::string();
        if (slots[1]) {
            value += kSlotSeparator;
            value += formatChord(*slots[1]);
        }
        ini.set(kSection, actionInfo(action).configKey, value);
    }
}

} // namespace trace
