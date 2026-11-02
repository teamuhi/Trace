#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "io/image_extra.hpp"
#include "io/image_io.hpp"

using namespace trace;
namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<std::uint8_t>;

std::uint32_t le16(const Bytes& b, std::size_t at) { return b[at] | (static_cast<std::uint32_t>(b[at + 1]) << 8); }
std::uint32_t le32(const Bytes& b, std::size_t at) { return le16(b, at) | (le16(b, at + 2) << 16); }
std::uint32_t be32(const Bytes& b, std::size_t at) {
    return (static_cast<std::uint32_t>(b[at]) << 24) | (static_cast<std::uint32_t>(b[at + 1]) << 16) |
           (static_cast<std::uint32_t>(b[at + 2]) << 8) | b[at + 3];
}

std::string text(const Bytes& b) { return std::string(b.begin(), b.end()); }

bool contains(const Bytes& b, const std::string& needle) { return text(b).find(needle) != std::string::npos; }

// Flat colors, a gradient, a transparent corner, runs and noise: every QOI opcode gets used.
Image mixedImage(int w, int h) {
    Image img(w, h, Rgba8{255, 255, 255, 255});
    std::mt19937 rng(7);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Rgba8 c{static_cast<std::uint8_t>(x * 5), static_cast<std::uint8_t>(y * 7), 90, 255};
            if (y < h / 4)
                c = Rgba8{200, 30, 30, 255};                       // long runs
            else if (y < h / 2)
                c = Rgba8{static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng()),
                          static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng())}; // noise with alpha
            else if (x < w / 4)
                c = Rgba8{};                                       // transparent
            img.set(x, y, c);
        }
    }
    return img;
}

// A decoder written from the QOI specification, to check what the encoder wrote.
bool decodeQoi(const Bytes& in, Image& out) {
    if (in.size() < 22 || text(Bytes(in.begin(), in.begin() + 4)) != "qoif")
        return false;
    const int w = static_cast<int>(be32(in, 4));
    const int h = static_cast<int>(be32(in, 8));
    if (in[12] != 4)
        return false;
    out = Image(w, h);
    std::array<Rgba8, 64> seen{};
    Rgba8 px{0, 0, 0, 255};
    std::size_t at = 14;
    int run = 0;
    for (std::size_t i = 0; i < out.pixels().size(); ++i) {
        if (run > 0) {
            --run;
        } else {
            const std::uint8_t b = in[at++];
            if (b == 0xFE) {
                px.r = in[at++];
                px.g = in[at++];
                px.b = in[at++];
            } else if (b == 0xFF) {
                px.r = in[at++];
                px.g = in[at++];
                px.b = in[at++];
                px.a = in[at++];
            } else if ((b & 0xC0) == 0x00) {
                px = seen[b];
            } else if ((b & 0xC0) == 0x40) {
                px.r = static_cast<std::uint8_t>(px.r + ((b >> 4) & 3) - 2);
                px.g = static_cast<std::uint8_t>(px.g + ((b >> 2) & 3) - 2);
                px.b = static_cast<std::uint8_t>(px.b + (b & 3) - 2);
            } else if ((b & 0xC0) == 0x80) {
                const int dg = (b & 0x3F) - 32;
                const std::uint8_t b2 = in[at++];
                px.r = static_cast<std::uint8_t>(px.r + dg - 8 + ((b2 >> 4) & 15));
                px.g = static_cast<std::uint8_t>(px.g + dg);
                px.b = static_cast<std::uint8_t>(px.b + dg - 8 + (b2 & 15));
            } else {
                run = b & 0x3F;
            }
            seen[(px.r * 3u + px.g * 5u + px.b * 7u + px.a * 11u) % 64u] = px;
        }
        out.pixels()[i] = px;
    }
    static const Bytes end = {0, 0, 0, 0, 0, 0, 0, 1};
    return Bytes(in.end() - 8, in.end()) == end;
}

bool samePixels(const Image& a, const Image& b) {
    if (a.width() != b.width() || a.height() != b.height())
        return false;
    for (std::size_t i = 0; i < a.pixels().size(); ++i) {
        const Rgba8 p = a.pixels()[i];
        const Rgba8 q = b.pixels()[i];
        if (p.r != q.r || p.g != q.g || p.b != q.b || p.a != q.a)
            return false;
    }
    return true;
}

} // namespace

