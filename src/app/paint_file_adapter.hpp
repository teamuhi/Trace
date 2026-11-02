#pragma once

#include "app/file_adapter.hpp"

namespace trace {

class Document;
struct Settings;

// The paint workspace's files: loaded as a flat image (PNG, JPEG, BMP, TGA, GIF) and saved as any picture format (also TIFF, ICO, QOI, PPM and SVG). This is the
// behaviour File > New / Open / Save had before workspaces existed, unchanged.
class PaintFileAdapter final : public FileAdapter {
public:
    PaintFileAdapter(Document& document, const Settings& settings) : document_(document), settings_(settings) {}

    bool isModified() const override;
    void commitPending() override;
    void markSaved() override;
    std::string newBlank() override;
    std::string newBlankVariant(int variant) override;
    LoadOutcome load(const std::filesystem::path& path) override;
    std::span<const FileFormat> saveFormats() const override;
    std::optional<int> formatForPath(const std::filesystem::path& path) const override;
    bool formatNeedsOptions(int format) const override;
    std::string save(const std::filesystem::path& path, int format, const SaveOptions& options) override;
    std::span<const FileFilter> openFilters() const override;

    // newBlankVariant(): an endless whiteboard.
    static constexpr int kNewWhiteboard = 1;
    // The paper of the endless whiteboard being edited (true: black); a new board takes the setting.
    bool boardDark() const noexcept { return boardDark_; }
    void setBoardDark(bool dark) noexcept { boardDark_ = dark; }

private:
    Document& document_;
    const Settings& settings_;
    bool boardDark_ = false;
};

// The image types every mode can open from the Open dialog.
std::span<const FileFilter> imageOpenFilters();

} // namespace trace
