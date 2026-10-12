#include "io/image_io.hpp"

#include "core/i18n.hpp"
#include "io/file_write.hpp"
#include "io/path_utf8.hpp"

#include <stb_image.h>
#include <stb_image_write.h>
#include <msf_gif.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <sstream>
#include <new>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace trace {

namespace {

std::string lowercaseExtension(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

// UTF-8, never the ANSI code page: path::string() throws for names it cannot represent.
std::string describe(const fs::path& path) {
    return utf8FromPath(path.filename());
}

// --- loading ---------------------------------------------------------------------------------------

// Skips a chain of GIF data sub-blocks starting at `pos`. False when the data ends first.
bool skipGifSubBlocks(const std::vector<unsigned char>& d, std::size_t& pos) {
    while (pos < d.size()) {
        const std::size_t length = d[pos++];
        if (length == 0)
            return true;
        pos += length;
    }
    return false;
}

// Number of image frames in a GIF (1 for anything else, or for a file this walk cannot follow). Only the
// block structure is read, so this is cheap and never decodes pixels.
int countGifFrames(const std::vector<unsigned char>& d) {
    if (d.size() < 13 || std::memcmp(d.data(), "GIF8", 4) != 0)
        return 1;

    std::size_t pos = 10;
    const unsigned screenFlags = d[pos];
    pos += 3; // flags, background color, aspect ratio
    if (screenFlags & 0x80u)
        pos += 3u * (1u << ((screenFlags & 7u) + 1u)); // global color table

    int frames = 0;
    while (pos < d.size()) {
        const unsigned char block = d[pos++];
        if (block == 0x3B) // trailer
            break;
        if (block == 0x21) { // extension: label, then data sub-blocks
            ++pos;
            if (!skipGifSubBlocks(d, pos))
                break;
        } else if (block == 0x2C) { // image descriptor
            ++frames;
            if (pos + 9 > d.size())
                break;
            const unsigned imageFlags = d[pos + 8];
            pos += 9;
            if (imageFlags & 0x80u)
                pos += 3u * (1u << ((imageFlags & 7u) + 1u)); // local color table
            ++pos;                                            // LZW minimum code size
            if (!skipGifSubBlocks(d, pos))
                break;
        } else {
            break;
        }
    }
    return std::max(frames, 1);
}

LoadResult failure(std::string message) {
    return LoadResult{std::nullopt, std::move(message)};
}

LoadResult decodeBytes(const std::vector<unsigned char>& bytes, const std::string& name) {
    // Read the header first so oversized images are refused before any pixel memory is allocated.
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels)) {
        return failure(trf(Str::IoUnsupported, {name}));
    }
    if (width < 1 || height < 1 || width > kMaxImageDimension || height > kMaxImageDimension) {
        return failure(trf(Str::IoTooLarge, {name, std::to_string(kMaxImageDimension)}));
    }

    int w = 0;
    int h = 0;
    int c = 0;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c, 4);
    if (!pixels) {
        const char* reason = stbi_failure_reason();
        return failure(reason ? trf(Str::IoDecodeReason, {name, reason}) : trf(Str::IoDecode, {name}));
    }

    // The decoder's buffer is released on every path out of here.
    struct Free {
        unsigned char* p;
        ~Free() { stbi_image_free(p); }
    } release{pixels};

    Image image(w, h);
    std::memcpy(image.pixels().data(), pixels, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    return LoadResult{std::move(image), {}, countGifFrames(bytes)};
}

LoadResult decode(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return failure(trf(Str::IoCannotOpen, {describe(path)}));

    const std::streamoff size = file.tellg();
    if (size <= 0)
        return failure(trf(Str::IoEmpty, {describe(path)}));
    if (static_cast<std::uintmax_t>(size) > kMaxImageFileBytes)
        return failure(trf(Str::IoTooLargeToOpen, {describe(path)}));

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        return failure(trf(Str::IoCannotRead, {describe(path)}));

    return decodeBytes(bytes, describe(path));
}

// --- saving ----------------------------------------------------------------------------------------