TEST_CASE("The new extensions name their format, in any case") {
    CHECK(formatFromPath("a.TIF") == ImageFormat::Tiff);
    CHECK(formatFromPath("a.tiff") == ImageFormat::Tiff);
    CHECK(formatFromPath("a.ico") == ImageFormat::Ico);
    CHECK(formatFromPath("a.qoi") == ImageFormat::Qoi);
    CHECK(formatFromPath("a.ppm") == ImageFormat::Ppm);
    CHECK(formatFromPath("a.svg") == ImageFormat::Svg);
    CHECK(formatFromPath("a.PDF") == ImageFormat::Pdf);
    CHECK(formatFromPath("a.docx") == ImageFormat::Docx);
    CHECK(formatFromPath("a.jfif") == ImageFormat::Jpeg);
    CHECK(formatFromPath("a.jpe") == ImageFormat::Jpeg);
    CHECK(formatFromPath("a.dib") == ImageFormat::Bmp);
    CHECK_FALSE(formatFromPath("a.doc").has_value());
    for (const ImageFormat f : kAllFormats) {
        CHECK(formatFromPath(std::string("x.") + defaultExtension(f)) == f);
        CHECK(std::string(formatName(f)).size() >= 3);
    }
    CHECK_FALSE(formatSupportsAlpha(ImageFormat::Ppm));
    CHECK(formatSupportsAlpha(ImageFormat::Tiff));
}

TEST_CASE("QOI round trips every pixel") {
    for (const auto& [w, h] : {std::pair{1, 1}, std::pair{3, 2}, std::pair{64, 48}, std::pair{200, 5}}) {
        const Image image = mixedImage(w, h);
        const Bytes bytes = encodeExtra(image, ImageFormat::Qoi, {});
        REQUIRE_FALSE(bytes.empty());
        Image decoded;
        REQUIRE(decodeQoi(bytes, decoded));
        CHECK(samePixels(image, decoded));
    }
    // A single flat color is almost all runs of 62 pixels, a byte each (90000 pixels: about 1450 bytes).
    const Bytes flat = encodeExtra(Image(300, 300, Rgba8{10, 20, 30, 255}), ImageFormat::Qoi, {});
    CHECK(flat.size() < 1600);
}

TEST_CASE("TIFF has a valid header, directory and uncompressed RGBA strip") {
    Image image(5, 3, Rgba8{1, 2, 3, 4});
    image.set(4, 2, Rgba8{9, 8, 7, 255});
    const Bytes tiff = encodeExtra(image, ImageFormat::Tiff, {});
    REQUIRE(tiff.size() > 154);
    CHECK(text(Bytes(tiff.begin(), tiff.begin() + 2)) == "II");
    CHECK(le16(tiff, 2) == 42);
    const std::size_t ifd = le32(tiff, 4);
    const std::uint32_t entries = le16(tiff, ifd);
    CHECK(entries == 11);

    std::uint32_t width = 0, height = 0, offset = 0, count = 0, compression = 99, samples = 0;
    std::uint32_t previousTag = 0;
    for (std::uint32_t i = 0; i < entries; ++i) {
        const std::size_t e = ifd + 2 + i * 12;
        const std::uint32_t tag = le16(tiff, e);
        CHECK(tag > previousTag); // a directory is sorted by tag
        previousTag = tag;
        const std::uint32_t type = le16(tiff, e + 2);
        const std::uint32_t value = type == 3 ? le16(tiff, e + 8) : le32(tiff, e + 8);
        if (tag == 256) width = value;
        if (tag == 257) height = value;
        if (tag == 259) compression = value;
        if (tag == 273) offset = value;
        if (tag == 277) samples = value;
        if (tag == 279) count = value;
    }
    CHECK(width == 5);
    CHECK(height == 3);
    CHECK(compression == 1);
    CHECK(samples == 4);
    CHECK(count == 5 * 3 * 4);
    REQUIRE(offset + count == tiff.size());
    CHECK(tiff[offset] == 1);
    CHECK(tiff[offset + 3] == 4);
    const std::size_t last = offset + (2 * 5 + 4) * 4;
    CHECK(tiff[last] == 9);
    CHECK(tiff[last + 3] == 255);
}

