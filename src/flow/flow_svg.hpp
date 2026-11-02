#pragma once

#include <optional>
#include <span>
#include <string>

#include "flow/flow_document.hpp"

namespace trace::flow {

// What an SVG export needs to know that the document leaves open. Auto colors (nullopt in a Style) are resolved
// here, so the same chart can be exported for a white page or a dark one.
struct SvgOptions {
    std::optional<Rgba8> background;               // nullopt = transparent
    Rgba8 inkAuto{32, 33, 36, 255};                // stroke and text of a shape or connector whose color is Auto
    Rgba8 paperAuto{255, 255, 255, 255};           // fill of a shape whose color is Auto
    Rgba8 stickyFill{255, 236, 140, 255};          // fill of a sticky note whose color is Auto
    float padding = 24.0f;                         // around the content, in board units
    std::string fontFamily = "sans-serif";         // a font name given by the user is placed before this fallback
    std::span<const NodeId> only;                  // export just these shapes and the connectors between them (empty = all)
};

// The document as a standalone SVG 1.1 text (UTF-8): shapes as <path>, connectors as <path> with arrowheads
// drawn as paths (not <marker>, so they match the board exactly), text as <text> / <tspan> (wrapped to the shape with an estimated character width, since the pure
// library has no font metrics), images as base64 PNG data URIs, labels turned and placed as on the board. Every
// piece of user text and every attribute value is XML-escaped. An empty selection or an empty document gives a
// small valid SVG with no content. Pure: no SDL, no ImGui.
std::string exportSvg(const FlowDocument& doc, const SvgOptions& options = {});

} // namespace trace::flow