struct Sink {
    std::ostream* out;
    bool ok = true;
};

void writeToSink(void* context, void* data, int size) {
    Sink* sink = static_cast<Sink*>(context);
    if (!sink->ok || size <= 0)
        return;
    sink->out->write(static_cast<const char*>(data), size);
    sink->ok = sink->out->good();
}

// Opaque RGB with transparent pixels composited onto white, for formats without alpha.
std::vector<std::uint8_t> flattenToRgb(const Image& image) {
    std::vector<std::uint8_t> rgb;
    rgb.reserve(image.pixels().size() * 3);
    for (const Rgba8& p : image.pixels()) {
        const Rgba8 out = blendOver(Rgba8{255, 255, 255, 255}, p);
        rgb.push_back(out.r);
        rgb.push_back(out.g);
        rgb.push_back(out.b);
    }
    return rgb;
}

bool encode(std::ostream& out, const Image& image, ImageFormat format, const SaveOptions& options) {
    const int w = image.width();
    const int h = image.height();
    const void* rgba = image.pixels().data();
    Sink sink{&out};

    switch (format) {
    case ImageFormat::Png:
        return stbi_write_png_to_func(writeToSink, &sink, w, h, 4, rgba, w * 4) != 0 && sink.ok;
    case ImageFormat::Bmp:
        return stbi_write_bmp_to_func(writeToSink, &sink, w, h, 4, rgba) != 0 && sink.ok;
    case ImageFormat::Tga:
        return stbi_write_tga_to_func(writeToSink, &sink, w, h, 4, rgba) != 0 && sink.ok;
    case ImageFormat::Jpeg: {
        const std::vector<std::uint8_t> rgb = flattenToRgb(image);
        const int quality = std::clamp(options.jpegQuality, 1, 100);
        return stbi_write_jpg_to_func(writeToSink, &sink, w, h, 3, rgb.data(), quality) != 0 && sink.ok;
    }
    case ImageFormat::Gif: {
        // GIF keeps on/off transparency: pixels at least half transparent become the transparent color.
        // The encoder reads the pixels without modifying them, so the const is cast away only for its API.
        MsfGifState state = {};
        msf_gif_alpha_threshold = 128;
        msf_gif_bgra_flag = 0;
        bool ok = msf_gif_begin(&state, w, h) != 0 &&
                  msf_gif_frame(&state, const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(rgba)), 0, 16,
                                w * 4) != 0;
        MsfGifResult result = msf_gif_end(&state);
        msf_gif_alpha_threshold = 0;
        if (ok && result.data) {
            out.write(static_cast<const char*>(result.data), static_cast<std::streamsize>(result.dataSize));
            ok = out.good();
        } else {
            ok = false;
        }
        msf_gif_free(result);
        return ok;
    }
    }
    return false;
}

} // namespace

std::optional<ImageFormat> formatFromPath(const fs::path& path) {
    const std::string ext = lowercaseExtension(path);
    if (ext == ".png")
        return ImageFormat::Png;
    if (ext == ".jpg" || ext == ".jpeg")
        return ImageFormat::Jpeg;
    if (ext == ".bmp")
        return ImageFormat::Bmp;
    if (ext == ".tga")
        return ImageFormat::Tga;
    if (ext == ".gif")
        return ImageFormat::Gif;
    return std::nullopt;
}

const char* formatName(ImageFormat format) {
    switch (format) {
    case ImageFormat::Png:
        return "PNG";
    case ImageFormat::Jpeg:
        return "JPEG";
    case ImageFormat::Bmp:
        return "BMP";
    case ImageFormat::Tga:
        return "TGA";
    case ImageFormat::Gif:
        return "GIF";
    }
    return "";
}

const char* defaultExtension(ImageFormat format) {
    switch (format) {
    case ImageFormat::Png:
        return "png";
    case ImageFormat::Jpeg:
        return "jpg";
    case ImageFormat::Bmp:
        return "bmp";
    case ImageFormat::Tga:
        return "tga";
    case ImageFormat::Gif:
        return "gif";
    }
    return "";
}