TEST_CASE("ICO holds one PNG; a large or wide picture is shrunk onto a square") {
    const Bytes small = encodeExtra(Image(16, 16, Rgba8{255, 0, 0, 255}), ImageFormat::Ico, {});
    REQUIRE(small.size() > 22);
    CHECK(le16(small, 2) == 1);
    CHECK(le16(small, 4) == 1);
    CHECK(small[6] == 16);
    CHECK(small[7] == 16);
    CHECK(le32(small, 18) == 22);
    CHECK(le32(small, 14) == small.size() - 22);
    const LoadResult inner = decodeImage(Bytes(small.begin() + 22, small.end()));
    REQUIRE(inner.image.has_value());
    CHECK(inner.image->width() == 16);

    const Bytes big = encodeExtra(Image(1000, 400, Rgba8{0, 0, 255, 255}), ImageFormat::Ico, {});
    REQUIRE(big.size() > 22);
    CHECK(big[6] == 0); // 0 means 256
    CHECK(big[7] == 0);
    const LoadResult shrunk = decodeImage(Bytes(big.begin() + 22, big.end()));
    REQUIRE(shrunk.image.has_value());
    CHECK(shrunk.image->width() == 256);
    CHECK(shrunk.image->height() == 256);
    CHECK(shrunk.image->get(128, 128).b == 255);       // the picture, in the middle
    CHECK(shrunk.image->get(128, 128).a == 255);
    CHECK(shrunk.image->get(128, 5).a == 0);           // the padding stays transparent
}

TEST_CASE("PPM is a binary P6 flattened onto white") {
    Image image(2, 1);
    image.set(0, 0, Rgba8{10, 20, 30, 255});
    image.set(1, 0, Rgba8{0, 0, 0, 0});
    const Bytes ppm = encodeExtra(image, ImageFormat::Ppm, {});
    const std::string header = "P6\n2 1\n255\n";
    REQUIRE(ppm.size() == header.size() + 6);
    CHECK(text(Bytes(ppm.begin(), ppm.begin() + static_cast<std::ptrdiff_t>(header.size()))) == header);
    CHECK(ppm[header.size()] == 10);
    CHECK(ppm[header.size() + 3] == 255);
}

TEST_CASE("SVG embeds the picture, or draws pixel art as rectangles") {
    const Image image = mixedImage(20, 10);
    const Bytes embedded = encodeExtra(image, ImageFormat::Svg, {});
    CHECK(contains(embedded, "<svg"));
    CHECK(contains(embedded, "viewBox=\"0 0 20 10\""));
    CHECK(contains(embedded, "data:image/png;base64,iVBOR")); // a PNG signature
    CHECK_FALSE(contains(embedded, "<path"));

    Image art(4, 2);
    art.set(0, 0, Rgba8{255, 0, 0, 255});
    art.set(1, 0, Rgba8{255, 0, 0, 255});
    art.set(3, 1, Rgba8{0, 0, 255, 128});
    SaveOptions options;
    options.pixelArt = true;
    const Bytes vector = encodeExtra(art, ImageFormat::Svg, options);
    CHECK(contains(vector, "shape-rendering=\"crispEdges\""));
    CHECK(contains(vector, "fill=\"#ff0000\""));
    CHECK(contains(vector, "M0 0h2v1h-2z")); // two red pixels are one rectangle
    CHECK(contains(vector, "fill=\"#0000ff\" fill-opacity=\"0.502\""));
    CHECK(contains(vector, "M3 1h1v1h-1z"));
    CHECK_FALSE(contains(vector, "<image"));
}

TEST_CASE("PDF has a correct cross-reference table and one image page") {
    Image image = mixedImage(30, 20);
    for (const SaveOptions options : {SaveOptions{}, [] { SaveOptions o; o.dpi = 192; return o; }()}) {
        const Bytes pdf = encodeExtra(image, ImageFormat::Pdf, options);
        REQUIRE(pdf.size() > 100);
        const std::string s = text(pdf);
        CHECK(s.rfind("%PDF-1.4", 0) == 0);
        CHECK(s.find("%%EOF", s.size() - 8) != std::string::npos);
        CHECK(s.find("/SMask 6 0 R") != std::string::npos); // the image has transparency

        const std::size_t startxref = s.rfind("startxref\n");
        REQUIRE(startxref != std::string::npos);
        const std::size_t xref = static_cast<std::size_t>(std::stoul(s.substr(startxref + 10)));
        REQUIRE(s.compare(xref, 4, "xref") == 0);
        const int objects = std::stoi(s.substr(s.find(' ', xref + 5) + 1));
        CHECK(objects == 7); // the free entry and six objects
        std::size_t line = s.find('\n', s.find('\n', xref) + 1) + 1; // after "xref" and "0 7"
        line += 20;                                                  // the free entry
        for (int n = 1; n < objects; ++n, line += 20) {
            const std::size_t offset = static_cast<std::size_t>(std::stoul(s.substr(line, 10)));
            const std::string expected = std::to_string(n) + " 0 obj\n";
            CHECK(s.compare(offset, expected.size(), expected) == 0);
        }
    }
    // The page is the picture's size at the given resolution: 30 px at 192 dpi is 11.25 points.
    SaveOptions dense;
    dense.dpi = 192;
    CHECK(contains(encodeExtra(image, ImageFormat::Pdf, dense), "/MediaBox [0 0 11.25 7.50]"));
    // Opaque pictures need no soft mask.
    CHECK_FALSE(contains(encodeExtra(Image(4, 4, Rgba8{1, 2, 3, 255}), ImageFormat::Pdf, {}), "/SMask"));
    // A huge page is kept within the viewers' limit.
    SaveOptions tiny;
    tiny.dpi = 1;
    CHECK(contains(encodeExtra(Image(2000, 1000, Rgba8{1, 2, 3, 255}), ImageFormat::Pdf, tiny), "/MediaBox [0 0 14400.00 7200.00]"));
}

