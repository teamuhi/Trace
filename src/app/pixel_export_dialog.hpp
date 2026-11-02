#pragma once

#include <optional>
#include <string>

#include "core/animation.hpp"

namespace trace {

class Document;

enum class PixelExportFormat : unsigned char { Png, Gif, Sheet };

// What the Export dialog collects. Frames are document frame indices (0 based, inclusive, first <= last).
struct PixelExportOptions {
    PixelExportFormat format = PixelExportFormat::Png;
    int scale = 1;                // 1..kMaxScale
    int firstFrame = 0;           // GIF and sprite sheet
    int lastFrame = 0;
    bool keepTransparency = true; // GIF: off makes every pixel opaque
    int alphaThreshold = 128;     // GIF: pixels with less opacity than this (1..255) become transparent
    SheetOptions sheet;           // sprite sheet layout and padding; its scale is `scale`

    static constexpr int kMaxScale = 32;
    static constexpr int kMaxPadding = 64;

    // The threshold exportGif takes: 0 keeps every pixel opaque.
    int gifAlpha() const noexcept { return keepTransparency ? alphaThreshold : 0; }
};

// A refusal or error message from src/io/pixel_export.* (English text) in the interface language; anything it
// does not know is returned as it is.
std::string localizedExportMessage(const std::string& message);

// File > Export in Pixel Art mode: a modal that asks for the format (PNG of the current frame, animated GIF, sprite
// sheet) and its options, shows the size of the result live, and refuses what cannot be written. It only collects the
// options; the workspace asks for the file name and writes the file. The options of the last use are kept for the
// session. While it is open busy() is true and the app ignores shortcuts.
class PixelExportDialog {
public:
    // Opens the dialog for `doc`: the range is the whole animation, and a document of one frame offers PNG.
    void open(const Document& doc);

    bool busy() const noexcept { return open_; }

    // Draws the dialog if it is open. Returns the options on the frame Export was pressed (the dialog closes).
    std::optional<PixelExportOptions> draw(const Document& doc);

private:
    PixelExportOptions options_;
    bool open_ = false;
    bool openRequested_ = false;
};

} // namespace trace
