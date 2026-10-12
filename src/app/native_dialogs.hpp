#pragma once

#include <cstdint>
#include <string>

struct SDL_Window;
union SDL_Event;

namespace trace {

// Native open and save dialogs for the Settings window (a font file, a theme, a language pack). They are
// asynchronous: SDL may report from another thread, so a result comes back as an SDL user event, which
// the main loop hands to handleEvent(). File > Open and Save use their own path in FileSession.
class NativeDialogs {
public:
    // What the dialog was for. The filters and the default name follow from it.
    enum class Tag { FontFile = 1, ThemeImport, ThemeExport, LanguageImport, LanguageTemplate };

    struct Result {
        Tag tag = Tag::FontFile;
        std::string path;  // UTF-8; empty when cancelled or failed
        std::string error; // set when the dialog itself failed
        bool cancelled = false;
    };

    // Registers the event type. Returns false when SDL has none left (dialogs are then unavailable).
    bool init(SDL_Window* window);
    bool available() const noexcept { return event_ != 0; }

    // Only one dialog at a time; both return false while one is open or when unavailable.
    bool open(Tag tag);
    bool save(Tag tag, const std::string& suggestedName);

    bool busy() const noexcept { return pending_; }

    // True when `event` was a dialog result (then `out` is filled and busy() is false again).
    bool handleEvent(const SDL_Event& event, Result& out);

private:
    SDL_Window* window_ = nullptr;
    std::uint32_t event_ = 0;
    bool pending_ = false;
    std::string location_; // kept alive for the dialog
    std::string lastDirectory_;
};

} // namespace trace
