#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "core/i18n.hpp"
#include "io/image_io.hpp"

namespace trace {

// One entry of the Save As dialog's file type list.
struct FileFormat {
    Str name;              // translated filter name, for example "PNG image"
    const char* patterns;  // extensions the dialog filter accepts, "jpg;jpeg"
    const char* extension; // appended to a name typed without one, "jpg"
};

// One entry of the Open dialog's file type list (a catch-all "All files" entry is always added last).
struct FileFilter {
    Str name;
    const char* patterns;
};

// What FileSession needs to know about the kind of document a workspace edits: how to start, load and save
// it, and which file types it uses. FileSession owns the flow (dialogs, the unsaved-changes prompt, the
// current path); the adapter owns the document and the format details. Formats are identified by their
// index in saveFormats().
class FileAdapter {
public:
    virtual ~FileAdapter() = default;

    // False for a workspace with nothing to save (the placeholder modes): New, Save and Save As do nothing.
    virtual bool hasDocument() const { return true; }

    virtual bool isModified() const = 0;
    // Turns pending edits into history, so isModified() reflects everything the user has done.
    virtual void commitPending() {}
    virtual void markSaved() = 0;

    // Starts from an empty document. Returns "" or a message fit to show the user; the document is untouched
    // on failure.
    virtual std::string newBlank() = 0;

    struct LoadOutcome {
        std::string error;        // set on failure; the document is untouched
        bool lockInPlace = false; // Save must ask for a new name (the original would lose data)
        std::string notice;       // information to show after a successful load
    };
    virtual LoadOutcome load(const std::filesystem::path& path) = 0;

    virtual std::span<const FileFormat> saveFormats() const = 0;
    // The format named by the file's extension, if it is one of saveFormats().
    virtual std::optional<int> formatForPath(const std::filesystem::path& path) const = 0;
    // Whether saving in `format` asks for options first (the JPEG quality).
    virtual bool formatNeedsOptions(int /*format*/) const { return false; }
    // Writes the document. Returns "" or a message fit to show the user.
    virtual std::string save(const std::filesystem::path& path, int format, const SaveOptions& options) = 0;

    virtual std::span<const FileFilter> openFilters() const = 0;
};

} // namespace trace
