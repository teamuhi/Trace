#pragma once

#include <optional>

#include "core/board_export.hpp"
#include "core/geometry.hpp"

namespace trace {

class Document;

// What the dialog hands back when Export is pressed: the options and the plan worked out from them (the area in image
// pixels, plus padding, and the size of the picture).
struct BoardExportRun {
    BoardExportOptions options;
    BoardExportPlan plan;
};

// File > Export for an endless whiteboard: a modal that asks which part of the board to export, how far beyond it the
// borders go (the same on every side or each side on its own, even past what is drawn), the size (a percentage, or a
// width, height or longest side in pixels), the background and the file type, and shows the size of the result live.
// It only collects the options; the workspace asks for the file name and writes the picture. The options of the last
// use are kept for the session. While it is open busy() is true and the app ignores shortcuts.
class BoardExportDialog {
public:
    // `view` is what the canvas shows now, in image pixels of `doc`.
    void open(const Document& doc, const IRect& view);

    bool busy() const noexcept { return open_; }

    std::optional<BoardExportRun> draw(const Document& doc);

private:
    // The region of the current options in image pixels (empty when it has none).
    IRect region(const Document& doc) const;

    BoardExportOptions options_;
    IRect content_;   // everything drawn on a visible layer
    IRect view_;
    IRect selection_;
    bool open_ = false;
    bool openRequested_ = false;
};

} // namespace trace
