#include "app/file_session.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <exception>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <vector>
#include <utility>

#include "app/settings.hpp"
#include "app/theme.hpp"
#include "core/i18n.hpp"
#include "io/path_utf8.hpp"

namespace fs = std::filesystem;

namespace trace {

namespace {

constexpr std::uintptr_t kOpenDialog = 1;
constexpr std::uintptr_t kSaveDialog = 2;

std::uint32_t g_dialogEvent = 0; // set by init(); the callback may run on another thread and has no `this`

// Filter names are translated each time a dialog opens. SDL may read them until the dialog reports back, and
// only one dialog is open at a time, so these statics are safe to rewrite on the next call.
struct FilterSet {
    std::vector<std::string> names;
    std::vector<SDL_DialogFileFilter> filters;
    void add(const char* name, const char* pattern) {
        names.emplace_back(name);
        filters.push_back({nullptr, pattern});
    }
    void seal() { // the name pointers are only stable once all names are in
        for (std::size_t i = 0; i < filters.size(); ++i)
            filters[i].name = names[i].c_str();
    }
};

// The adapter's open types, then a catch-all.
const FilterSet& openFilters(const FileAdapter& adapter) {
    static FilterSet set;
    set = FilterSet{};
    set.names.reserve(adapter.openFilters().size() + 1);
    for (const FileFilter& filter : adapter.openFilters())
        set.add(tr(filter.name), filter.patterns);
    set.add(tr(Str::FilterAllFiles), "*");
    set.seal();
    return set;
}

// One filter per format of the adapter, in the same order, so the filter index SDL reports maps straight back.
const FilterSet& saveFilters(const FileAdapter& adapter) {
    static FilterSet set;
    set = FilterSet{};
    set.names.reserve(adapter.saveFormats().size());
    for (const FileFormat& format : adapter.saveFormats())
        set.add(tr(format.name), format.patterns);
    set.seal();
    return set;
}

// Visible titles are translated; the part after ### keeps each popup's identity the same in every language.
std::string unsavedTitle() { return std::string(tr(Str::FileUnsavedTitle)) + "###Unsaved"; }
std::string jpegTitle() { return std::string(tr(Str::FileJpegTitle)) + "###JpegQuality"; }
std::string overwriteTitle() { return std::string(tr(Str::FileReplaceTitle)) + "###Overwrite"; }
std::string errorTitle() { return std::string(tr(Str::FileErrorTitle)) + "###Error"; }
std::string infoTitle() { return std::string(tr(Str::FileInfoTitle)) + "###Info"; }

constexpr ImGuiWindowFlags kModalFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

void centerNextWindow() {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

} // namespace

struct FileSession::DialogResult {
    std::uintptr_t kind = 0;
    int filter = -1;
    bool failed = false;
    bool cancelled = false;
    std::string path; // UTF-8
    std::string error;
};

FileSession::FileSession(FileAdapter& adapter, Settings& settings, Hooks hooks)
    : adapter_(adapter), settings_(settings), hooks_(std::move(hooks)) {}

bool FileSession::init(SDL_Window* window) {
    window_ = window;
    // Every session shares one event type: the dialog callback has no `this`, and only the session that opened
    // a dialog can be waiting for its result (switching workspaces is refused while a dialog is open).
    if (g_dialogEvent == 0)
        g_dialogEvent = SDL_RegisterEvents(1);
    dialogEvent_ = g_dialogEvent;
    return dialogEvent_ != 0;
}

bool FileSession::hasUnsavedChanges() {
    adapter_.commitPending();
    return adapter_.isModified();
}

// --- requests ----------------------------------------------------------------------------------

void FileSession::newDocumentNow() {
    doNew();
}

void FileSession::requestNew() {
    if (!busy() && hasDocument())
        guard({Followup::Kind::New, {}});
}

void FileSession::requestOpen() {
    if (!busy())
        showOpenDialog(); // unsaved changes are dealt with once a file has actually been chosen
}

void FileSession::requestOpenPath(const fs::path& path) {
    if (busy())
        return;
    if (hooks_.routeOpen && hooks_.routeOpen(path))
        return; // another workspace takes it
    guard({Followup::Kind::OpenPath, path});
}

void FileSession::requestSave() {
    if (!busy() && hasDocument())
        saveThen({});
}

void FileSession::requestSaveAs() {
    if (busy() || !hasDocument())
        return;
    afterSave_ = {};
    showSaveDialog();
}

void FileSession::requestExit() {
    if (!busy())
        guard({Followup::Kind::Exit, {}});
}

// --- flow --------------------------------------------------------------------------------------

void FileSession::run(const Followup& followup) {
    switch (followup.kind) {
    case Followup::Kind::None:
        break;
    case Followup::Kind::New:
        doNew();
        break;
    case Followup::Kind::OpenPath:
        doOpen(followup.path);
        break;
    case Followup::Kind::Exit:
        exitApproved_ = true;
        break;
    }
}

void FileSession::guard(Followup followup) {
    // Pending edits are committed first, so that "modified" reflects everything the user has drawn.
    if (!hasUnsavedChanges()) {
        run(followup);
        return;
    }
    followup_ = std::move(followup);
    request(Modal::Unsaved);
}

void FileSession::saveThen(Followup followup) {
    if (canSaveInPlace()) {
        if (writeFile(path_, *format_, settings_.jpegQuality))
            run(followup);
        return;
    }
    afterSave_ = std::move(followup);
    showSaveDialog();
}

void FileSession::doNew() {
    const std::string error = adapter_.newBlank();
    if (!error.empty()) {
        showMessage(error, true);
        return; // the old document is untouched
    }
    path_.clear();
    format_.reset();
    lockInPlace_ = false;
    if (hooks_.documentReplaced)
        hooks_.documentReplaced(true);
}

void FileSession::doOpen(const fs::path& path) {
    FileAdapter::LoadOutcome outcome = adapter_.load(path);
    if (!outcome.error.empty()) {
        showMessage(std::move(outcome.error), true);
        return;
    }

    path_ = path;
    format_ = adapter_.formatForPath(path);
    lockInPlace_ = outcome.lockInPlace;
    rememberDirectory(path);
    if (hooks_.documentReplaced)
        hooks_.documentReplaced(false);

    if (!outcome.notice.empty())
        showMessage(std::move(outcome.notice), false);
}

bool FileSession::writeFile(const fs::path& path, int format, int quality) {
    adapter_.commitPending();
    SaveOptions options;
    options.jpegQuality = std::clamp(quality, 1, 100);
    const std::string error = adapter_.save(path, format, options);
    if (!error.empty()) {
        afterSave_ = {};
        showMessage(error, true);
        return false;
    }
    path_ = path;
    format_ = format;
    lockInPlace_ = false;
    adapter_.markSaved();
    rememberDirectory(path);
    if (hooks_.saved)
        hooks_.saved(utf8FromPath(path.filename()));
    return true;
}

// A recognised extension on `chosen` decides the format. Otherwise `fallbackFormat` (the file type selected in
// the save dialog) is used and its extension is appended, so the result always names a format.
FileSession::Target FileSession::resolveTarget(const fs::path& chosen, int fallbackFormat) const {
    if (const std::optional<int> format = adapter_.formatForPath(chosen))
        return Target{chosen, *format, false};

    fs::path path = chosen;
    const auto formats = adapter_.saveFormats();
    path += std::string(".") + formats[static_cast<std::size_t>(fallbackFormat)].extension;
    return Target{std::move(path), fallbackFormat, true};
}

void FileSession::onSavePicked(const fs::path& chosen, int filterIndex) {
    if (adapter_.saveFormats().empty()) {
        afterSave_ = {};
        return;
    }
    const bool known = filterIndex >= 0 && static_cast<std::size_t>(filterIndex) < adapter_.saveFormats().size();
    const Target target = resolveTarget(chosen, known ? filterIndex : 0);

    // The native dialog already asked about replacing the name the user typed. If we appended an extension,
    // the file that now names may be a different, existing one.
    std::error_code ec;
    if (target.extensionAdded && fs::exists(target.path, ec)) {
        pendingSave_ = target;
        request(Modal::Overwrite);
        return;
    }
    continueSave(target);
}

void FileSession::continueSave(const Target& target) {
    if (adapter_.formatNeedsOptions(target.format)) {
        pendingSave_ = target;
        jpegQuality_ = std::clamp(settings_.jpegQuality, 1, 100);
        request(Modal::JpegQuality);
        return;
    }
    finishSave(target, settings_.jpegQuality);
}

void FileSession::finishSave(const Target& target, int quality) {
    const Followup next = std::move(afterSave_);
    afterSave_ = {};
    if (writeFile(target.path, target.format, quality))
        run(next);
}

// --- native dialogs ----------------------------------------------------------------------------

void FileSession::onDialogResult(void* userdata, const char* const* files, int filter) {
    // May be called from any thread: only build a result and hand it to the main loop as an event.
    auto* result = new (std::nothrow) DialogResult;
    if (!result)
        return;
    result->kind = reinterpret_cast<std::uintptr_t>(userdata);
    result->filter = filter;
    if (!files) {
        result->failed = true;
        result->error = SDL_GetError();
    } else if (!files[0]) {
        result->cancelled = true;
    } else {
        result->path = files[0];
    }

    SDL_Event event = {};
    event.type = g_dialogEvent;
    event.user.data1 = result;
    if (!SDL_PushEvent(&event))
        delete result;
}

void FileSession::showOpenDialog() {
    if (dialogEvent_ == 0) {
        showMessage(tr(Str::ErrDialogUnavailable), true);
        return;
    }
    dialogOpen_ = true;
    const FilterSet& filters = openFilters(adapter_);
    SDL_ShowOpenFileDialog(&FileSession::onDialogResult, reinterpret_cast<void*>(kOpenDialog), window_,
                           filters.filters.data(), static_cast<int>(filters.filters.size()),
                           lastDirectory_.empty() ? nullptr : lastDirectory_.c_str(), false);
}

void FileSession::showSaveDialog() {
    if (adapter_.saveFormats().empty()) {
        afterSave_ = {};
        return;
    }
    if (dialogEvent_ == 0) {
        afterSave_ = {};
        showMessage(tr(Str::ErrDialogUnavailable), true);
        return;
    }
    // Suggest the current file, or "Untitled" with the first format's extension in the folder last used.
    fs::path suggestion = path_;
    if (suggestion.empty()) {
        const auto formats = adapter_.saveFormats();
        const std::string name = formats.empty() ? std::string("Untitled") : std::string("Untitled.") + formats.front().extension;
        suggestion = (lastDirectory_.empty() ? fs::path() : pathFromUtf8(lastDirectory_)) / name;
    }
    dialogLocation_ = utf8FromPath(suggestion);

    dialogOpen_ = true;
    const FilterSet& filters = saveFilters(adapter_);
    SDL_ShowSaveFileDialog(&FileSession::onDialogResult, reinterpret_cast<void*>(kSaveDialog), window_,
                           filters.filters.data(), static_cast<int>(filters.filters.size()), dialogLocation_.c_str());
}

bool FileSession::handleEvent(const SDL_Event& event) {
    if (dialogEvent_ != 0 && event.type == dialogEvent_) {
        std::unique_ptr<DialogResult> result(static_cast<DialogResult*>(event.user.data1));
        dialogOpen_ = false;
        if (!result)
            return true;

        if (result->failed) {
            afterSave_ = {};
            showMessage(result->error.empty() ? std::string(tr(Str::ErrDialogFailed))
                                              : trf(Str::ErrDialogFailedReason, {result->error}),
                        true);
        } else if (result->cancelled) {
            afterSave_ = {};
        } else if (result->kind == kOpenDialog) {
            requestOpenPath(pathFromUtf8(result->path));
        } else {
            onSavePicked(pathFromUtf8(result->path), result->filter);
        }
        return true;
    }

    // Dropping several files delivers one event each. Only the first opens: the rest would replace it
    // straight away, and the document can only hold one image.
    switch (event.type) {
    case SDL_EVENT_DROP_BEGIN:
    case SDL_EVENT_DROP_COMPLETE:
        dropConsumed_ = false;
        return true;
    case SDL_EVENT_DROP_FILE:
        if (event.drop.data && !dropConsumed_) {
            dropConsumed_ = true;
            requestOpenPath(pathFromUtf8(event.drop.data)); // ignored while a dialog or prompt is open
        }
        return true;
    default:
        return false;
    }
}

// --- modals ------------------------------------------------------------------------------------

void FileSession::showMessage(std::string text, bool isError) {
    message_ = std::move(text);
    messageIsError_ = isError;
    request(Modal::Message);
}

std::string FileSession::displayName() const {
    return path_.empty() ? std::string(tr(Str::WordUntitled)) : utf8FromPath(path_.filename());
}

std::string FileSession::windowTitle() const {
    return displayName() + (adapter_.isModified() ? "*" : "") + " - Trace";
}

void FileSession::rememberDirectory(const fs::path& file) {
    const fs::path parent = file.parent_path();
    if (!parent.empty())
        lastDirectory_ = utf8FromPath(parent);
}

void FileSession::drawModals() {
    if (requested_ != Modal::None) {
        modal_ = requested_;
        requested_ = Modal::None;
        switch (modal_) {
        case Modal::Unsaved:
            ImGui::OpenPopup(unsavedTitle().c_str());
            break;
        case Modal::JpegQuality:
            ImGui::OpenPopup(jpegTitle().c_str());
            break;
        case Modal::Overwrite:
            ImGui::OpenPopup(overwriteTitle().c_str());
            break;
        case Modal::Message:
            ImGui::OpenPopup((messageIsError_ ? errorTitle() : infoTitle()).c_str());
            break;
        case Modal::None:
            break;
        }
    }

    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);

