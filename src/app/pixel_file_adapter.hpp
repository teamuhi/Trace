#pragma once

#include "app/file_adapter.hpp"

namespace trace {

class Document;
struct Settings;

// The Pixel Art workspace's files: a `.tpix` project holding every layer and frame (see io/pixel_project.hpp).
// New starts a transparent canvas of the size from the Pixel Art settings, kept within 1..1024 per side. Save
// and Save As only write the project format; Export is separate. Opening a project larger than the pixel
// canvas limit is refused with a message.
class PixelFileAdapter final : public FileAdapter {
public:
    PixelFileAdapter(Document& document, const Settings& settings) : document_(document), settings_(settings) {}

    bool isModified() const override;
    void commitPending() override;
    void markSaved() override;
    std::string newBlank() override;
    LoadOutcome load(const std::filesystem::path& path) override;
    std::span<const FileFormat> saveFormats() const override;
    std::optional<int> formatForPath(const std::filesystem::path& path) const override;
    std::string save(const std::filesystem::path& path, int format, const SaveOptions& options) override;
    std::span<const FileFilter> openFilters() const override;

    // A fresh single-layer, single-frame, fully transparent document of this size, with the layer named in
    // the current language. Exposed for tests of the callers; returns false when memory runs out.
    static bool makeBlank(Document& document, int width, int height);

private:
    Document& document_;
    const Settings& settings_;
};

} // namespace trace
