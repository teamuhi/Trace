#include "io/project_io.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <fstream>
#include <new>
#include <string_view>
#include <utility>

#include "core/i18n.hpp"
#include "io/file_write.hpp"
#include "io/image_io.hpp"
#include "io/path_utf8.hpp"

namespace fs = std::filesystem;

namespace trace {

namespace {

constexpr char kMagic[8] = {'T', 'R', 'C', 'P', 'R', 'O', 'J', '\0'};
constexpr std::size_t kHeaderSize = 16;
constexpr char kTagMeta[4] = {'M', 'E', 'T', 'A'};
constexpr char kTagPng[4] = {'P', 'N', 'G', ' '};
constexpr char kTagEnd[4] = {'E', 'N', 'D', ' '};

std::uint32_t readU32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void putU32(std::vector<unsigned char>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFFu));
}

void putTag(std::vector<unsigned char>& out, const char (&tag)[4]) {
    out.insert(out.end(), tag, tag + 4);
}

bool sameTag(const unsigned char* p, const char (&tag)[4]) {
    return std::memcmp(p, tag, 4) == 0;
}

ProjectReadResult failure(std::string message) {
    ProjectReadResult result;
    result.error = std::move(message);
    return result;
}

ProjectReadResult parse(const std::vector<unsigned char>& bytes, const std::string& name) {
    if (bytes.size() < kHeaderSize || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0)
        return failure(trf(Str::IoProjectNotProject, {name}));
    if (bytes.size() > kMaxProjectBytes)
        return failure(trf(Str::IoTooLargeToOpen, {name}));

    const std::uint32_t version = readU32(bytes.data() + 8);
    if (version == 0)
        return failure(trf(Str::IoProjectCorrupt, {name}));
    if (version > kProjectVersion)
        return failure(trf(Str::IoProjectTooNew, {name}));

    ProjectData data;
    data.kind = readU32(bytes.data() + 12);
    int undecodable = 0;
    std::size_t pixelsUsed = 0;
    bool haveMeta = false;
    bool sawEnd = false;

    std::size_t pos = kHeaderSize;
    for (std::size_t chunks = 0; pos < bytes.size(); ++chunks) {
        if (chunks >= kMaxProjectChunks)
            return failure(trf(Str::IoProjectCorrupt, {name}));
        if (bytes.size() - pos < 4)
            return failure(trf(Str::IoProjectCorrupt, {name})); // a tag cut short
        const unsigned char* tag = bytes.data() + pos;
        pos += 4;
        if (sameTag(tag, kTagEnd)) {
            sawEnd = true;
            break;
        }
        if (bytes.size() - pos < 4)
            return failure(trf(Str::IoProjectCorrupt, {name}));
        const std::size_t length = readU32(bytes.data() + pos);
        pos += 4;
        if (length > bytes.size() - pos)
            return failure(trf(Str::IoProjectCorrupt, {name})); // longer than what is left of the file
        const unsigned char* body = bytes.data() + pos;
        pos += length;

        if (sameTag(tag, kTagMeta)) {
            if (!haveMeta) { // a second META is ignored
                data.meta = IniFile::parse(std::string_view(reinterpret_cast<const char*>(body), length));
                haveMeta = true;
            }
        } else if (sameTag(tag, kTagPng)) {
            Image image;
            if (length > 0) {
                const std::vector<unsigned char> png(body, body + length);
                LoadResult decoded = decodeImage(png, name);
                if (decoded.image) {
                    const std::size_t pixels = static_cast<std::size_t>(decoded.image->width()) *
                                               static_cast<std::size_t>(decoded.image->height());
                    if (pixels <= kMaxProjectPixels - pixelsUsed) {
                        pixelsUsed += pixels;
                        image = std::move(*decoded.image);
                    } else {
                        ++undecodable; // over the memory budget: kept as a placeholder
                    }
                } else {
                    ++undecodable;
                }
            }
            data.images.push_back(std::move(image));
        }
        // anything else is a chunk from a newer version: skipped
    }
    if (!sawEnd)
        return failure(trf(Str::IoProjectCorrupt, {name})); // cut off before the END chunk

    ProjectReadResult result;
    result.project = std::move(data);
    result.undecodableImages = undecodable;
    return result;
}

} // namespace

