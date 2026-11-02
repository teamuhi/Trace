#pragma once

#include "app/file_adapter.hpp"

namespace trace {

class Document;
struct PaletteState;
struct Settings;

// The Pixel Art workspace's files: a `.tpix` project holding every layer and frame (see io/pixel_project.hpp).
// New starts a transparent canvas of the size from the Pixel Art settings, kept within 1..1024 per side. Save
// writes the project; Save As also offers the picture formats (PNG, JPEG, BMP, TGA, GIF, TIFF, ICO, QOI, PPM, SVG)
// as export-only copies of the shown frame (every frame for GIF), leaving the project as it was. Opening a project larger than the pixel
// canvas limit is refused with a message.
//
// The palette (owned by the workspace) is part of the project: New resets it to the default palette, Open restores
// the file's palette (the default one when the file has none), Save writes it, and a palette change counts as a
// modification for the unsaved-changes guard.
class PixelFileAdapter final : public FileAdapter {
public:
    PixelFileAdapter(Document& document, const Settings& settings, PaletteState& palette)
        : document_(document), settings_(settings), palette_(palette) {}

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

    // A fresh single-layer, single-frame, fully transparent document of this size, with the layer named in
    // the current language. Exposed for tests of the callers; returns false when memory runs out.
    static bool makeBlank(Document& document, int width, int height);

private:
    Document& document_;
    const Settings& settings_;
    PaletteState& palette_;
};

} // namespace trace
