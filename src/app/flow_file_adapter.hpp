#pragma once

#include <functional>
#include <utility>

#include "app/file_adapter.hpp"

namespace trace {

struct FlowState;

// The Flowchart workspace's files. The document is the FlowState's (document plus undo history). New starts an
// empty board. `.tflow` is the project format (io/flow_project.hpp); Open replaces the document, clears the
// selection and starts the undo history over, and says so when damaged items had to be skipped. The
// unsaved-changes guard follows the undo history. Save As also lists every picture and page format (PNG, JPEG, BMP,
// TGA, GIF, TIFF, ICO, QOI, PPM, SVG, PDF, Word) as export-only copies; the workspace draws those, through the
// exporter it installs.
class FlowFileAdapter final : public FileAdapter {
public:
    explicit FlowFileAdapter(FlowState& state) : state_(state) {}

    // Writes the chart in a picture or page format. Returns "" when the export was accepted (it may finish on a
    // later frame and then reports for itself) or a message fit to show the user.
    using Exporter = std::function<std::string(const std::filesystem::path&, ImageFormat, const SaveOptions&)>;
    void setExporter(Exporter exporter) { exporter_ = std::move(exporter); }

    bool isModified() const override;
    void commitPending() override;
    void markSaved() override;
    std::string newBlank() override;
    LoadOutcome load(const std::filesystem::path& path) override;
    std::span<const FileFormat> saveFormats() const override;
    std::optional<int> formatForPath(const std::filesystem::path& path) const override;
    bool formatNeedsOptions(int format) const override;
    bool savesLater(int format) const override;
    std::string save(const std::filesystem::path& path, int format, const SaveOptions& options) override;
    std::span<const FileFilter> openFilters() const override;

private:
    FlowState& state_;
    Exporter exporter_;
};

} // namespace trace
