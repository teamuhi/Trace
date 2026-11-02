#pragma once

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/image.hpp"

namespace trace {

class Document;

// Exporting an endless whiteboard as a picture. The board has no edge to export, so the user picks what part of it
// goes into the picture (the region), how far beyond that region the picture reaches (the padding, which may reach
// past what is allocated: that area is filled with the background) and how big the picture is.

enum class BoardRegion : unsigned char {
    Content,   // everything drawn, on any visible layer
    View,      // what the canvas shows now
    Board,     // the whole allocated board
    Selection, // the selected rectangle
    Custom,    // a rectangle typed in world coordinates
};

enum class BoardSizeMode : unsigned char {
    Scale,    // a percentage of the region's size
    Width,    // a width in pixels; the height follows
    Height,   // a height in pixels; the width follows
    LongEdge, // the longer side in pixels; the other follows
};

enum class BoardBackground : unsigned char {
    Paper,       // the board's own white or black
    White,
    Black,
    Transparent, // PNG keeps it; JPEG turns it white
};

enum class BoardExportFormat : unsigned char { Png, Jpeg };

struct BoardExportOptions {
    static constexpr int kMaxPad = 16384;
    static constexpr int kMinScalePct = 5;
    static constexpr int kMaxScalePct = 800;
    static constexpr int kMaxTarget = 16384;

    BoardRegion region = BoardRegion::Content;
    // World rectangle of BoardRegion::Custom: top-left corner and size.
    int customX = 0;
    int customY = 0;
    int customW = 1920;
    int customH = 1080;

    bool uniformPad = true;
    int padAll = 48;       // pixels of the region added on every side
    int padLeft = 48;      // used when !uniformPad
    int padTop = 48;
    int padRight = 48;
    int padBottom = 48;

    BoardSizeMode size = BoardSizeMode::Scale;
    int scalePct = 100;
    int target = 1920;     // pixels, for the three target modes
    bool smooth = true;    // bilinear when scaling; off keeps hard pixel edges

    BoardBackground background = BoardBackground::Paper;
    BoardExportFormat format = BoardExportFormat::Png;
};

// What an export would produce, worked out from the region (image pixels) and the options.
struct BoardExportPlan {
    enum class Problem : unsigned char { None, Empty, TooLarge };

    IRect source;       // region plus padding, in image pixels; may reach outside the image
    int width = 0;      // the finished picture
    int height = 0;
    Problem problem = Problem::Empty;

    bool ok() const noexcept { return problem == Problem::None; }
};

// Region + padding + size mode. Empty when the region has no area; TooLarge when the source or the result would
// exceed the picture size and memory limits.
BoardExportPlan planBoardExport(const IRect& region, const BoardExportOptions& options);

// The picture: the visible layers of the active frame over `background`, covering plan.source (area beyond the board
// is background), scaled to the plan's size. May throw std::bad_alloc. Pending edits must have been committed.
Image renderBoardExport(const Document& doc, const BoardExportPlan& plan, Rgba8 background, bool smooth);

} // namespace trace
