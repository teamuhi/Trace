#pragma once

#include "app/file_adapter.hpp"

namespace trace {

class Document;
struct Settings;

// The paint workspace's files: any of the five image formats, loaded and saved as a flat image. This is the
// behaviour File > New / Open / Save had before workspaces existed, unchanged.
class PaintFileAdapter final : public FileAdapter {
public:
    PaintFileAdapter(Document& document, const Settings& settings) : document_(document), settings_(settings) {}

    bool isModified() const override;
    void commitPending() override;
    void markSaved() override;
    std::string newBlank() override;
    LoadOutcome load(const std::filesystem::path& path) override;
    std::span<const FileFormat> saveFormats() const override;
    std::optional<int> formatForPath(const std::filesystem::path& path) const override;
    bool formatNeedsOptions(int format) const override;
    std::string save(const std::filesystem::path& path, int format, const SaveOptions& options) override;
    std::span<const FileFilter> openFilters() const override;

private:
    Document& document_;
    const Settings& settings_;
};

// The image types every mode can open from the Open dialog.
std::span<const FileFilter> imageOpenFilters();

} // namespace trace
