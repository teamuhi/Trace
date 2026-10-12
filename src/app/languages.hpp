#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/i18n.hpp"

namespace trace {

// Finds, loads, imports and exports language packs. Packs are ini files (see core/i18n.hpp) in two folders:
// `languages` next to the program (shipped with it) and `languages` in the per-user data folder (imported
// by the user; wins over a shipped pack with the same code). English is built in and needs no file.
class LanguageManager {
public:
    struct Entry {
        LanguageInfo info;
        std::filesystem::path file;
    };

    // Either folder may be empty (not available). Scans them.
    void init(const std::string& userFolderUtf8, const std::string& bundledFolderUtf8);

    // Scans both folders again. Unreadable and invalid files are skipped.
    void refresh();

    const std::vector<Entry>& available() const noexcept { return available_; }

    // Switches the interface language: "" (or "en") is English. On failure English is used and the returned
    // message says why; empty on success.
    std::string apply(const std::string& code);

    // The pack in use, null for English.
    const LanguagePack* active() const noexcept { return hasActive_ ? &active_ : nullptr; }

    bool canImport() const noexcept { return !userFolder_.empty(); }

    // Validates a pack file and copies it to the user folder as <code>.ini (replacing a pack with that code).
    // Returns a message on failure, empty on success; `code` and `name` describe what was imported.
    std::string importFile(const std::string& pathUtf8, std::string& code, std::string& name);

    // Writes the translator template. Returns a message on failure, empty on success.
    std::string exportTemplate(const std::string& pathUtf8) const;

private:
    void scan(const std::filesystem::path& folder);

    std::filesystem::path userFolder_;
    std::filesystem::path bundledFolder_;
    std::vector<Entry> available_;
    LanguagePack active_;
    bool hasActive_ = false;
};

// Reads a whole file of at most `maxBytes`. Returns false (with `error` set to a user message) when it
// cannot be read or is larger.
bool readSmallFile(const std::filesystem::path& path, std::size_t maxBytes, std::string& text, std::string& error);

} // namespace trace