    switch (modal_) {
    case Modal::None:
        break;

    case Modal::Unsaved: {
        centerNextWindow();
        if (ImGui::BeginPopupModal(unsavedTitle().c_str(), nullptr, kModalFlags)) {
            ImGui::TextUnformatted(trf(Str::FileSaveChangesQuestion, {displayName()}).c_str());
            ImGui::TextDisabled("%s", tr(Str::FileChangesLost));
            ImGui::Spacing();

            Followup next;
            bool save = false;
            bool discard = false;
            bool cancel = escape;
            if (ImGui::Button(tr(Str::WordSave)))
                save = true;
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button(tr(Str::FileDontSave)))
                discard = true;
            ImGui::SameLine();
            if (ImGui::Button(tr(Str::WordCancel)))
                cancel = true;

            if (save || discard || cancel) {
                ImGui::CloseCurrentPopup();
                modal_ = Modal::None;
                next = std::move(followup_);
                followup_ = {};
                if (save)
                    saveThen(next);
                else if (discard)
                    run(next);
            }
            ImGui::EndPopup();
        }
        break;
    }

    case Modal::JpegQuality: {
        centerNextWindow();
        if (ImGui::BeginPopupModal(jpegTitle().c_str(), nullptr, kModalFlags)) {
            ImGui::TextUnformatted(tr(Str::FileQuality));
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            ImGui::SliderInt("##quality", &jpegQuality_, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextDisabled("%s", tr(Str::FileQualityHint));
            ImGui::TextDisabled("%s", tr(Str::FileJpegNoAlpha));
            ImGui::Spacing();

            bool save = false;
            bool cancel = escape;
            if (ImGui::Button(tr(Str::WordSave)))
                save = true;
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button(tr(Str::WordCancel)))
                cancel = true;

            if (save || cancel) {
                ImGui::CloseCurrentPopup();
                modal_ = Modal::None;
                if (save) {
                    settings_.jpegQuality = jpegQuality_;
                    if (hooks_.settingsChanged)
                        hooks_.settingsChanged();
                    finishSave(pendingSave_, jpegQuality_);
                } else {
                    afterSave_ = {};
                }
            }
            ImGui::EndPopup();
        }
        break;
    }