TEST_CASE("DOCX is a zip whose entries and checksums are intact") {
    const Image image = mixedImage(60, 30);
    const Bytes docx = encodeExtra(image, ImageFormat::Docx, {});
    REQUIRE(docx.size() > 200);
    REQUIRE(le32(docx, 0) == 0x04034B50u);

    // The end-of-central-directory record is the last 22 bytes (no comment).
    const std::size_t eocd = docx.size() - 22;
    REQUIRE(le32(docx, eocd) == 0x06054B50u);
    const std::uint32_t count = le16(docx, eocd + 10);
    CHECK(count == 5);
    std::size_t at = le32(docx, eocd + 16);

    std::vector<std::string> names;
    for (std::uint32_t i = 0; i < count; ++i) {
        REQUIRE(le32(docx, at) == 0x02014B50u);
        const std::uint32_t crc = le32(docx, at + 16);
        const std::uint32_t size = le32(docx, at + 20);
        const std::size_t nameLength = le16(docx, at + 28);
        const std::size_t local = le32(docx, at + 42);
        names.push_back(std::string(docx.begin() + static_cast<std::ptrdiff_t>(at + 46),
                                    docx.begin() + static_cast<std::ptrdiff_t>(at + 46 + nameLength)));

        REQUIRE(le32(docx, local) == 0x04034B50u);
        CHECK(le32(docx, local + 14) == crc);
        const std::size_t data = local + 30 + le16(docx, local + 26);
        REQUIRE(data + size <= docx.size());

        std::uint32_t check = 0xFFFFFFFFu;
        for (std::size_t k = 0; k < size; ++k) {
            check ^= docx[data + k];
            for (int bit = 0; bit < 8; ++bit)
                check = (check & 1u) ? (0xEDB88320u ^ (check >> 1)) : (check >> 1);
        }
        CHECK((check ^ 0xFFFFFFFFu) == crc);

        if (names.back() == "word/media/image1.png")
            CHECK(decodeImage(Bytes(docx.begin() + static_cast<std::ptrdiff_t>(data),
                                    docx.begin() + static_cast<std::ptrdiff_t>(data + size)))
                      .image.has_value());
        at += 46 + nameLength + le16(docx, at + 30) + le16(docx, at + 32);
    }
    CHECK(names.front() == "[Content_Types].xml");
    CHECK(contains(docx, "word/document.xml"));
    CHECK(contains(docx, "r:embed=\"rId1\""));
    CHECK(contains(docx, "<w:pgSz w:w=\"16838\" w:h=\"11906\" w:orient=\"landscape\"/>")); // wider than tall
    CHECK(contains(encodeExtra(Image(30, 60, Rgba8{1, 2, 3, 255}), ImageFormat::Docx, {}), "<w:pgSz w:w=\"11906\" w:h=\"16838\"/>"));
}

TEST_CASE("Every new format saves to a file and refuses an empty image") {
    const fs::path dir = fs::temp_directory_path() / "trace_extra_save";
    fs::create_directories(dir);
    const Image image = mixedImage(24, 16);
    for (const ImageFormat f : kAllFormats) {
        const fs::path path = dir / (std::string("pic.") + defaultExtension(f));
        REQUIRE_MESSAGE(saveImage(path, image, f).empty(), formatName(f));
        CHECK_MESSAGE(fs::file_size(path) > 20, formatName(f));
        CHECK_MESSAGE(encodeImage(image, f).size() == fs::file_size(path), formatName(f));
        CHECK_FALSE_MESSAGE(saveImage(path, Image(), f).empty(), formatName(f));
    }
    std::error_code ignored;
    fs::remove_all(dir, ignored);
}
