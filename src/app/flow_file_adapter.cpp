#include "app/flow_file_adapter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "app/flow_state.hpp"
#include "app/image_file_formats.hpp"
#include "core/i18n.hpp"
#include "io/flow_project.hpp"
#include "io/path_utf8.hpp"

namespace trace {

namespace {

// Projects first, then the images that open in Paint (App routes them there, so choosing one here is fine).
constexpr std::array<FileFilter, 2> kOpenFilters = {{
    {Str::FilterFlowProject, "tflow"},
    {Str::FilterImages, "png;jpg;jpeg;bmp;tga;gif"},
}};

} // namespace

bool FlowFileAdapter::isModified() const {
    return state_.history.isModified(state_.doc);
}

void FlowFileAdapter::commitPending() {
    state_.commit();
}

void FlowFileAdapter::markSaved() {
    state_.history.markSaved();
}

namespace {

// The words of the templates, in the current language.
flow::TemplateText templateText() {
    flow::TemplateText t;
    t.start = tr(Str::FlowTplStart);
    t.end = tr(Str::FlowTplEnd);
    t.step = tr(Str::FlowTplStep);
    t.decision = tr(Str::FlowTplDecision);
    t.yes = tr(Str::FlowTplYes);
    t.no = tr(Str::FlowTplNo);
    t.action = tr(Str::FlowTplAction);
    t.lane = tr(Str::FlowTplLane);
    return t;
}

} // namespace

std::string FlowFileAdapter::newBlank() {
    // A template asked for by File > New from Template is used once; a plain New is empty.
    flow::FlowDocument fresh;
    if (state_.pendingTemplate) {
        fresh = flow::makeTemplate(*state_.pendingTemplate, templateText());
        state_.pendingTemplate.reset();
    }
    state_.doc = std::move(fresh);
    state_.history.reset(state_.doc);
    state_.clearSelection();
    return {};
}

FileAdapter::LoadOutcome FlowFileAdapter::load(const std::filesystem::path& path) {
    LoadOutcome outcome;
    FlowProjectLoad loaded = loadFlowProject(path);
    if (!loaded.doc) {
        outcome.error = std::move(loaded.error); // the document is untouched
        return outcome;
    }
    // A whole new document: the tool stays as it is, the selection and the undo history start over.
    state_.doc = std::move(*loaded.doc);
    state_.history.reset(state_.doc);
    state_.clearSelection();
    if (loaded.repaired > 0)
        outcome.notice = trf(Str::FileFlowRepaired, {std::to_string(loaded.repaired)});
    return outcome;
}

std::span<const FileFormat> FlowFileAdapter::saveFormats() const {
    // The project first, then every picture and page format the chart can be exported as (index i > 0 is
    // ImageFormat(i - 1)).
    static const std::vector<FileFormat> formats =
        projectSaveFormats({Str::FilterFlowProject, "tflow", "tflow"}, true);
    return formats;
}

std::optional<int> FlowFileAdapter::formatForPath(const std::filesystem::path& path) const {
    return projectFormatForPath(path, "tflow", true);
}

bool FlowFileAdapter::formatNeedsOptions(int format) const {
    return format == 1 + static_cast<int>(ImageFormat::Jpeg);
}

bool FlowFileAdapter::savesLater(int format) const {
    return format > 0; // the chart is drawn by the workspace, which reports for itself
}

std::string FlowFileAdapter::save(const std::filesystem::path& path, int format, const SaveOptions& options) {
    if (format == 0)
        return saveFlowProject(path, state_.doc);
    if (format < 1 || static_cast<std::size_t>(format) > kAllFormats.size() || !exporter_)
        return trf(Str::IoCannotSave, {utf8FromPath(path.filename()), "?"});
    return exporter_(path, static_cast<ImageFormat>(format - 1), options);
}

std::span<const FileFilter> FlowFileAdapter::openFilters() const {
    return kOpenFilters;
}

} // namespace trace
