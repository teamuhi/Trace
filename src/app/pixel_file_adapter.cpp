#include "app/pixel_file_adapter.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "app/image_file_formats.hpp"
#include "app/settings.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "core/palette_edit.hpp"
#include "io/path_utf8.hpp"
#include "io/pixel_export.hpp"
#include "io/pixel_project.hpp"

namespace trace {

namespace {

// Projects first, then the images that open in Paint (App routes them there, so choosing one here is fine).
constexpr std::array<FileFilter, 2> kOpenFilters = {{
    {Str::FilterPixelProject, "tpix"},
    {Str::FilterImages, "png;jpg;jpeg;bmp;tga;gif"},
}};

} // namespace

bool PixelFileAdapter::isModified() const {
    return document_.isModified() || palette_.modified();
}

void PixelFileAdapter::commitPending() {
    document_.commit();
}

void PixelFileAdapter::markSaved() {
    document_.markSaved();
    palette_.markSaved();
}

bool PixelFileAdapter::makeBlank(Document& document, int width, int height) {
    width = std::clamp(width, 1, kMaxPixelCanvasSide);
    height = std::clamp(height, 1, kMaxPixelCanvasSide);
    try {
        CelStore store;
        store.layout.layers.push_back(LayerInfo{1, trf(Str::LayerNameDefault, {"1"}), true, 255, false});
        store.layout.frames.push_back(FrameInfo{});
        store.layout.grid.push_back(1);
        store.pool.emplace(1, Image(width, height));
        return document.loadStore(std::move(store));
    } catch (const std::exception& e) {
        SDL_Log("Cannot create a %d x %d pixel canvas: %s", width, height, e.what());
        return false;
    }
}

std::string PixelFileAdapter::newBlank() {
    const int width = std::clamp(settings_.pixel.newWidth, 1, kMaxPixelCanvasSide);
    const int height = std::clamp(settings_.pixel.newHeight, 1, kMaxPixelCanvasSide);
    if (!makeBlank(document_, width, height))
        return trf(Str::ErrNewMemory, {std::to_string(width), std::to_string(height)});
    palette_.reset(defaultPalette());
    return {};
}

FileAdapter::LoadOutcome PixelFileAdapter::load(const std::filesystem::path& path) {
    LoadOutcome outcome;
    PixelProjectLoad loaded = loadPixelProject(path);
    if (!loaded.store) {
        outcome.error = std::move(loaded.error);
        return outcome;
    }
    if (!document_.loadStore(std::move(*loaded.store))) {
        outcome.error = trf(Str::ErrOpenMemory, {utf8FromPath(path.filename())});
        return outcome;
    }
    palette_.reset(loaded.palette ? std::move(*loaded.palette) : defaultPalette());
    if (loaded.damagedCels > 0)
        outcome.notice = trf(Str::FilePixelDamagedCels, {std::to_string(loaded.damagedCels)});
    return outcome;
}

std::span<const FileFormat> PixelFileAdapter::saveFormats() const {
    // The project first, then the pictures it can be exported as (index i > 0 is ImageFormat(i - 1)).
    static const std::vector<FileFormat> formats =
        projectSaveFormats({Str::FilterPixelProject, "tpix", "tpix"}, false);
    return formats;
}

std::optional<int> PixelFileAdapter::formatForPath(const std::filesystem::path& path) const {
    return projectFormatForPath(path, "tpix", false);
}

bool PixelFileAdapter::formatNeedsOptions(int format) const {
    return format == 1 + static_cast<int>(ImageFormat::Jpeg);
}

std::string PixelFileAdapter::save(const std::filesystem::path& path, int format, const SaveOptions& options) {
    if (format == 0)
        return savePixelProject(path, document_, &palette_.palette);
    if (format < 1 || static_cast<std::size_t>(format) > kPictureFormatCount)
        return trf(Str::IoCannotSave, {utf8FromPath(path.filename()), "?"});
    return exportAs(path, document_, static_cast<ImageFormat>(format - 1), options);
}

std::span<const FileFilter> PixelFileAdapter::openFilters() const {
    return kOpenFilters;
}

} // namespace trace
