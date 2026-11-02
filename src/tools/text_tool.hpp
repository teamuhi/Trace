#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "core/color.hpp"
#include "core/text_raster.hpp"
#include "tools/tool.hpp"

namespace trace {

// The Text tool. Unlike the others it is not one gesture: a click opens a text box at that pixel and typing goes on
// over many frames, so the tool keeps a session. The text is not in the image while it is typed; it is previewed
// from bitmap() and drawn into the image, as one undo step, by commit(). The window layer (CanvasView) feeds the
// typing in with setText() and decides when to commit or discard.
//
// A click while a session is open applies that text and opens a new box where the click landed.
class TextTool final : public Tool {
public:
    // Gives the TrueType bytes for a font family and style. The bytes only have to stay valid until the next call (the
    // tool draws with them at once). Without a resolver, or when it gives nothing, nothing is drawn.
    using FontResolver = std::function<std::span<const std::uint8_t>(std::string_view family, std::string_view style)>;
    void setFontResolver(FontResolver resolver);

    // A resolver over two fonts that must outlive the tool: "JetBrains Mono" is `mono`, every other family is `sans`.
    void setFonts(std::span<const std::uint8_t> sans, std::span<const std::uint8_t> mono);

    // --- Tool ----------------------------------------------------------------------------------------
    void press(Document& doc, ToolSettings& settings, const PointerEvent& ev) override;
    void drag(Document&, ToolSettings&, const PointerEvent&) override {}
    void release(Document&, ToolSettings&, const PointerEvent&) override {}
    void cancel(Document&) override { discard(); }
    void abort() noexcept override { discard(); }

    // --- session -------------------------------------------------------------------------------------
    bool editing() const noexcept { return editing_; }
    // Changes whenever a new box is opened, so a view can tell "the same box" from "a different one".
    std::uint64_t serial() const noexcept { return serial_; }
    // The pixel the text box's top-left corner sits on.
    int originX() const noexcept { return originX_; }
    int originY() const noexcept { return originY_; }
    const std::string& text() const noexcept { return text_; }
    // Replaces the typed text (sanitized and cut to kMaxTextBytes).
    void setText(std::string_view text);

    // The session belongs to one layer of one document; it is stale once either changed.
    bool stale(const Document& doc) const noexcept;

    // The text as it will be drawn with `settings`. Rendered again only when the text or a setting changed.
    const TextBitmap& bitmap(const ToolSettings& settings);
    // The pixels' color: the primary color, or the secondary one for a box opened with the right button.
    Rgba8 color(const ToolSettings& settings) const noexcept {
        return secondary_ ? settings.secondary : settings.primary;
    }

    // Draws the text into the active layer and ends the session. Returns the pixels that changed (empty for no text,
    // or text entirely outside the image); marks them dirty. The caller commits the document.
    IRect commit(Document& doc, const ToolSettings& settings);
    // Ends the session without drawing.
    void discard() noexcept;

private:
    FontResolver fonts_;

    bool editing_ = false;
    bool secondary_ = false;
    int originX_ = 0;
    int originY_ = 0;
    int layer_ = 0;
    std::uint64_t generation_ = 0;
    std::uint64_t serial_ = 0;
    std::string text_;

    // The last rendering and what it was made from.
    TextBitmap bitmap_;
    bool bitmapValid_ = false;
    int bitmapSize_ = 0;
    std::string bitmapFamily_;
    std::string bitmapStyle_;
    bool bitmapAntialias_ = true;
    TextAlign bitmapAlign_ = TextAlign::Left;
};

} // namespace trace
