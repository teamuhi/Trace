#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/cel_grid.hpp"
#include "io/project_io.hpp"

namespace trace {

class Document;

// The whiteboard project (.tboard): an endless Paint board written into the project container (kind kProjectBoard).
// The layers and their pictures are stored exactly as a Pixel Art project stores them (see io/pixel_project.hpp), with
// one more section:
//
//   [board]    theme (white or black), origin_x, origin_y (the world position of pixel (0, 0))
//
// The canvas may be as large as a picture (kMaxImageDimension), still inside the document memory budget. Reading is as
// safe as for the other projects: nothing throws and a hostile file cannot make it allocate beyond the limits.

struct BoardProjectLoad {
    std::optional<CelStore> store; // empty on failure; the layout carries the origin
    std::string error;             // a message fit to show the user when `store` is empty
    int damagedCels = 0;
    bool dark = false;             // black paper
};

// Turns a decoded project into a cel grid ready for Document::loadStore.
BoardProjectLoad boardProjectToCels(ProjectData data, const std::string& name);

// Writes the whiteboard to `path` atomically. Returns "" or a message fit to show the user; does not throw.
std::string saveBoardProject(const std::filesystem::path& path, const Document& doc, bool dark);

// Reads a .tboard file. Does not throw.
BoardProjectLoad loadBoardProject(const std::filesystem::path& path);

} // namespace trace
