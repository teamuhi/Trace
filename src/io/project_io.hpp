#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/image.hpp"
#include "core/ini.hpp"

namespace trace {

// Trace project files (.tflow, .tpix) share one container: a header, then a list of tagged chunks.
//
//   "TRCPROJ\0"                     8 bytes
//   u32 version                     1
//   u32 kind                        kProjectFlow or kProjectPixel
//   chunks, each:  [4 ASCII tag bytes][u32 length][length bytes]
//   a final chunk tagged "END " (written with length 0; the length is not read)
//
// Integers are little-endian. Chunk tags: "META" holds UTF-8 INI text (parsed with IniFile) and "PNG "
// holds one image, encoded as PNG; the images of a project are numbered in the order of their chunks.
// Unknown chunk tags are skipped, so later versions can add chunks. Reading is safe on hostile input: the
// file, the chunk count and every chunk length are bounded and checked against the bytes that remain.

inline constexpr std::uint32_t kProjectVersion = 1;
inline constexpr std::uint32_t kProjectFlow = 1;
inline constexpr std::uint32_t kProjectPixel = 2;

inline constexpr std::size_t kMaxProjectBytes = 512u * 1024u * 1024u;
inline constexpr std::size_t kMaxProjectChunks = 65536;
// The decoded images of one project may hold at most this many pixels in total (1 GiB of RGBA).
inline constexpr std::size_t kMaxProjectPixels = 256u * 1024u * 1024u;

struct ProjectData {
    std::uint32_t kind = 0;
    IniFile meta;
    // Image N of the project. A PNG chunk with no data, or one that cannot be decoded, reads as an empty
    // Image so the numbering of the others is kept.
    std::vector<Image> images;
};

struct ProjectReadResult {
    std::optional<ProjectData> project; // empty on failure
    std::string error;                  // a message fit to show the user when `project` is empty
    int undecodableImages = 0;          // PNG chunks that were damaged and read as empty images
};

// Parses a project held in memory. `name` is how the source is called in messages. Does not throw.
ProjectReadResult readProject(const std::vector<unsigned char>& bytes, const std::string& name = "project");

// Reads and parses a project file. Does not throw.
ProjectReadResult loadProject(const std::filesystem::path& path);

struct ProjectWriteResult {
    std::vector<unsigned char> bytes; // empty on failure
    std::string error;
};

// Serialises a project. Fails when the result would pass the size limits or an image cannot be encoded
// (an empty Image is written as an empty chunk). Does not throw.
ProjectWriteResult writeProject(const ProjectData& data);

// writeProject followed by an atomic write, like saveImage. Returns an empty string on success.
std::string saveProject(const std::filesystem::path& path, const ProjectData& data);

// The `kind` field of a file that starts with the project magic, whatever the version, or nothing when the
// file cannot be read or is not a project. Reads only the first bytes. Used to route Open and drag-and-drop.
std::optional<std::uint32_t> sniffProjectKind(const std::filesystem::path& path);

} // namespace trace
