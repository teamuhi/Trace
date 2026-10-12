#include "app/languages.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

#include "io/path_utf8.hpp"

namespace fs = std::filesystem;

namespace trace {

namespace {

constexpr std::size_t kMaxPacksPerFolder = 64;

bool isIni(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".ini";
}

} // namespace

bool readSmallFile(const fs::path& path, std::size_t maxBytes, std::string& text, std::string& error) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        error = tr(Str::ErrLanguageRead);
        return false;
    }
    if (size > maxBytes) {
        error = "This file is too large to be a language pack.";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = tr(Str::ErrLanguageRead);
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

void LanguageManager::init(const std::string& userFolderUtf8, const std::string& bundledFolderUtf8) {
    userFolder_ = userFolderUtf8.empty() ? fs::path() : pathFromUtf8(userFolderUtf8);
    bundledFolder_ = bundledFolderUtf8.empty() ? fs::path() : pathFromUtf8(bundledFolderUtf8);
    refresh();
}

void LanguageManager::scan(const fs::path& folder) {
    if (folder.empty())
        return;
    std::error_code ec;
    fs::directory_iterator it(folder, ec);
    if (ec)
        return;
    std::size_t seen = 0;
    for (const fs::directory_entry& entry : it) {
        if (seen >= kMaxPacksPerFolder)
            break;
        std::error_code statusError;
        if (!entry.is_regular_file(statusError) || !isIni(entry.path()))
            continue;
        ++seen;

        std::string text;
        std::string error;
        LanguagePack pack;
        if (!readSmallFile(entry.path(), kMaxLanguageFileBytes, text, error) || !parseLanguagePack(text, pack).empty())
            continue;

        // A later folder (the user's) replaces an earlier one with the same code.
        const auto same = std::find_if(available_.begin(), available_.end(),
                                       [&](const Entry& e) { return e.info.code == pack.info.code; });
        if (same != available_.end())
            *same = Entry{pack.info, entry.path()};
        else
            available_.push_back(Entry{pack.info, entry.path()});
    }
}

void LanguageManager::refresh() {
    available_.clear();
    scan(bundledFolder_);
    scan(userFolder_);
    std::sort(available_.begin(), available_.end(),
              [](const Entry& a, const Entry& b) { return a.info.name < b.info.name; });
}

std::string LanguageManager::apply(const std::string& code) {
    if (code.empty() || code == "en") {
        setLanguage(nullptr);
        hasActive_ = false;
        active_ = LanguagePack{};
        return {};
    }
    const auto found = std::find_if(available_.begin(), available_.end(), [&](const Entry& e) { return e.info.code == code; });
    std::string error;
    if (found != available_.end()) {
        std::string text;
        LanguagePack pack;
        if (readSmallFile(found->file, kMaxLanguageFileBytes, text, error) && (error = parseLanguagePack(text, pack)).empty()) {
            active_ = std::move(pack);
            hasActive_ = true;
            setLanguage(&active_);
            return {};
        }
    }
    setLanguage(nullptr);
    hasActive_ = false;
    active_ = LanguagePack{};
    return trf(Str::ErrLanguageLoad, {code});
}

std::string LanguageManager::importFile(const std::string& pathUtf8, std::string& code, std::string& name) {
    if (userFolder_.empty())
        return tr(Str::ErrLanguageFolder);

    std::string text;
    std::string error;
    if (!readSmallFile(pathFromUtf8(pathUtf8), kMaxLanguageFileBytes, text, error))
        return error;
    LanguagePack pack;
    error = parseLanguagePack(text, pack);
    if (!error.empty())
        return error;

    std::error_code ec;
    fs::create_directories(userFolder_, ec);
    if (ec)
        return tr(Str::ErrLanguageFolder);

    // Write through a temporary file so a failed copy never leaves a half-written pack behind.
    const fs::path target = userFolder_ / (pack.info.code + ".ini");
    fs::path temp = target;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << text;
        out.flush();
        if (!out) {
            std::error_code ignored;
            fs::remove(temp, ignored);
            return tr(Str::ErrLanguageWrite);
        }
    }
    fs::rename(temp, target, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return tr(Str::ErrLanguageWrite);
    }
    code = pack.info.code;
    name = pack.info.name;
    refresh();
    return {};
}

std::string LanguageManager::exportTemplate(const std::string& pathUtf8) const {
    std::ofstream out(pathFromUtf8(pathUtf8), std::ios::binary | std::ios::trunc);
    out << exportLanguageTemplate();
    out.flush();
    return out ? std::string() : std::string(tr(Str::ErrLanguageWrite));
}

} // namespace trace