    case Modal::Overwrite: {
        centerNextWindow();
        if (ImGui::BeginPopupModal(overwriteTitle().c_str(), nullptr, kModalFlags)) {
            ImGui::TextUnformatted(trf(Str::FileAlreadyExists, {utf8FromPath(pendingSave_.path.filename())}).c_str());
            ImGui::TextDisabled("%s", tr(Str::FileReplaceQuestion));
            ImGui::Spacing();

            bool replace = false;
            bool cancel = escape;
            if (ImGui::Button(tr(Str::WordReplace)))
                replace = true;
            ImGui::SameLine();
            ImGui::SetItemDefaultFocus();
            if (ImGui::Button(tr(Str::WordCancel)))
                cancel = true;

            if (replace || cancel) {
                ImGui::CloseCurrentPopup();
                modal_ = Modal::None;
                if (replace)
                    continueSave(pendingSave_);
                else
                    afterSave_ = {};
            }
            ImGui::EndPopup();
        }
        break;
    }

    case Modal::Message: {
        centerNextWindow();
        if (ImGui::BeginPopupModal((messageIsError_ ? errorTitle() : infoTitle()).c_str(), nullptr, kModalFlags)) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            if (messageIsError_)
                ImGui::TextColored(tokens().warning, "%s", message_.c_str());
            else
                ImGui::TextUnformatted(message_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();

            const bool ok = ImGui::Button(tr(Str::WordOk));
            ImGui::SetItemDefaultFocus();
            if (ok || escape) {
                ImGui::CloseCurrentPopup();
                modal_ = Modal::None;
            }
            ImGui::EndPopup();
        }
        break;
    }
    }
}

} // namespace trace