bool formatSupportsAlpha(ImageFormat format) {
    return format != ImageFormat::Jpeg;
}

SaveTarget resolveSaveTarget(const fs::path& chosen, ImageFormat fallback) {
    if (const std::optional<ImageFormat> format = formatFromPath(chosen))
        return SaveTarget{chosen, *format, false};

    fs::path path = chosen;
    path += std::string(".") + defaultExtension(fallback);
    return SaveTarget{std::move(path), fallback, true};
}

LoadResult decodeImage(const std::vector<unsigned char>& bytes, const std::string& name) {
    try {
        return decodeBytes(bytes, name);
    } catch (const std::bad_alloc&) {
        return failure(trf(Str::IoMemoryOpenNamed, {name}));
    } catch (const std::exception& e) {
        return failure(trf(Str::IoCannotOpenNamed, {name, e.what()}));
    }
}

std::vector<unsigned char> encodeImage(const Image& image, ImageFormat format, const SaveOptions& options) {
    if (image.empty() || image.width() > kMaxImageDimension || image.height() > kMaxImageDimension)
        return {};
    try {
        std::ostringstream out(std::ios::binary);
        if (!encode(out, image, format, options))
            return {};
        const std::string text = out.str();
        return std::vector<unsigned char>(text.begin(), text.end());
    } catch (const std::exception&) {
        return {};
    }
}

LoadResult loadImage(const fs::path& path) {
    try {
        return decode(path);
    } catch (const std::bad_alloc&) {
        return failure(trf(Str::IoMemoryOpen, {describe(path)}));
    } catch (const std::exception& e) {
        return failure(trf(Str::IoCannotOpenReason, {describe(path), e.what()}));
    }
}

std::string saveImage(const fs::path& path, const Image& image, ImageFormat format, const SaveOptions& options) {
    if (image.empty() || image.width() > kMaxImageDimension || image.height() > kMaxImageDimension)
        return trf(Str::IoCannotSaveImage, {std::to_string(kMaxImageDimension)});

    return writeFileAtomic(path, [&](std::ostream& out) { return encode(out, image, format, options); });
}

std::string saveAnimatedGif(const fs::path& path, std::span<const Image> frames, std::span<const int> delaysCs,
                            int alphaThreshold) {
    const std::string tooBig = trf(Str::IoCannotSaveGif, {std::to_string(kMaxImageDimension)});
    if (frames.empty() || frames.size() > kMaxGifFrames)
        return tooBig;
    const int w = frames.front().width();
    const int h = frames.front().height();
    if (w < 1 || h < 1 || w > kMaxImageDimension || h > kMaxImageDimension)
        return tooBig;
    for (const Image& frame : frames) {
        if (frame.width() != w || frame.height() != h)
            return tooBig;
    }
    const int threshold = std::clamp(alphaThreshold, 0, 255);

    return writeFileAtomic(path, [&](std::ostream& out) {
        MsfGifState state = {};
        msf_gif_alpha_threshold = threshold;
        msf_gif_bgra_flag = 0;
        bool ok = msf_gif_begin(&state, w, h) != 0;
        for (std::size_t i = 0; ok && i < frames.size(); ++i) {
            const int delay = i < delaysCs.size() ? std::clamp(delaysCs[i], kMinGifDelayCs, 65535) : kDefaultGifDelayCs;
            // The encoder only reads the pixels; the const is cast away for its API.
            ok = msf_gif_frame(&state, const_cast<std::uint8_t*>(reinterpret_cast<const std::uint8_t*>(frames[i].pixels().data())),
                               delay, 16, w * 4) != 0;
        }
        MsfGifResult result = msf_gif_end(&state);
        msf_gif_alpha_threshold = 0;
        if (ok && result.data) {
            out.write(static_cast<const char*>(result.data), static_cast<std::streamsize>(result.dataSize));
            ok = out.good();
        } else {
            ok = false;
        }
        msf_gif_free(result);
        return ok;
    });
}

} // namespace trace
