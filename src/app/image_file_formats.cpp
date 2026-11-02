#include "app/image_file_formats.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include "io/path_utf8.hpp"

namespace trace {

namespace {

// One entry per ImageFormat, in the same order as kAllFormats.
constexpr std::array<FileFormat, 12> kFormats = {{
    {Str::FilterPng, "png", "png"},
    {Str::FilterJpeg, "jpg;jpeg;jpe;jfif", "jpg"},
    {Str::FilterBmp, "bmp;dib", "bmp"},
    {Str::FilterTga, "tga", "tga"},
    {Str::FilterGif, "gif", "gif"},
    {Str::FilterTiff, "tif;tiff", "tiff"},
    {Str::FilterIco, "ico", "ico"},
    {Str::FilterQoi, "qoi", "qoi"},
    {Str::FilterPpm, "ppm", "ppm"},
    {Str::FilterSvg, "svg", "svg"},
    {Str::FilterPdf, "pdf", "pdf"},
    {Str::FilterDocx, "docx", "docx"},
}};
static_assert(kAllFormats.size() == kFormats.size(), "one file type per image format");
static_assert(kAllFormats[0] == ImageFormat::Png && kAllFormats[4] == ImageFormat::Gif &&
                  kAllFormats[9] == ImageFormat::Svg && kAllFormats[11] == ImageFormat::Docx,
              "format indices follow kAllFormats");
static_assert(kPictureFormatCount <= kAllFormats.size() && kAllFormats[kPictureFormatCount] == ImageFormat::Pdf,
              "the page formats come last");

std::string lowercaseExtension(const std::filesystem::path& path) {
    std::string ext = utf8FromPath(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

std::span<const FileFormat> imageFileFormats() {
    return kFormats;
}

std::vector<FileFormat> projectSaveFormats(const FileFormat& project, bool pages) {
    std::vector<FileFormat> formats;
    formats.reserve(1 + kFormats.size());
    formats.push_back(project);
    const std::size_t count = pages ? kFormats.size() : kPictureFormatCount;
    for (std::size_t i = 0; i < count; ++i) {
        FileFormat format = kFormats[i];
        format.exportOnly = true;
        formats.push_back(format);
    }
    return formats;
}

std::optional<int> projectFormatForPath(const std::filesystem::path& path, const char* projectExtension, bool pages) {
    if (lowercaseExtension(path) == std::string(".") + projectExtension)
        return 0;
    if (const std::optional<ImageFormat> format = formatFromPath(path)) {
        const std::size_t index = static_cast<std::size_t>(*format);
        if (pages || index < kPictureFormatCount)
            return static_cast<int>(index) + 1;
    }
    return std::nullopt;
}

} // namespace trace
