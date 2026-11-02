#include "io/board_project.hpp"

#include <algorithm>
#include <climits>
#include <exception>
#include <new>
#include <utility>

#include "core/document.hpp"
#include "core/i18n.hpp"
#include "io/image_io.hpp"
#include "io/path_utf8.hpp"
#include "io/pixel_project.hpp"

namespace trace {

namespace {

constexpr const char* kBoard = "board";
constexpr int kMaxOrigin = 1'000'000; // far beyond any board; keeps the rulers' arithmetic small

BoardProjectLoad failure(std::string message) {
    BoardProjectLoad result;
    result.error = std::move(message);
    return result;
}

int readOrigin(const IniFile& ini, const char* key) {
    if (const std::string* value = ini.get(kBoard, key)) {
        if (const std::optional<int> parsed = parseInt(*value, -kMaxOrigin, kMaxOrigin))
            return *parsed;
    }
    return 0;
}

} // namespace

BoardProjectLoad boardProjectToCels(ProjectData data, const std::string& name) {
    const bool dark = [&] {
        const std::string* theme = data.meta.get(kBoard, "theme");
        return theme && *theme == "black";
    }();
    const int originX = readOrigin(data.meta, "origin_x");
    const int originY = readOrigin(data.meta, "origin_y");

    PixelProjectLoad loaded = pixelProjectToCels(std::move(data), name, kProjectBoard, kMaxImageDimension);
    if (!loaded.store)
        return failure(std::move(loaded.error));
    BoardProjectLoad result;
    result.store = std::move(loaded.store);
    result.store->layout.originX = originX;
    result.store->layout.originY = originY;
    result.damagedCels = loaded.damagedCels;
    result.dark = dark;
    return result;
}

std::string saveBoardProject(const std::filesystem::path& path, const Document& doc, bool dark) {
    try {
        ProjectData data = pixelProjectData(doc);
        data.kind = kProjectBoard;
        data.meta.set(kBoard, "theme", dark ? "black" : "white");
        data.meta.set(kBoard, "origin_x", std::to_string(doc.originX()));
        data.meta.set(kBoard, "origin_y", std::to_string(doc.originY()));
        return saveProject(path, data);
    } catch (const std::bad_alloc&) {
        return trf(Str::IoMemorySave, {utf8FromPath(path.filename())});
    } catch (const std::exception& e) {
        return trf(Str::IoCannotSave, {utf8FromPath(path.filename()), e.what()});
    }
}

BoardProjectLoad loadBoardProject(const std::filesystem::path& path) {
    ProjectReadResult read = loadProject(path);
    if (!read.project)
        return failure(std::move(read.error));
    return boardProjectToCels(std::move(*read.project), utf8FromPath(path.filename()));
}

} // namespace trace
