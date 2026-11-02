#include "tools/text_tool.hpp"

#include <algorithm>
#include <utility>

#include "tools/tool_factories.hpp"
#include "tools/tool_util.hpp"

namespace trace {

namespace {

int clampedSize(const ToolSettings& s) noexcept {
    return std::clamp(s.textSize, ToolSettings::kMinTextSize, ToolSettings::kMaxTextSize);
}

} // namespace

void TextTool::setFontResolver(FontResolver resolver) {
    fonts_ = std::move(resolver);
    bitmapValid_ = false;
}

void TextTool::setFonts(std::span<const std::uint8_t> sans, std::span<const std::uint8_t> mono) {
    setFontResolver([sans, mono](std::string_view family, std::string_view) {
        const auto& wanted = family == "JetBrains Mono" ? mono : sans;
        return wanted.empty() ? (sans.empty() ? mono : sans) : wanted;
    });
}

void TextTool::press(Document& doc, ToolSettings& settings, const PointerEvent& ev) {
    // A click away from an open box applies it first (unless it can no longer be drawn anywhere).
    if (editing_) {
        if (stale(doc))
            discard();
        else
            commit(doc, settings);
    }
    if (doc.image().empty())
        return;

    const Vec2f p = tools::sanitize(ev.pos);
    editing_ = true;
    secondary_ = ev.secondary;
    originX_ = tools::toPixel(p.x);
    originY_ = tools::toPixel(p.y);
    layer_ = doc.activeLayer();
    generation_ = doc.generation();
    ++serial_;
    text_.clear();
    bitmapValid_ = false;
}

void TextTool::setText(std::string_view text) {
    std::string clean = sanitizeText(text);
    if (clean == text_)
        return;
    text_ = std::move(clean);
    bitmapValid_ = false;
}

bool TextTool::stale(const Document& doc) const noexcept {
    return !editing_ || doc.generation() != generation_ || doc.activeLayer() != layer_;
}

const TextBitmap& TextTool::bitmap(const ToolSettings& settings) {
    const int size = clampedSize(settings);
    if (!bitmapValid_ || bitmapSize_ != size || bitmapFamily_ != settings.textFontFamily ||
        bitmapStyle_ != settings.textFontStyle || bitmapAntialias_ != settings.antialias || bitmapAlign_ != settings.textAlign) {
        const std::span<const std::uint8_t> font =
            fonts_ ? fonts_(settings.textFontFamily, settings.textFontStyle) : std::span<const std::uint8_t>();
        rasterizeText(font, text_, size, settings.antialias, bitmap_, settings.textAlign);
        bitmapValid_ = true;
        bitmapSize_ = size;
        bitmapFamily_ = settings.textFontFamily;
        bitmapStyle_ = settings.textFontStyle;
        bitmapAntialias_ = settings.antialias;
        bitmapAlign_ = settings.textAlign;
    }
    return bitmap_;
}

IRect TextTool::commit(Document& doc, const ToolSettings& settings) {
    if (!editing_)
        return IRect{};
    const TextBitmap& rendered = bitmap(settings);
    IRect touched;
    if (!rendered.empty() && !doc.image().empty()) {
        touched = compositeText(doc.image(), rendered, originX_, originY_, color(settings));
        if (!touched.empty())
            doc.markDirty(touched);
    }
    discard();
    return touched;
}

void TextTool::discard() noexcept {
    editing_ = false;
    text_.clear();
    bitmap_ = TextBitmap{};
    bitmapValid_ = false;
}

std::unique_ptr<Tool> createTextTool() {
    return std::make_unique<TextTool>();
}

} // namespace trace
