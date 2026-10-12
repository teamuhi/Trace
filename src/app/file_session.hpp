#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "io/image_io.hpp"

struct SDL_Window;
union SDL_Event;

namespace trace {

class Document;
struct Settings;

// Everything about the document's relationship to files: which file it came from, New / Open / Save /
// Save As, the native file dialogs, drag-and-drop, the "unsaved changes" guard and the messages shown when
// something fails.
//
// Native dialogs are asynchronous and may report from another thread, so their results come back as SDL
// events (see handleEvent). Modals are drawn by drawModals() inside the ImGui frame; while any dialog or
// modal is open busy() is true and the app ignores shortcuts, so two file operations never overlap.
class FileSession {
public:
    struct Hooks {
        std::function<void(bool createdBlank)> documentReplaced; // true: a blank canvas, false: a file was opened
        std::function<void()> settingsChanged;                   // the JPEG quality changed
        std::function<void(const std::string&)> saved;           // a file was written; the argument is its name
    };

    FileSession(Document& document, Settings& settings, Hooks hooks);

    void setHooks(Hooks hooks) { hooks_ = std::move(hooks); }

    // Registers the dialog-result event. Returns false if SDL has no event types left.
    bool init(SDL_Window* window);

    // Start from a blank canvas without asking (startup).
    void newDocumentNow();

    // Each of these may first ask what to do with unsaved changes. They do nothing while busy().
    void requestNew();
    void requestOpen();
    void requestOpenPath(const std::filesystem::path& path);
    void requestSave();
    void requestSaveAs();
    void requestExit();

    // Set once the user has settled any unsaved changes after requestExit().
    bool exitApproved() const noexcept { return exitApproved_; }

    // Shows a message dialog (an error, or plain information) for failures that happen elsewhere.
    void report(std::string text, bool isError) { showMessage(std::move(text), isError); }

    // Handles dialog results and dropped files. Returns true when the event was consumed.
    bool handleEvent(const SDL_Event& event);

    void drawModals();
    bool busy() const noexcept { return dialogOpen_ || modal_ != Modal::None || requested_ != Modal::None; }

    // "name* - Trace"; the star marks unsaved changes.
    std::string windowTitle() const;

private:
    enum class Modal { None, Unsaved, JpegQuality, Overwrite, Message };

    // What to do once unsaved changes have been dealt with (or a save has finished).
    struct Followup {
        enum class Kind { None, New, OpenPath, Exit } kind = Kind::None;
        std::filesystem::path path;
    };

    struct DialogResult;
    static void onDialogResult(void* userdata, const char* const* files, int filter);

    void run(const Followup& followup);
    void guard(Followup followup);
    void saveThen(Followup followup);

    void doNew();
    void doOpen(const std::filesystem::path& path);
    void showOpenDialog();
    void showSaveDialog();
    void onSavePicked(const std::filesystem::path& chosen, int filterIndex);
    void continueSave(const SaveTarget& target);
    void finishSave(const SaveTarget& target, int quality);
    bool writeFile(const std::filesystem::path& path, ImageFormat format, int quality);

    bool canSaveInPlace() const noexcept { return !path_.empty() && format_ && !lockInPlace_; }
    std::string displayName() const;
    void rememberDirectory(const std::filesystem::path& file);
    void request(Modal modal) noexcept { requested_ = modal; }
    void showMessage(std::string text, bool isError);

    Document& document_;
    Settings& settings_;
    Hooks hooks_;
    SDL_Window* window_ = nullptr;
    std::uint32_t dialogEvent_ = 0;

    std::filesystem::path path_;       // the file the document came from or was last saved to
    std::optional<ImageFormat> format_; // its format, when the extension names one
    bool lockInPlace_ = false;         // Save must ask for a new name (the original would lose data)

    bool dialogOpen_ = false;
    std::string dialogLocation_; // kept alive for the native dialog
    std::string lastDirectory_;

    Modal modal_ = Modal::None;
    Modal requested_ = Modal::None; // opened at the start of the next drawModals()
    Followup followup_;             // waits behind the unsaved-changes prompt
    Followup afterSave_;            // waits behind a save
    SaveTarget pendingSave_;
    int jpegQuality_ = kDefaultJpegQuality;
    std::string message_;
    bool messageIsError_ = false;
    bool exitApproved_ = false;
    bool dropConsumed_ = false; // the first file of the current drop has been taken
};

} // namespace trace
