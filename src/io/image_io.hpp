#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/image.hpp"

namespace trace {

enum class ImageFormat { Png, Jpeg, Bmp, Tga, Gif };

// Every supported format, in the order the save dialog lists them.
inline constexpr std::array<ImageFormat, 5> kAllFormats = {ImageFormat::Png, ImageFormat::Jpeg, ImageFormat::Bmp,
                                                           ImageFormat::Tga, ImageFormat::Gif};

inline constexpr int kMaxImageDimension = 16384;
inline constexpr std::uintmax_t kMaxImageFileBytes = 512u * 1024u * 1024u;
inline constexpr int kDefaultJpegQuality = 90;

// The format named by the file extension (case-insensitive: png, jpg, jpeg, bmp, tga, gif).
std::optional<ImageFormat> formatFromPath(const std::filesystem::path& path);

const char* formatName(ImageFormat format);      // "PNG"
const char* defaultExtension(ImageFormat format); // "png", without the dot

// Whether the format can store transparency. GIF keeps only on/off transparency; JPEG keeps none.
bool formatSupportsAlpha(ImageFormat format);

struct LoadResult {
    std::optional<Image> image; // empty on failure
    std::string error;          // a message fit to show the user when `image` is empty
    int frameCount = 1;         // frames in the file; animated GIFs have several but only the first is loaded
};

// Decodes by content, not by extension. Animated GIFs load their first frame. Files that are empty, too
// large, truncated, corrupt, of an unsupported type or taller or wider than kMaxImageDimension fail with a
// message; this function does not throw.
LoadResult loadImage(const std::filesystem::path& path);

struct SaveOptions {
    int jpegQuality = kDefaultJpegQuality; // 1-100, used for JPEG only
};

// Decodes an image held in memory (for example from the clipboard) with the same rules and messages as
// loadImage; `name` is how the source is called in messages.
LoadResult decodeImage(const std::vector<unsigned char>& bytes, const std::string& name = "image");

// The encoded bytes of `image`, or an empty vector on failure. Same flattening rules as saveImage.
std::vector<unsigned char> encodeImage(const Image& image, ImageFormat format, const SaveOptions& options = {});

struct SaveTarget {
    std::filesystem::path path;
    ImageFormat format = ImageFormat::Png;
    bool extensionAdded = false; // the user's name had no usable extension, so one was appended
};

// A recognised extension on `chosen` decides the format. Otherwise `fallback` (for example the file type
// selected in the save dialog) is used and its extension is appended, so the result always names a format.
SaveTarget resolveSaveTarget(const std::filesystem::path& chosen, ImageFormat fallback);


// Writes `image` in `format`. Formats without alpha are flattened onto white. The file is written to a
// temporary name and renamed over `path` only when complete, so a failed save never damages an existing
// file. Returns an empty string on success, otherwise a message fit to show the user; does not throw.
std::string saveImage(const std::filesystem::path& path, const Image& image, ImageFormat format,
                      const SaveOptions& options = {});

} // namespace trace
