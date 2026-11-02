#include "io/pixel_export.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <new>
#include <utility>
#include <vector>

#include "core/document.hpp"
#include "io/image_io.hpp"

namespace trace {

namespace {

constexpr int kMaxScale = 32;
// All frames of a GIF are held in memory at once (and the encoder works on a copy), so they get a budget of their own.
constexpr long long kMaxGifPixels = 128LL * 1024 * 1024;

struct FrameRange {
    int first = 0;
    int last = 0;
    int count() const noexcept { return last - first + 1; }
};

ExportSize refuse(std::string message) {
    ExportSize size;
    size.error = std::move(message);
    return size;
}

std::string sizeMessage(long long width, long long height) {
    return "The result would be " + std::to_string(width) + " x " + std::to_string(height) +
           " pixels, over the limit of " + std::to_string(kMaxExportSide) +
           " pixels on a side. Use a smaller scale.";
}

constexpr const char* kNothing = "There is nothing to export.";
constexpr const char* kNoMemory = "There is not enough memory to export this.";

bool scaleOk(int scale) noexcept {
    return scale >= 1 && scale <= kMaxScale;
}

// The document's frame size, or 0 x 0 when it has no pictures.
std::pair<int, int> frameSize(const Document& doc) noexcept {
    if (doc.frameCount() < 1 || doc.layerCount() < 1)
        return {0, 0};
    const Image& cel = doc.cel(0, 0);
    return {cel.width(), cel.height()};
}

// A reversed range is swapped and one that sticks out is pulled into the document. Empty when there are no frames.
bool normalisedRange(const Document& doc, int firstFrame, int lastFrame, FrameRange& range) noexcept {
    if (doc.frameCount() < 1)
        return false;
    if (firstFrame > lastFrame)
        std::swap(firstFrame, lastFrame);
    range.first = std::clamp(firstFrame, 0, doc.frameCount() - 1);
    range.last = std::clamp(lastFrame, 0, doc.frameCount() - 1);
    return true;
}

// The composite of every frame in `range`, or fewer when memory runs out (the caller compares).
std::vector<Image> compositeRange(const Document& doc, const FrameRange& range) {
    std::vector<Image> frames;
    frames.reserve(static_cast<std::size_t>(range.count()));
    for (int f = range.first; f <= range.last; ++f) {
        Image composite = doc.compositeOfFrame(f);
        if (composite.empty())
            return {};
        frames.push_back(std::move(composite));
    }
    return frames;
}

ExportSize gifSize(const Document& doc, int firstFrame, int lastFrame, int scale, FrameRange& range) {
    const auto [w, h] = frameSize(doc);
    if (w < 1 || h < 1 || !normalisedRange(doc, firstFrame, lastFrame, range))
        return refuse(kNothing);
    if (!scaleOk(scale))
        return refuse("The scale must be between 1 and " + std::to_string(kMaxScale) + ".");
    const long long width = static_cast<long long>(w) * scale;
    const long long height = static_cast<long long>(h) * scale;
    if (width > kMaxExportSide || height > kMaxExportSide)
        return refuse(sizeMessage(width, height));
    if (static_cast<std::size_t>(range.count()) > kMaxGifFrames)
        return refuse("An animated GIF can hold at most " + std::to_string(kMaxGifFrames) + " frames.");
    if (width * height * range.count() > kMaxGifPixels)
        return refuse("This animation is too large to export as a GIF. Use a smaller scale or fewer frames.");
    ExportSize size;
    size.ok = true;
    size.width = static_cast<int>(width);
    size.height = static_cast<int>(height);
    size.frames = range.count();
    return size;
}

ExportSize sheetSize(const Document& doc, int firstFrame, int lastFrame, const SheetOptions& sheet, FrameRange& range,
                     SheetLayout& layout) {
    const auto [w, h] = frameSize(doc);
    if (w < 1 || h < 1 || !normalisedRange(doc, firstFrame, lastFrame, range))
        return refuse(kNothing);
    layout = sheetLayout(range.count(), w, h, sheet);
    if (!layout.ok)
        return refuse(layout.error);
    ExportSize size;
    size.ok = true;
    size.width = layout.width;
    size.height = layout.height;
    size.frames = range.count();
    return size;
}

} // namespace

ExportSize pngExportSize(const Document& doc, int scale) {
    const auto [w, h] = frameSize(doc);
    if (w < 1 || h < 1)
        return refuse(kNothing);
    if (!scaleOk(scale))
        return refuse("The scale must be between 1 and " + std::to_string(kMaxScale) + ".");
    const long long width = static_cast<long long>(w) * scale;
    const long long height = static_cast<long long>(h) * scale;
    if (width > kMaxExportSide || height > kMaxExportSide)
        return refuse(sizeMessage(width, height));
    ExportSize size;
    size.ok = true;
    size.width = static_cast<int>(width);
    size.height = static_cast<int>(height);
    size.frames = 1;
    return size;
}

ExportSize gifExportSize(const Document& doc, int firstFrame, int lastFrame, int scale) {
    FrameRange range;
    return gifSize(doc, firstFrame, lastFrame, scale, range);
}

ExportSize sheetExportSize(const Document& doc, int firstFrame, int lastFrame, const SheetOptions& sheet) {
    FrameRange range;
    SheetLayout layout;
    return sheetSize(doc, firstFrame, lastFrame, sheet, range, layout);
}

std::string exportFrame(const std::filesystem::path& path, const Document& doc, int frame, int scale, ImageFormat format,
                        const SaveOptions& options) {
    try {
        const ExportSize size = pngExportSize(doc, scale);
        if (!size.ok)
            return size.error;
        if (frame < 0 || frame >= doc.frameCount())
            return "That frame does not exist.";
        const Image scaled = scaledNearest(doc.compositeOfFrame(frame), scale);
        if (scaled.empty())
            return kNoMemory;
        SaveOptions pixel = options;
        pixel.pixelArt = true;
        return saveImage(path, scaled, format, pixel);
    } catch (const std::bad_alloc&) {
        return kNoMemory;
    } catch (const std::exception& e) {
        return std::string("The export failed: ") + e.what();
    } catch (...) {
        return "The export failed.";
    }
}

std::string exportPng(const std::filesystem::path& path, const Document& doc, int frame, int scale) {
    return exportFrame(path, doc, frame, scale, ImageFormat::Png);
}

std::string exportAs(const std::filesystem::path& path, const Document& doc, ImageFormat format, const SaveOptions& options) {
    if (format == ImageFormat::Gif)
        return exportGif(path, doc, 0, doc.frameCount() - 1, 1);
    return exportFrame(path, doc, doc.activeFrame(), 1, format, options);
}

std::string exportGif(const std::filesystem::path& path, const Document& doc, int firstFrame, int lastFrame, int scale,
                      int alphaThreshold) {
    try {
        FrameRange range;
        const ExportSize size = gifSize(doc, firstFrame, lastFrame, scale, range); // before anything big is made
        if (!size.ok)
            return size.error;

        std::vector<Image> frames;
        std::vector<int> delays;
        frames.reserve(static_cast<std::size_t>(range.count()));
        delays.reserve(static_cast<std::size_t>(range.count()));
        for (int f = range.first; f <= range.last; ++f) {
            Image scaled = scaledNearest(doc.compositeOfFrame(f), scale); // one full-size picture at a time
            if (scaled.empty())
                return kNoMemory;
            frames.push_back(std::move(scaled));
            const int durationMs = doc.frame(f).durationMs;
            delays.push_back(std::max(kMinGifDelayCs, (durationMs + 5) / 10)); // rounded to hundredths of a second
        }
        return saveAnimatedGif(path, frames, delays, std::clamp(alphaThreshold, 0, 255));
    } catch (const std::bad_alloc&) {
        return kNoMemory;
    } catch (const std::exception& e) {
        return std::string("The export failed: ") + e.what();
    } catch (...) {
        return "The export failed.";
    }
}

std::string exportSpriteSheet(const std::filesystem::path& path, const Document& doc, int firstFrame, int lastFrame,
                              const SheetOptions& sheet) {
    try {
        FrameRange range;
        SheetLayout layout;
        const ExportSize size = sheetSize(doc, firstFrame, lastFrame, sheet, range, layout);
        if (!size.ok)
            return size.error;

        const std::vector<Image> frames = compositeRange(doc, range);
        if (frames.size() != static_cast<std::size_t>(range.count()))
            return kNoMemory;
        const Image picture = buildSpriteSheet(frames, layout, sheet.scale);
        if (picture.empty())
            return kNoMemory;
        return saveImage(path, picture, ImageFormat::Png);
    } catch (const std::bad_alloc&) {
        return kNoMemory;
    } catch (const std::exception& e) {
        return std::string("The export failed: ") + e.what();
    } catch (...) {
        return "The export failed.";
    }
}

} // namespace trace
