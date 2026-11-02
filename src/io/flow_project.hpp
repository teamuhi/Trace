#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "flow/flow_document.hpp"
#include "io/project_io.hpp"

namespace trace {

// The Flowchart project (.tflow): a FlowDocument written into the project container (kind kProjectFlow). META is
// the INI text of flow/flow_serialize.hpp and every picture is one PNG chunk, in the order the INI names them,
// stored at its original size. Reading never throws and never crashes on hostile input; what cannot be read is
// skipped and counted (see flow_serialize.hpp for the rules).

struct FlowProjectLoad {
    std::optional<flow::FlowDocument> doc; // empty on failure
    std::string error;                     // a message fit to show the user when `doc` is empty
    int repaired = 0;                      // damaged items skipped or repaired, undecodable pictures included
};

// The project for `doc`. May throw std::bad_alloc (callers use saveFlowProject, which does not).
ProjectData flowProjectData(const flow::FlowDocument& doc);

// Turns a decoded project into a document. `name` is how the file is called in messages.
FlowProjectLoad flowProjectToDocument(ProjectData data, const std::string& name);

// Writes the document to `path` atomically. Returns "" or a message fit to show the user; does not throw.
std::string saveFlowProject(const std::filesystem::path& path, const flow::FlowDocument& doc);

// Reads a .tflow file. A file of another kind (a Pixel Art project) is refused with a clear message.
FlowProjectLoad loadFlowProject(const std::filesystem::path& path);

} // namespace trace
