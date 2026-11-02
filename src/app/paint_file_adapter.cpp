#include "app/paint_file_adapter.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <exception>
#include <string>
#include <utility>

#include "app/image_file_formats.hpp"
#include "app/settings.hpp"
#include "core/board_growth.hpp"
#include "core/document.hpp"
#include "io/board_project.hpp"
#include "io/project_io.hpp"
#include "core/i18n.hpp"
#include "io/path_utf8.hpp"

namespace trace {

namespace {

constexpr std::array<FileFilter, 1> kImageFilters = {{{Str::FilterImages, "png;jpg;jpeg;bmp;tga;gif"}}};
constexpr std::array<FileFilter, 2> kOpenFilters = {{{Str::FilterImages, "png;jpg;jpeg;bmp;tga;gif"}, {Str::FilterBoard, "tboard"}}};

} // namespace

std::span<const FileFilter> imageOpenFilters() {
    return kImageFilters;
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

// Variant 1: an endless whiteboard, a transparent board that grows as it is drawn on.
std::string PaintFileAdapter::newBlankVariant(int variant) {
    if (variant != kNewWhiteboard)
        return newBlank();
    try {
        document_.reset(kBoardStartSide, kBoardStartSide, Rgba8{});
    } catch (const std::exception& e) {
        SDL_Log("Cannot create a whiteboard: %s", e.what());
        return trf(Str::ErrNewMemory, {std::to_string(kBoardStartSide), std::to_string(kBoardStartSide)});
    }
    document_.setEndless(true);
    boardDark_ = settings_.whiteboardDark;
    return {};
}

FileAdapter::LoadOutcome PaintFileAdapter::load(const std::filesystem::path& path) {
    LoadOutcome outcome;
    if (sniffProjectKind(path) == kProjectBoard) {
        BoardProjectLoad board = loadBoardProject(path);
        if (!board.store) {
            outcome.error = std::move(board.error);
            return outcome;
        }
        if (!document_.loadStore(std::move(*board.store))) {
            outcome.error = trf(Str::ErrOpenMemory, {utf8FromPath(path.filename())});
            return outcome;
        }
        document_.setEndless(true);
        boardDark_ = board.dark;
        if (board.damagedCels > 0)
            outcome.notice = trf(Str::FilePixelDamagedCels, {std::to_string(board.damagedCels)});
        return outcome;
    }
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
    if (document_.endless()) {
        // The whiteboard first, then the pictures it can be exported as (index i > 0 is ImageFormat(i - 1)).
        static const std::vector<FileFormat> formats = projectSaveFormats({Str::FilterBoard, "tboard", "tboard"}, false);
        return formats;
    }
    // The picture formats, in ImageFormat order so a format index is the enum's value. PDF and Word are only
    // offered by the Flowchart.
    return imageFileFormats().first(kPictureFormatCount);
}

std::optional<int> PaintFileAdapter::formatForPath(const std::filesystem::path& path) const {
    if (document_.endless())
        return projectFormatForPath(path, "tboard", false);
    if (const std::optional<ImageFormat> format = formatFromPath(path);
        format && static_cast<std::size_t>(*format) < kPictureFormatCount)
        return static_cast<int>(*format);
    return std::nullopt;
}

bool PaintFileAdapter::formatNeedsOptions(int format) const {
    if (document_.endless())
        return format == 1 + static_cast<int>(ImageFormat::Jpeg);
    return format == static_cast<int>(ImageFormat::Jpeg);
}

std::string PaintFileAdapter::save(const std::filesystem::path& path, int format, const SaveOptions& options) {
    if (document_.endless()) {
        if (format == 0)
            return saveBoardProject(path, document_, boardDark_);
        if (format < 1 || static_cast<std::size_t>(format) > kPictureFormatCount)
            return trf(Str::IoCannotSaveImage, {std::to_string(kMaxImageDimension)});
        return saveImage(path, document_.displayImage(), kAllFormats[static_cast<std::size_t>(format - 1)], options);
    }
    if (format < 0 || static_cast<std::size_t>(format) >= kPictureFormatCount)
        return trf(Str::IoCannotSaveImage, {std::to_string(kMaxImageDimension)});
    return saveImage(path, document_.displayImage(), kAllFormats[static_cast<std::size_t>(format)], options);
}

std::span<const FileFilter> PaintFileAdapter::openFilters() const {
    return kOpenFilters;
}

} // namespace trace
