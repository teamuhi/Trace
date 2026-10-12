#pragma once

#include <optional>
#include <string>

#include "app/settings.hpp"
#include "app/theme.hpp"
#include "core/keymap.hpp"

namespace trace {

class Toolbox;
class NativeDialogs;
class LanguageManager;

// The floating Settings window: General, Theme, View, Tools, Pixel Art, Scripts, Keybinds and About. It edits a Settings
// value in place; the App applies and saves it.
class SettingsWindow {
public:
    ~SettingsWindow() { endCapture(); }

    void open();
    bool isOpen() const noexcept { return open_; }

    // Call every frame. Returns true when `settings` changed this frame. `settingsPath` is shown on the
    // About page (empty when settings cannot be saved).
    bool draw(Settings& settings, Toolbox& toolbox, const std::string& settingsPath, NativeDialogs& dialogs,
              LanguageManager& languages);

private:
    struct SlotRef {
        Action action;
        int slot;
    };
    struct Conflict {
        SlotRef target;
        Chord chord;
        Action owner; // the action that currently uses the chord
    };

    bool drawCategories(Settings& settings, float footer);
    bool drawGeneral(Settings& settings);
    bool drawTheme(Settings& settings);
    bool drawView(Settings& settings);
    bool drawTools(Settings& settings, Toolbox& toolbox);
    bool drawPixelArt(Settings& settings);
    void drawScripts();
    bool drawKeybinds(Settings& settings);
    void drawAbout(const std::string& settingsPath);

    bool pollKeyCapture(Keymap& keymap);
    bool drawKeySlot(Keymap& keymap, Action action, int slot);
    bool drawConflictBanner(Keymap& keymap);

    void startCapture(SlotRef target);
    void endCapture();

    NativeDialogs* dialogs_ = nullptr; // valid while draw() runs
    LanguageManager* languages_ = nullptr;
    bool wasOpen_ = false;
    bool open_ = false;
    bool focusRequested_ = false;

    std::optional<SlotRef> capture_;
    std::optional<Conflict> conflict_;
    bool navWasEnabled_ = false;
    char filter_[64] = {};

    bool editDark_ = true;      // which palette the Theme page edits
    bool editInit_ = false;     // editDark_ follows the applied theme when the page is first shown
    float themeRows_[kTokenCount][4] = {};

    float primary_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float secondary_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

} // namespace trace