ProjectReadResult readProject(const std::vector<unsigned char>& bytes, const std::string& name) {
    try {
        return parse(bytes, name);
    } catch (const std::bad_alloc&) {
        return failure(trf(Str::IoMemoryOpenNamed, {name}));
    } catch (const std::exception& e) {
        return failure(trf(Str::IoCannotOpenNamed, {name, e.what()}));
    }
}

ProjectReadResult loadProject(const fs::path& path) {
    const std::string name = utf8FromPath(path.filename());
    try {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return failure(trf(Str::IoCannotOpen, {name}));
        const std::streamoff size = file.tellg();
        if (size <= 0)
            return failure(trf(Str::IoEmpty, {name}));
        if (static_cast<std::uintmax_t>(size) > kMaxProjectBytes)
            return failure(trf(Str::IoTooLargeToOpen, {name}));

        std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
            return failure(trf(Str::IoCannotRead, {name}));
        return readProject(bytes, name);
    } catch (const std::bad_alloc&) {
        return failure(trf(Str::IoMemoryOpen, {name}));
    } catch (const std::exception& e) {
        return failure(trf(Str::IoCannotOpenReason, {name, e.what()}));
    }
}

ProjectWriteResult writeProject(const ProjectData& data) {
    ProjectWriteResult result;
    const auto fail = [&result](std::string message) {
        result.bytes.clear();
        result.error = std::move(message);
        return result;
    };
    try {
        std::vector<unsigned char>& out = result.bytes;
        out.insert(out.end(), kMagic, kMagic + sizeof(kMagic));
        putU32(out, kProjectVersion);
        putU32(out, data.kind);

        if (data.images.size() + 2 > kMaxProjectChunks)
            return fail(tr(Str::IoProjectTooBigToSave));

        const std::string meta = data.meta.serialize();
        if (meta.size() > kMaxProjectBytes)
            return fail(tr(Str::IoProjectTooBigToSave));
        putTag(out, kTagMeta);
        putU32(out, static_cast<std::uint32_t>(meta.size()));
        out.insert(out.end(), meta.begin(), meta.end());

        for (const Image& image : data.images) {
            std::vector<unsigned char> png;
            if (!image.empty()) {
                png = encodeImage(image, ImageFormat::Png);
                if (png.empty())
                    return fail(trf(Str::IoCannotSaveImage, {std::to_string(kMaxImageDimension)}));
            }
            putTag(out, kTagPng);
            putU32(out, static_cast<std::uint32_t>(png.size()));
            out.insert(out.end(), png.begin(), png.end());
            if (out.size() > kMaxProjectBytes)
                return fail(tr(Str::IoProjectTooBigToSave));
        }

        putTag(out, kTagEnd);
        putU32(out, 0);
        if (out.size() > kMaxProjectBytes)
            return fail(tr(Str::IoProjectTooBigToSave));
    } catch (const std::bad_alloc&) {
        return fail(tr(Str::IoProjectTooBigToSave));
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    return result;
}

std::string saveProject(const fs::path& path, const ProjectData& data) {
    const ProjectWriteResult written = writeProject(data);
    if (written.bytes.empty())
        return written.error.empty() ? std::string(tr(Str::IoProjectTooBigToSave)) : written.error;
    return writeFileAtomic(path, written.bytes);
}

std::optional<std::uint32_t> sniffProjectKind(const fs::path& path) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return std::nullopt;
        unsigned char header[kHeaderSize] = {};
        file.read(reinterpret_cast<char*>(header), sizeof(header));
        if (file.gcount() < static_cast<std::streamsize>(sizeof(header)) ||
            std::memcmp(header, kMagic, sizeof(kMagic)) != 0)
            return std::nullopt;
        return readU32(header + 12);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace trace
