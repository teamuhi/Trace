#pragma once

#include <span>
#include <vector>

#include "core/image.hpp"
#include "core/ini.hpp"
#include "flow/flow_document.hpp"

namespace trace::flow {

// A FlowDocument as the META text of a `.tflow` project plus its pictures (pure data, no files; the project
// reader and writer are in io/flow_project.hpp).
//
//   [board]        version = 1
//   [node.N]       kind, x, y, w, h, text, fill, stroke, text_color, stroke_width, dash, corner_radius, font_size,
//                  align, image, parent, group, sides, z (and valign when it is not middle)
//   [edge.N]       from_node, from_face, from_x, from_y, to_node, to_face, to_x, to_y, route, arrow_start,
//                  arrow_end, label, label_pos, label_side, label_along, stroke, width, dash, text_color, font_size
//                  (and via_x, via_y when the route was shaped by hand, fill, corner_radius, align when they differ
//                  from the defaults)
//   [image.N]      chunk = <index of the PNG chunk holding the picture>
//
// Enums are written by name ("rectangle", "elbow", "dashed", ...) so reordering them in code never changes what
// an old file means. Colors are RRGGBBAA hex; a missing color key means Auto. Text and labels escape `\`, line
// breaks, tabs, control characters and a leading or trailing space (`\\`, `\n`, `\r`, `\t`, `\xHH`, `\s`) so a
// value is always one line of INI. Ids in the file are canonical, not the document's own: nodes are numbered
// 1.. in paint order (so `z` is the same number), edges 1.. in document order, groups 1.. by first member, and
// pictures 0.. in order of first use. Writing is therefore deterministic, and writing what was read back gives
// the same text. Pictures that no node uses are not written.
//
// Reading is safe on hostile input and never throws: unknown kinds are skipped, duplicate ids keep the first,
// edges to missing nodes are dropped, parent cycles and non-container parents are cut, groups of fewer than two
// members dissolve, a bad picture index leaves the node with no picture, numbers are clamped (NaN and infinity
// included), strings are cut to kMaxTextBytes and counts stop at kMaxNodes, kMaxEdges and kMaxImages. Whatever
// had to be skipped or repaired is counted.

inline constexpr int kFlowFormatVersion = 1;

struct FlowSerialized {
    IniFile ini;
    // Picture N of the file is images[N]: the PNG chunks of the project, in this order.
    std::vector<ImagePtr> images;
};

FlowSerialized serializeFlow(const FlowDocument& doc);

enum class FlowParseStatus {
    Ok,
    NotABoard, // no [board] section
    TooNew,    // [board] version is newer than this program reads
    Failed,    // ran out of memory
};

struct FlowParse {
    FlowParseStatus status = FlowParseStatus::Ok;
    FlowDocument doc;  // empty unless status is Ok
    int repaired = 0;  // items skipped or repaired (a picture that could not be decoded is not counted here)
};

// `images` are the decoded PNG chunks in order (an empty Image is a chunk that could not be decoded). They are
// moved from, so each can back one document picture (several nodes may share it).
FlowParse parseFlow(const IniFile& ini, std::span<Image> images);

} // namespace trace::flow
