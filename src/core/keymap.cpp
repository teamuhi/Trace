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
    {Action::NewCanvas, "file.new", "New canvas", "File", ActionContext::Global, {"Ctrl+N", nullptr}},
    {Action::OpenFile, "file.open", "Open", "File", ActionContext::Global, {"Ctrl+O", nullptr}},
    {Action::SaveFile, "file.save", "Save", "File", ActionContext::Global, {"Ctrl+S", nullptr}},
    {Action::SaveFileAs, "file.save_as", "Save as", "File", ActionContext::Global, {"Ctrl+Shift+S", nullptr}},
    {Action::Export, "file.export", "Export", "File", ActionContext::Global, {"Ctrl+Shift+E", nullptr}},
    {Action::OpenSettings, "file.settings", "Settings", "File", ActionContext::Global, {"Ctrl+,", nullptr}},
    {Action::Undo, "edit.undo", "Undo", "Edit", ActionContext::Global, {"Ctrl+Z", nullptr}},
    {Action::Redo, "edit.redo", "Redo", "Edit", ActionContext::Global, {"Ctrl+Y", "Ctrl+Shift+Z"}},
    {Action::Cut, "edit.cut", "Cut", "Edit", ActionContext::Global, {"Ctrl+X", nullptr}},
    {Action::Copy, "edit.copy", "Copy", "Edit", ActionContext::Global, {"Ctrl+C", nullptr}},
    {Action::Paste, "edit.paste", "Paste", "Edit", ActionContext::Global, {"Ctrl+V", nullptr}},
    {Action::SelectAll, "edit.select_all", "Select all", "Edit", ActionContext::Global, {"Ctrl+A", nullptr}},
    {Action::Deselect, "edit.deselect", "Deselect", "Edit", ActionContext::Raster, {"Ctrl+D", nullptr}},
    {Action::DeleteSelection, "edit.delete", "Delete selection", "Edit", ActionContext::Global, {"Delete", nullptr}},
    {Action::ResizeImage, "image.resize", "Resize image", "Image", ActionContext::Paint, {"Ctrl+Alt+I", nullptr}},
    {Action::CanvasSize, "image.canvas_size", "Canvas size", "Image", ActionContext::Paint, {"Ctrl+Alt+C", nullptr}},
    {Action::CropToSelection, "image.crop", "Crop to selection", "Image", ActionContext::Paint, {"Ctrl+Shift+X", nullptr}},
    {Action::ZoomIn, "view.zoom_in", "Zoom in", "View", ActionContext::Global, {"Ctrl+=", "Ctrl+Num+"}},
    {Action::ZoomOut, "view.zoom_out", "Zoom out", "View", ActionContext::Global, {"Ctrl+-", "Ctrl+Num-"}},
    {Action::FitToWindow, "view.fit", "Fit to window", "View", ActionContext::Global, {"Ctrl+0", nullptr}},
    {Action::ActualSize, "view.actual_size", "Actual size", "View", ActionContext::Global, {"Ctrl+1", nullptr}},
    {Action::TogglePixelGrid, "view.pixel_grid", "Pixel grid", "View", ActionContext::Raster, {"Ctrl+'", nullptr}},
    {Action::ToggleRulers, "view.rulers", "Rulers", "View", ActionContext::Raster, {"Ctrl+R", nullptr}},
    {Action::ToggleGuides, "view.guides", "Guides", "View", ActionContext::Raster, {"Ctrl+;", nullptr}},
    {Action::ToggleSnapToGuides, "view.snap_to_guides", "Snap to guides", "View", ActionContext::Raster, {"Ctrl+Shift+;", nullptr}},
    {Action::ToolPencil, "tools.pencil", "Pencil", "Tools", ActionContext::Raster, {"P", nullptr}},
    {Action::ToolBrush, "tools.brush", "Brush", "Tools", ActionContext::Raster, {"B", nullptr}},
    {Action::ToolEraser, "tools.eraser", "Eraser", "Tools", ActionContext::Raster, {"E", nullptr}},
    {Action::ToolLine, "tools.line", "Line", "Tools", ActionContext::Raster, {"L", nullptr}},
    {Action::ToolRectangle, "tools.rectangle", "Rectangle", "Tools", ActionContext::Raster, {"R", nullptr}},
    {Action::ToolEllipse, "tools.ellipse", "Ellipse", "Tools", ActionContext::Raster, {"O", nullptr}},
    {Action::ToolFill, "tools.fill", "Fill", "Tools", ActionContext::Raster, {"F", nullptr}},
    {Action::ToolPicker, "tools.picker", "Picker", "Tools", ActionContext::Raster, {"K", nullptr}},
    {Action::ToolSelect, "tools.select", "Select", "Tools", ActionContext::Raster, {"S", nullptr}},
    {Action::SwapColors, "tools.swap_colors", "Swap colors", "Tools", ActionContext::Raster, {"X", nullptr}},
    {Action::DecreaseSize, "tools.size_down", "Decrease size", "Tools", ActionContext::Raster, {"[", nullptr}},
    {Action::IncreaseSize, "tools.size_up", "Increase size", "Tools", ActionContext::Raster, {"]", nullptr}},
    {Action::ModePaint, "modes.paint", "Paint mode", "Modes", ActionContext::Global, {"Ctrl+Shift+1", nullptr}},
    {Action::ModeFlowchart, "modes.flowchart", "Flowchart mode", "Modes", ActionContext::Global, {"Ctrl+Shift+2", nullptr}},
    {Action::ModePixelArt, "modes.pixel_art", "Pixel Art mode", "Modes", ActionContext::Global, {"Ctrl+Shift+3", nullptr}},
    {Action::NewLayer, "pixel.new_layer", "New layer", "Pixel Art", ActionContext::Pixel, {"Ctrl+Shift+N", nullptr}},
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

bool contextActive(ActionContext ctx, ModeContext mode) noexcept {
    switch (ctx) {
    case ActionContext::Global:
        return true;
    case ActionContext::Raster:
        return mode == ModeContext::Paint || mode == ModeContext::Pixel;
    case ActionContext::Paint:
        return mode == ModeContext::Paint;
    case ActionContext::Pixel:
        return mode == ModeContext::Pixel;
    case ActionContext::Flow:
        return mode == ModeContext::Flow;
    }
    return false;
}

bool contextsOverlap(ActionContext a, ActionContext b) noexcept {
    // Two contexts overlap when some mode has both live.
    for (const ModeContext mode : {ModeContext::Paint, ModeContext::Pixel, ModeContext::Flow}) {
        if (contextActive(a, mode) && contextActive(b, mode))
            return true;
    }
    return false;
}

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
    const ActionContext context = actionInfo(ignore).context;
    for (int i = 0; i < kActionCount; ++i) {
        const Action action = static_cast<Action>(i);
        if (action == ignore || !contextsOverlap(actionInfo(action).context, context))
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

    // A hand-edited file can bind one chord twice. The earlier action keeps it, but only against actions that
    // can be live at the same time: the same chord in two modes is fine.
    for (std::size_t i = 1; i < slots_.size(); ++i) {
        const ActionContext context = kActions[i].context;
        for (auto& chord : slots_[i]) {
            if (!chord)
                continue;
            for (std::size_t j = 0; j < i; ++j) {
                if (!contextsOverlap(kActions[j].context, context))
                    continue;
                if (std::find(slots_[j].begin(), slots_[j].end(), chord) != slots_[j].end()) {
                    chord.reset();
                    break;
                }
            }
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
