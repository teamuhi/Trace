#pragma once

#include <span>
#include <vector>

#include "app/file_adapter.hpp"
#include "io/image_io.hpp"

namespace trace {

// The Save As entries for every picture format, in ImageFormat order (so an entry's index is the enum's value).
std::span<const FileFormat> imageFileFormats();

// What the project modes (Pixel Art, Flowchart) list: their own project type first, then the formats the document
// can be exported as, flagged export-only because they hold a picture of the document, not the document. With
// `pages` the PDF and Word entries are included. An entry at index i > 0 is ImageFormat(i - 1).
std::vector<FileFormat> projectSaveFormats(const FileFormat& project, bool pages);

// The format of `path` among a project mode's entries (see projectSaveFormats): the project's own extension is 0.
// Nothing for an extension the mode does not list.
std::optional<int> projectFormatForPath(const std::filesystem::path& path, const char* projectExtension, bool pages);

} // namespace trace
