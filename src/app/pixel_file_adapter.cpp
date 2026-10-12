#include "app/pixel_file_adapter.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <string>
#include <utility>

#include "app/settings.hpp"
#include "core/document.hpp"
#include "core/i18n.hpp"
#include "io/path_utf8.hpp"
#include "io/pixel_project.hpp"

namespace trace {

namespace {

constexpr std::array<FileFormat, 1> kFormats = {{{Str::FilterPixelProject, "tpix", "tpix"}}};

// Projects first, then the images that open in Paint (App routes them there, so choosing one here is fine).
constexpr std::array<FileFilter, 2> kOpenFilters = {{
    {Str::FilterPixelProject, "tpix"},
    {Str::FilterImages, "png;jpg;jpeg;bmp;tga;gif"},
}};

bool hasExtension(const std::filesystem::path& path, const char* ext) {
    std::string actual = utf8FromPath(path.extension());
    std::transform(actual.begin(), actual.end(), actual.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return actual == std::string(".") + ext;
}

} // namespace

bool PixelFileAdapter::isModified() const {
    return document_.isModified();
}

void PixelFileAdapter::commitPending() {
    document_.commit();
}

void PixelFileAdapter::markSaved() {
    document_.markSaved();
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
    if (loaded.damagedCels > 0)
        outcome.notice = trf(Str::FilePixelDamagedCels, {std::to_string(loaded.damagedCels)});
    return outcome;
}

std::span<const FileFormat> PixelFileAdapter::saveFormats() const {
    return kFormats;
}

std::optional<int> PixelFileAdapter::formatForPath(const std::filesystem::path& path) const {
    if (hasExtension(path, "tpix"))
        return 0;
    return std::nullopt;
}

std::string PixelFileAdapter::save(const std::filesystem::path& path, int format, const SaveOptions&) {
    if (format != 0)
        return trf(Str::IoCannotSave, {utf8FromPath(path.filename()), "?"});
    return savePixelProject(path, document_);
}

std::span<const FileFilter> PixelFileAdapter::openFilters() const {
    return kOpenFilters;
}

} // namespace trace
