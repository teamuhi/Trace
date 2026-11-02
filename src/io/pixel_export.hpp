#pragma once

#include <filesystem>
#include <string>

#include "core/animation.hpp"
#include "io/image_io.hpp"

namespace trace {

class Document;

// Exporting a Pixel Art document: one frame as PNG, a range of frames as an animated GIF, or as a sprite sheet PNG.
// A frame is the composite of its visible layers. Every function returns "" on success or a message fit to show the
// user, writes atomically, never throws, and refuses a result over kMaxExportSide pixels on a side or an out-of-range
// frame range with a message. `firstFrame` and `lastFrame` are inclusive document frame indices (swapped if reversed,
// clamped into the document).

struct ExportSize {
    bool ok = false;
    std::string error;
    int width = 0;  // the output picture (one GIF frame, or the whole sheet)
    int height = 0;
    int frames = 0; // how many frames the export will contain
};

// What an export would produce, so a dialog can show the size and refuse before writing anything.
ExportSize pngExportSize(const Document& doc, int scale);
ExportSize gifExportSize(const Document& doc, int firstFrame, int lastFrame, int scale);
ExportSize sheetExportSize(const Document& doc, int firstFrame, int lastFrame, const SheetOptions& sheet);

// PNG of frame `frame` at an integer scale (1..32).
std::string exportPng(const std::filesystem::path& path, const Document& doc, int frame, int scale);

// Frame `frame` at an integer scale (1..32) in any picture format; `options` carry the JPEG quality. SVG is drawn as
// one rectangle per pixel run.
std::string exportFrame(const std::filesystem::path& path, const Document& doc, int frame, int scale, ImageFormat format,
                        const SaveOptions& options = {});

// What Save As writes for a picture format: every frame as an animated GIF for GIF, otherwise the shown frame at 1:1.
std::string exportAs(const std::filesystem::path& path, const Document& doc, ImageFormat format, const SaveOptions& options);

// Animated GIF of frames firstFrame..lastFrame at an integer scale (1..32). Each frame's delay is its duration in
// the document (milliseconds rounded to hundredths of a second, at least kMinGifDelayCs). Pixels with alpha below
// `alphaThreshold` (1..255) are transparent; 0 keeps everything opaque.
std::string exportGif(const std::filesystem::path& path, const Document& doc, int firstFrame, int lastFrame, int scale,
                      int alphaThreshold = 128);

// Sprite sheet PNG of frames firstFrame..lastFrame (the scale is in `sheet.scale`).
std::string exportSpriteSheet(const std::filesystem::path& path, const Document& doc, int firstFrame, int lastFrame,
                              const SheetOptions& sheet);

} // namespace trace
