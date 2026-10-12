#include "io/pixel_project.hpp"

#include <algorithm>
#include <climits>
#include <exception>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/document.hpp"
#include "core/i18n.hpp"
#include "io/path_utf8.hpp"

namespace trace {

namespace {

constexpr const char* kDoc = "doc";
constexpr const char* kCel = "cel";

PixelProjectLoad failure(std::string message) {
    PixelProjectLoad result;
    result.error = std::move(message);
    return result;
}

bool hasVisiblePixel(const Image& image) noexcept {
    for (const Rgba8& p : image.pixels()) {
        if (p.a != 0)
            return true;
    }
    return false;
}

std::string layerSection(int index) {
    return "layer." + std::to_string(index);
}

std::string frameSection(int index) {
    return "frame." + std::to_string(index);
}

std::string celKey(int layer, int frame) {
    return std::to_string(layer) + "," + std::to_string(frame);
}

// A whole number inside [lo, hi], or nothing when the key is missing or is not one.
std::optional<int> readInt(const IniFile& ini, const std::string& section, const char* key, int lo, int hi) {
    const std::string* value = ini.get(section, key);
    return value ? parseInt(*value, lo, hi) : std::nullopt;
}

// Like readInt, but a number outside the range is pulled to the nearest end instead of being refused.
int readClamped(const IniFile& ini, const std::string& section, const char* key, int lo, int hi, int fallback) {
    if (const std::optional<int> value = readInt(ini, section, key, INT_MIN, INT_MAX))
        return std::clamp(*value, lo, hi);
    return fallback;
}

bool readBool(const IniFile& ini, const std::string& section, const char* key, bool fallback) {
    if (const std::string* value = ini.get(section, key)) {
        if (const std::optional<bool> parsed = parseBool(*value))
            return *parsed;
    }
    return fallback;
}

} // namespace

ProjectData pixelProjectData(const Document& doc) {
    ProjectData data;
    data.kind = kProjectPixel;
    const Layout& layout = doc.layout();
    const int layers = layout.layerCount();
    const int frames = layout.frameCount();

    IniFile& meta = data.meta;
    meta.set(kDoc, "width", std::to_string(doc.cel(0, 0).width()));
    meta.set(kDoc, "height", std::to_string(doc.cel(0, 0).height()));
    meta.set(kDoc, "layers", std::to_string(layers));
    meta.set(kDoc, "frames", std::to_string(frames));
    meta.set(kDoc, "active_layer", std::to_string(layout.activeLayer));
    meta.set(kDoc, "active_frame", std::to_string(layout.activeFrame));

    for (int l = 0; l < layers; ++l) {
        const LayerInfo& info = layout.layers[static_cast<std::size_t>(l)];
        const std::string section = layerSection(l);
        meta.set(section, "name", info.name);
        meta.set(section, "visible", info.visible ? "true" : "false");
        meta.set(section, "opacity", std::to_string(info.opacity));
        meta.set(section, "locked", info.locked ? "true" : "false");
    }
    for (int f = 0; f < frames; ++f)
        meta.set(frameSection(f), "duration_ms", std::to_string(layout.frames[static_cast<std::size_t>(f)].durationMs));

    for (int f = 0; f < frames; ++f) {
        for (int l = 0; l < layers; ++l) {
            const Image& cel = doc.cel(l, f);
            if (cel.empty() || !hasVisiblePixel(cel))
                continue;
            meta.set(kCel, celKey(l, f), std::to_string(data.images.size()));
            data.images.push_back(cel);
        }
    }
    return data;
}

PixelProjectLoad pixelProjectToCels(ProjectData data, const std::string& name) {
    try {
        if (data.kind != kProjectPixel)
            return failure(trf(Str::IoPixelNotPixel, {name}));
        const IniFile& ini = data.meta;
        if (!ini.section(kDoc))
            return failure(trf(Str::IoPixelUnusable, {name}));

        // Layers and frames: a missing count means one; a count beyond the limits is refused, not trimmed,
        // because trimming would silently drop someone's work.
        int layerCount = 1;
        int frameCount = 1;
        if (const std::string* value = ini.get(kDoc, "layers")) {
            const std::optional<int> parsed = parseInt(*value, 1, kMaxLayers);
            if (!parsed)
                return failure(trf(Str::IoPixelLimits, {name, std::to_string(kMaxLayers), std::to_string(kMaxFrames)}));
            layerCount = *parsed;
        }
        if (const std::string* value = ini.get(kDoc, "frames")) {
            const std::optional<int> parsed = parseInt(*value, 1, kMaxFrames);
            if (!parsed)
                return failure(trf(Str::IoPixelLimits, {name, std::to_string(kMaxLayers), std::to_string(kMaxFrames)}));
            frameCount = *parsed;
        }

        // The canvas size. A missing size falls back to the first picture in the file.
        int width = 0;
        int height = 0;
        if (const std::optional<int> w = readInt(ini, kDoc, "width", 1, INT_MAX))
            width = *w;
        if (const std::optional<int> h = readInt(ini, kDoc, "height", 1, INT_MAX))
            height = *h;
        if (width == 0 || height == 0) {
            const auto first = std::find_if(data.images.begin(), data.images.end(), [](const Image& i) { return !i.empty(); });
            if (first == data.images.end())
                return failure(trf(Str::IoPixelUnusable, {name}));
            width = first->width();
            height = first->height();
        }
        if (width > kMaxPixelCanvasSide || height > kMaxPixelCanvasSide)
            return failure(trf(Str::IoPixelTooBig, {name, std::to_string(width), std::to_string(height),
                                                    std::to_string(kMaxPixelCanvasSide)}));
        if (!fitsDocumentBudget(width, height, layerCount, frameCount))
            return failure(trf(Str::IoPixelTooMuch, {name, std::to_string(layerCount), std::to_string(frameCount),
                                                     std::to_string(width), std::to_string(height)}));

        PixelProjectLoad result;
        CelStore store;
        Layout& layout = store.layout;
        for (int l = 0; l < layerCount; ++l) {
            const std::string section = layerSection(l);
            LayerInfo info;
            info.id = static_cast<std::uint32_t>(l) + 1;
            if (const std::string* value = ini.get(section, "name"))
                info.name = *value; // cleaned by Document::loadStore
            info.visible = readBool(ini, section, "visible", true);
            info.opacity = static_cast<std::uint8_t>(readClamped(ini, section, "opacity", 0, 255, 255));
            info.locked = readBool(ini, section, "locked", false);
            layout.layers.push_back(std::move(info));
        }
        for (int f = 0; f < frameCount; ++f)
            layout.frames.push_back(
                FrameInfo{readClamped(ini, frameSection(f), "duration_ms", kMinFrameMs, kMaxFrameMs, kDefaultFrameMs)});
        layout.activeLayer = readClamped(ini, kDoc, "active_layer", 0, layerCount - 1, 0);
        layout.activeFrame = readClamped(ini, kDoc, "active_frame", 0, frameCount - 1, 0);

        // The cels. A picture used by more than one cel is moved into the first and copied for the rest.
        std::vector<CelId> owner(data.images.size(), kNoCel); // the cel that took each picture
        layout.grid.assign(static_cast<std::size_t>(layerCount) * static_cast<std::size_t>(frameCount), kNoCel);
        CelId nextId = 1;
        for (int f = 0; f < frameCount; ++f) {
            for (int l = 0; l < layerCount; ++l) {
                const CelId id = nextId++;
                Image cel;
                if (const std::string* value = ini.get(kCel, celKey(l, f))) {
                    const std::optional<int> index =
                        parseInt(*value, 0, static_cast<int>(std::min<std::size_t>(data.images.size(), INT_MAX)) - 1);
                    bool usable = false;
                    if (index) {
                        const auto slot = static_cast<std::size_t>(*index);
                        Image& source = data.images[slot];
                        if (owner[slot] != kNoCel) {
                            cel = store.pool.at(owner[slot]); // already given to an earlier cel: copy that
                            usable = true;
                        } else if (source.width() == width && source.height() == height) {
                            cel = std::move(source);
                            owner[slot] = id;
                            usable = true;
                        }
                    }
                    if (!usable)
                        ++result.damagedCels;
                }
                if (cel.empty())
                    cel = Image(width, height); // missing, damaged or never written: transparent
                layout.grid[static_cast<std::size_t>(f) * static_cast<std::size_t>(layerCount) + static_cast<std::size_t>(l)] = id;
                store.pool.emplace(id, std::move(cel));
            }
        }
        result.store = std::move(store);
        return result;
    } catch (const std::bad_alloc&) {
        return failure(trf(Str::IoMemoryOpenNamed, {name}));
    } catch (const std::exception& e) {
        return failure(trf(Str::IoCannotOpenNamed, {name, e.what()}));
    }
}

std::string savePixelProject(const std::filesystem::path& path, const Document& doc) {
    try {
        return saveProject(path, pixelProjectData(doc));
    } catch (const std::bad_alloc&) {
        return trf(Str::IoMemorySave, {utf8FromPath(path.filename())});
    } catch (const std::exception& e) {
        return trf(Str::IoCannotSave, {utf8FromPath(path.filename()), e.what()});
    }
}

PixelProjectLoad loadPixelProject(const std::filesystem::path& path) {
    ProjectReadResult read = loadProject(path);
    if (!read.project)
        return failure(std::move(read.error));
    PixelProjectLoad result = pixelProjectToCels(std::move(*read.project), utf8FromPath(path.filename()));
    return result;
}

} // namespace trace
