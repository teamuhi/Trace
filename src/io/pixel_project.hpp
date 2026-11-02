#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/cel_grid.hpp"
#include "core/palette.hpp"
#include "io/project_io.hpp"

namespace trace {

class Document;

// The Pixel Art project (.tpix): a Document written into the project container (kind kProjectPixel).
//
// META is INI text:
//
//   [doc]      width, height (1..kMaxPixelCanvasSide), layers (1..kMaxLayers), frames (1..kMaxFrames),
//              active_layer, active_frame
//   [layer.N]  N = 0 for the bottom layer: name, visible, opacity (0..255), locked
//   [frame.N]  duration_ms
//   [cel]      "<layer>,<frame>" = <index of the PNG chunk holding that cel>
//
//   [palette]  optional, the project's color palette: name (cleaned, may be absent), count (0..kMaxPaletteColors)
//              and c0, c1, ... = RRGGBB (hex, opaque; a leading '#' is tolerated when reading)
//
// One PNG chunk is written per cel that has any visible pixel; a cel with no entry under [cel] is fully
// transparent, so an empty layer or frame costs nothing in the file (and a project can pass the container's
// 65536-chunk limit only with real content). Frames are stored even though the Timeline arrives later.
//
// Reading is safe on hostile input: counts and sizes are validated against the limits, a cel that is missing,
// undecodable or the wrong size becomes transparent (and is counted), unknown keys and sections are ignored,
// and nothing here throws. A [palette] is read leniently: at most kMaxPaletteColors colors, malformed colors
// skipped, the name cleaned. A section with no usable color is a valid empty palette only when it says
// count=0; otherwise it counts as damaged and is ignored (the project still loads, without a palette).

struct PixelProjectLoad {
    std::optional<CelStore> store; // empty on failure
    std::string error;             // a message fit to show the user when `store` is empty
    int damagedCels = 0;           // cels the file listed but could not supply; they were replaced by transparent ones
    std::optional<Palette> palette; // the file's [palette]; nullopt when it has none (or only a damaged one)
};

// The project for `doc`: exactly what is on screen and in history's baseline, so a floating selection's pixels
// are included. A non-null `palette` is stored as [palette] (colors beyond kMaxPaletteColors are dropped). May
// throw std::bad_alloc (callers use savePixelProject, which does not).
ProjectData pixelProjectData(const Document& doc, const Palette* palette = nullptr);

// Turns a decoded project into a cel grid ready for Document::loadStore. `name` is how the file is called in
// messages. Ids are assigned fresh (layers 1.., cels 1..).
// `kind` is the project kind the data must have and `maxSide` the largest canvas side accepted (whiteboards use
// the same layout as Pixel Art projects but go up to the picture size limit).
PixelProjectLoad pixelProjectToCels(ProjectData data, const std::string& name, std::uint32_t kind = kProjectPixel,
                                    int maxSide = kMaxPixelCanvasSide);

// Writes the document to `path` atomically. Returns "" or a message fit to show the user; does not throw.
std::string savePixelProject(const std::filesystem::path& path, const Document& doc, const Palette* palette = nullptr);

// Reads a .tpix file. Does not throw.
PixelProjectLoad loadPixelProject(const std::filesystem::path& path);

} // namespace trace
