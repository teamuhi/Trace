#include "io/flow_project.hpp"

#include <exception>
#include <new>
#include <string>
#include <utility>

#include "core/i18n.hpp"
#include "flow/flow_serialize.hpp"
#include "io/path_utf8.hpp"

namespace trace {

namespace {

FlowProjectLoad failure(std::string message) {
    FlowProjectLoad result;
    result.error = std::move(message);
    return result;
}

} // namespace

ProjectData flowProjectData(const flow::FlowDocument& doc) {
    flow::FlowSerialized serialized = flow::serializeFlow(doc);
    ProjectData data;
    data.kind = kProjectFlow;
    data.meta = std::move(serialized.ini);
    data.images.reserve(serialized.images.size());
    for (const flow::ImagePtr& image : serialized.images)
        data.images.push_back(*image); // a copy: the project owns its pictures, the document keeps its own
    return data;
}

FlowProjectLoad flowProjectToDocument(ProjectData data, const std::string& name) {
    try {
        if (data.kind != kProjectFlow)
            return failure(trf(Str::IoFlowNotFlow, {name}));
        flow::FlowParse parsed = flow::parseFlow(data.meta, data.images);
        switch (parsed.status) {
        case flow::FlowParseStatus::Ok: break;
        case flow::FlowParseStatus::NotABoard: return failure(trf(Str::IoFlowUnusable, {name}));
        case flow::FlowParseStatus::TooNew: return failure(trf(Str::IoProjectTooNew, {name}));
        case flow::FlowParseStatus::Failed: return failure(trf(Str::IoMemoryOpenNamed, {name}));
        }
        FlowProjectLoad result;
        result.doc = std::move(parsed.doc);
        result.repaired = parsed.repaired;
        return result;
    } catch (const std::bad_alloc&) {
        return failure(trf(Str::IoMemoryOpenNamed, {name}));
    } catch (const std::exception& e) {
        return failure(trf(Str::IoCannotOpenNamed, {name, e.what()}));
    }
}

std::string saveFlowProject(const std::filesystem::path& path, const flow::FlowDocument& doc) {
    try {
        ProjectData data = flowProjectData(doc);
        // The reader keeps at most kMaxProjectPixels of pictures; a file that passed it would lose images.
        std::size_t pixels = 0;
        for (const Image& image : data.images) {
            const std::size_t count = static_cast<std::size_t>(image.width()) * static_cast<std::size_t>(image.height());
            if (count > kMaxProjectPixels - pixels)
                return tr(Str::IoProjectTooBigToSave);
            pixels += count;
        }
        return saveProject(path, data);
    } catch (const std::bad_alloc&) {
        return trf(Str::IoMemorySave, {utf8FromPath(path.filename())});
    } catch (const std::exception& e) {
        return trf(Str::IoCannotSave, {utf8FromPath(path.filename()), e.what()});
    }
}

FlowProjectLoad loadFlowProject(const std::filesystem::path& path) {
    ProjectReadResult read = loadProject(path);
    if (!read.project)
        return failure(std::move(read.error));
    FlowProjectLoad result = flowProjectToDocument(std::move(*read.project), utf8FromPath(path.filename()));
    if (result.doc)
        result.repaired += read.undecodableImages;
    return result;
}

} // namespace trace
