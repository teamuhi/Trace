#include "app/paint_file_adapter.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <exception>
#include <string>
#include <utility>

#include "app/settings.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "io/path_utf8.hpp"

namespace trace {

namespace {

// One entry per ImageFormat, in the same order as kAllFormats, so a format index is the enum's value.
constexpr std::array<FileFormat, 5> kFormats = {{
    {Str::FilterPng, "png", "png"},
    {Str::FilterJpeg, "jpg;jpeg", "jpg"},
    {Str::FilterBmp, "bmp", "bmp"},
    {Str::FilterTga, "tga", "tga"},
    {Str::FilterGif, "gif", "gif"},
}};
static_assert(kAllFormats.size() == kFormats.size(), "one file type per image format");
static_assert(kAllFormats[0] == ImageFormat::Png && kAllFormats[1] == ImageFormat::Jpeg &&
                  kAllFormats[2] == ImageFormat::Bmp && kAllFormats[3] == ImageFormat::Tga &&
                  kAllFormats[4] == ImageFormat::Gif,
              "format indices follow kAllFormats");

constexpr std::array<FileFilter, 1> kOpenFilters = {{{Str::FilterImages, "png;jpg;jpeg;bmp;tga;gif"}}};

} // namespace

std::span<const FileFilter> imageOpenFilters() {
    return kOpenFilters;
}

bool PaintFileAdapter::isModified() const {
    return document_.isModified();
}

void PaintFileAdapter::commitPending() {
    document_.commit();
}

void PaintFileAdapter::markSaved() {
    document_.markSaved();
}

std::string PaintFileAdapter::newBlank() {
    try {
        document_.reset(settings_.newWidth, settings_.newHeight);
    } catch (const std::exception& e) {
        SDL_Log("Cannot create a %d x %d canvas: %s", settings_.newWidth, settings_.newHeight, e.what());
        return trf(Str::ErrNewMemory, {std::to_string(settings_.newWidth), std::to_string(settings_.newHeight)});
    }
    return {};
}

FileAdapter::LoadOutcome PaintFileAdapter::load(const std::filesystem::path& path) {
    LoadOutcome outcome;
    LoadResult result = loadImage(path);
    if (!result.image) {
        outcome.error = std::move(result.error);
        return outcome;
    }
    try {
        document_.load(std::move(*result.image));
    } catch (const std::exception& e) {
        SDL_Log("Cannot open %s: %s", utf8FromPath(path).c_str(), e.what());
        outcome.error = trf(Str::ErrOpenMemory, {utf8FromPath(path.filename())});
        return outcome;
    }
    // Saving would write a still image over the original animation, so insist on a new name.
    outcome.lockInPlace = result.frameCount > 1;
    if (result.frameCount > 1)
        outcome.notice = trf(Str::FileGifFrames, {std::to_string(result.frameCount)});
    return outcome;
}

std::span<const FileFormat> PaintFileAdapter::saveFormats() const {
    return kFormats;
}

std::optional<int> PaintFileAdapter::formatForPath(const std::filesystem::path& path) const {
    if (const std::optional<ImageFormat> format = formatFromPath(path))
        return static_cast<int>(*format);
    return std::nullopt;
}

bool PaintFileAdapter::formatNeedsOptions(int format) const {
    return format == static_cast<int>(ImageFormat::Jpeg);
}

std::string PaintFileAdapter::save(const std::filesystem::path& path, int format, const SaveOptions& options) {
    if (format < 0 || static_cast<std::size_t>(format) >= kAllFormats.size())
        return trf(Str::IoCannotSaveImage, {std::to_string(kMaxImageDimension)});
    return saveImage(path, document_.displayImage(), kAllFormats[static_cast<std::size_t>(format)], options);
}

std::span<const FileFilter> PaintFileAdapter::openFilters() const {
    return kOpenFilters;
}

} // namespace trace
