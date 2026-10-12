#include "app/native_dialogs.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <iterator>
#include <memory>
#include <new>

#include "core/i18n.hpp"
#include "io/path_utf8.hpp"

namespace trace {

namespace {

std::uint32_t g_event = 0; // the callback may run on another thread and has no `this`

struct Raw {
    std::uintptr_t tag = 0;
    bool failed = false;
    bool cancelled = false;
    std::string path;
    std::string error;
};

// Filter names are translated each time a dialog opens and kept in a static until the next one, because SDL
// may read them until the dialog reports back (only one dialog is open at a time).
struct FilterSet {
    std::string names[2];
    SDL_DialogFileFilter filters[2];
};

const FilterSet& filtersFor(NativeDialogs::Tag tag) {
    static FilterSet set;
    const char* pattern = "";
    Str name = Str::FilterFonts;
    switch (tag) {
    case NativeDialogs::Tag::FontFile:
        pattern = "ttf;otf";
        name = Str::FilterFonts;
        break;
    case NativeDialogs::Tag::ThemeImport:
    case NativeDialogs::Tag::ThemeExport:
        pattern = "ini";
        name = Str::FilterTheme;
        break;
    case NativeDialogs::Tag::LanguageImport:
    case NativeDialogs::Tag::LanguageTemplate:
        pattern = "ini";
        name = Str::FilterLanguage;
        break;
    }
    set.names[0] = tr(name);
    set.names[1] = tr(Str::FilterAllFiles);
    set.filters[0] = {set.names[0].c_str(), pattern};
    set.filters[1] = {set.names[1].c_str(), "*"};
    return set;
}

void SDLCALL onResult(void* userdata, const char* const* files, int /*filter*/) {
    // May be called from any thread: only build a result and hand it to the main loop as an event.
    auto* raw = new (std::nothrow) Raw;
    if (!raw)
        return;
    raw->tag = reinterpret_cast<std::uintptr_t>(userdata);
    if (!files) {
        raw->failed = true;
        raw->error = SDL_GetError();
    } else if (!files[0]) {
        raw->cancelled = true;
    } else {
        raw->path = files[0];
    }

    SDL_Event event = {};
    event.type = g_event;
    event.user.data1 = raw;
    if (!SDL_PushEvent(&event))
        delete raw;
}

} // namespace

bool NativeDialogs::init(SDL_Window* window) {
    window_ = window;
    g_event = SDL_RegisterEvents(1);
    event_ = g_event;
    return event_ != 0;
}

bool NativeDialogs::open(Tag tag) {
    if (!available() || pending_)
        return false;
    pending_ = true;
    const FilterSet& f = filtersFor(tag);
    SDL_ShowOpenFileDialog(&onResult, reinterpret_cast<void*>(static_cast<std::uintptr_t>(tag)), window_, f.filters, 2,
                           lastDirectory_.empty() ? nullptr : lastDirectory_.c_str(), false);
    return true;
}

bool NativeDialogs::save(Tag tag, const std::string& suggestedName) {
    if (!available() || pending_)
        return false;
    pending_ = true;
    namespace fs = std::filesystem;
    fs::path suggestion = pathFromUtf8(suggestedName);
    if (!lastDirectory_.empty())
        suggestion = pathFromUtf8(lastDirectory_) / suggestion;
    location_ = utf8FromPath(suggestion);
    const FilterSet& f = filtersFor(tag);
    SDL_ShowSaveFileDialog(&onResult, reinterpret_cast<void*>(static_cast<std::uintptr_t>(tag)), window_, f.filters, 2,
                           location_.c_str());
    return true;
}

bool NativeDialogs::handleEvent(const SDL_Event& event, Result& out) {
    if (event_ == 0 || event.type != event_)
        return false;
    std::unique_ptr<Raw> raw(static_cast<Raw*>(event.user.data1));
    pending_ = false;
    out = Result{};
    if (!raw) {
        out.cancelled = true;
        return true;
    }
    out.tag = static_cast<Tag>(raw->tag);
    out.cancelled = raw->cancelled;
    out.error = raw->failed ? (raw->error.empty() ? std::string(tr(Str::ErrDialogFailed)) : raw->error) : std::string();
    out.path = raw->path;
    if (!out.path.empty()) {
        const std::filesystem::path parent = pathFromUtf8(out.path).parent_path();
        if (!parent.empty())
            lastDirectory_ = utf8FromPath(parent);
    }
    return true;
}

} // namespace trace
