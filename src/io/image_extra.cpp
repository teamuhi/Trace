#include "io/image_extra.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace trace {

namespace {

using Bytes = std::vector<std::uint8_t>;

// --- byte helpers ------------------------------------------------------------------------------

void put16le(Bytes& out, unsigned v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

void put32le(Bytes& out, std::uint32_t v) {
    put16le(out, v & 0xFFFFu);
    put16le(out, v >> 16);
}

void put32be(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void putText(Bytes& out, std::string_view text) {
    out.insert(out.end(), text.begin(), text.end());
}

void putBytes(Bytes& out, const Bytes& more) {
    out.insert(out.end(), more.begin(), more.end());
}

bool sameColor(const Rgba8& a, const Rgba8& b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// "%.2f" style text. The program never changes the C locale, so the decimal separator is always a point.
std::string number(double value, const char* format = "%.2f") {
    std::array<char, 48> buffer{};
    std::snprintf(buffer.data(), buffer.size(), format, value);
    return buffer.data();
}

// Opaque RGB, transparent pixels composited onto white.
Bytes flattenOnWhite(const Image& image) {
    Bytes rgb;
    rgb.reserve(image.pixels().size() * 3);
    for (const Rgba8& p : image.pixels()) {
        const Rgba8 out = blendOver(Rgba8{255, 255, 255, 255}, p);
        rgb.push_back(out.r);
        rgb.push_back(out.g);
        rgb.push_back(out.b);
    }
    return rgb;
}

std::string base64(const Bytes& in) {
    static constexpr char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (static_cast<unsigned>(in[i]) << 16) | (static_cast<unsigned>(in[i + 1]) << 8) | in[i + 2];
        out += kDigits[(v >> 18) & 63u];
        out += kDigits[(v >> 12) & 63u];
        out += kDigits[(v >> 6) & 63u];
        out += kDigits[v & 63u];
    }
    if (i + 1 == in.size()) {
        const unsigned v = static_cast<unsigned>(in[i]) << 16;
        out += kDigits[(v >> 18) & 63u];
        out += kDigits[(v >> 12) & 63u];
        out += "==";
    } else if (i + 2 == in.size()) {
        const unsigned v = (static_cast<unsigned>(in[i]) << 16) | (static_cast<unsigned>(in[i + 1]) << 8);
        out += kDigits[(v >> 18) & 63u];
        out += kDigits[(v >> 12) & 63u];
        out += kDigits[(v >> 6) & 63u];
        out += '=';
    }
    return out;
}

// --- TIFF --------------------------------------------------------------------------------------

Bytes encodeTiff(const Image& image) {
    constexpr std::uint32_t kEntries = 11;
    constexpr std::uint32_t kIfdOffset = 8;
    constexpr std::uint32_t kBitsOffset = kIfdOffset + 2 + kEntries * 12 + 4; // after the directory
    constexpr std::uint32_t kPixelOffset = kBitsOffset + 8;                   // after BitsPerSample's four values

    const std::uint32_t width = static_cast<std::uint32_t>(image.width());
    const std::uint32_t height = static_cast<std::uint32_t>(image.height());
    const std::uint64_t pixelBytes = static_cast<std::uint64_t>(width) * height * 4u;
    if (pixelBytes + kPixelOffset > 0xFFFFFFFFull)
        return {};

    Bytes out;
    out.reserve(static_cast<std::size_t>(pixelBytes) + kPixelOffset);
    putText(out, "II");
    put16le(out, 42);
    put32le(out, kIfdOffset);

    put16le(out, kEntries);
    enum : unsigned { kShort = 3, kLong = 4 };
    const auto entry = [&out](unsigned tag, unsigned type, std::uint32_t count, std::uint32_t value) {
        put16le(out, tag);
        put16le(out, type);
        put32le(out, count);
        if (type == kShort && count == 1) {
            put16le(out, value);
            put16le(out, 0);
        } else {
            put32le(out, value);
        }
    };
    entry(256, kLong, 1, width);                       // ImageWidth
    entry(257, kLong, 1, height);                      // ImageLength
    entry(258, kShort, 4, kBitsOffset);                // BitsPerSample: 8, 8, 8, 8
    entry(259, kShort, 1, 1);                          // Compression: none
    entry(262, kShort, 1, 2);                          // PhotometricInterpretation: RGB
    entry(273, kLong, 1, kPixelOffset);                // StripOffsets: one strip
    entry(277, kShort, 1, 4);                          // SamplesPerPixel
    entry(278, kLong, 1, height);                      // RowsPerStrip
    entry(279, kLong, 1, static_cast<std::uint32_t>(pixelBytes)); // StripByteCounts
    entry(284, kShort, 1, 1);                          // PlanarConfiguration: chunky
    entry(338, kShort, 1, 2);                          // ExtraSamples: unassociated alpha
    put32le(out, 0);                                   // no further directory

    for (int i = 0; i < 4; ++i)
        put16le(out, 8);

    for (const Rgba8& p : image.pixels()) {
        out.push_back(p.r);
        out.push_back(p.g);
        out.push_back(p.b);
        out.push_back(p.a);
    }
    return out;
}

// --- ICO ---------------------------------------------------------------------------------------

constexpr int kMaxIconSide = 256;

// `image` shrunk to `width` x `height` with an area average on premultiplied color (so transparent pixels do not
// bleed their color into the edges).
Image shrunk(const Image& image, int width, int height) {
    Image out(width, height);
    const double stepX = static_cast<double>(image.width()) / width;
    const double stepY = static_cast<double>(image.height()) / height;
    for (int y = 0; y < height; ++y) {
        const double y0 = y * stepY;
        const double y1 = std::min((y + 1) * stepY, static_cast<double>(image.height()));
        for (int x = 0; x < width; ++x) {
            const double x0 = x * stepX;
            const double x1 = std::min((x + 1) * stepX, static_cast<double>(image.width()));
            double r = 0, g = 0, b = 0, a = 0, weight = 0;
            for (int sy = static_cast<int>(y0); sy < std::ceil(y1) && sy < image.height(); ++sy) {
                const double wy = std::min<double>(sy + 1, y1) - std::max<double>(sy, y0);
                for (int sx = static_cast<int>(x0); sx < std::ceil(x1) && sx < image.width(); ++sx) {
                    const double w = wy * (std::min<double>(sx + 1, x1) - std::max<double>(sx, x0));
                    const Rgba8 p = image.get(sx, sy);
                    const double alpha = p.a * w;
                    r += p.r * alpha;
                    g += p.g * alpha;
                    b += p.b * alpha;
                    a += alpha;
                    weight += w;
                }
            }
            if (weight <= 0.0 || a <= 0.0)
                continue; // stays transparent
            const auto channel = [a](double sum) { return static_cast<std::uint8_t>(std::clamp(sum / a + 0.5, 0.0, 255.0)); };
            out.set(x, y, Rgba8{channel(r), channel(g), channel(b),
                                static_cast<std::uint8_t>(std::clamp(a / weight + 0.5, 0.0, 255.0))});
        }
    }
    return out;
}

// The picture fitted into at most 256 x 256 and centred on a transparent square.
Image iconSquare(const Image& image) {
    const int longest = std::max(image.width(), image.height());
    int width = image.width();
    int height = image.height();
    Image fitted;
    if (longest > kMaxIconSide) {
        const double scale = static_cast<double>(kMaxIconSide) / longest;
        width = std::max(1, static_cast<int>(std::lround(image.width() * scale)));
        height = std::max(1, static_cast<int>(std::lround(image.height() * scale)));
        fitted = shrunk(image, width, height);
    }
    const Image& source = fitted.empty() ? image : fitted;

    const int side = std::max(width, height);
    if (width == height)
        return source;
    Image square(side, side);
    const int offsetX = (side - width) / 2;
    const int offsetY = (side - height) / 2;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            square.set(x + offsetX, y + offsetY, source.get(x, y));
    return square;
}

Bytes encodeIco(const Image& image) {
    const Image square = iconSquare(image);
    const Bytes png = encodeImage(square, ImageFormat::Png);
    if (png.empty())
        return {};
    const unsigned side = static_cast<unsigned>(square.width());

    Bytes out;
    put16le(out, 0); // reserved
    put16le(out, 1); // type: icon
    put16le(out, 1); // one image
    out.push_back(static_cast<std::uint8_t>(side >= 256 ? 0 : side)); // 0 means 256
    out.push_back(static_cast<std::uint8_t>(side >= 256 ? 0 : side));
    out.push_back(0); // no palette
    out.push_back(0);
    put16le(out, 1);  // color planes
    put16le(out, 32); // bits per pixel
    put32le(out, static_cast<std::uint32_t>(png.size()));
    put32le(out, 22); // the image follows the 6 + 16 byte directory
    putBytes(out, png);
    return out;
}

// --- QOI ---------------------------------------------------------------------------------------

Bytes encodeQoi(const Image& image) {
    Bytes out;
    putText(out, "qoif");
    put32be(out, static_cast<std::uint32_t>(image.width()));
    put32be(out, static_cast<std::uint32_t>(image.height()));
    out.push_back(4); // channels
    out.push_back(0); // sRGB with linear alpha

    std::array<Rgba8, 64> seen{};
    Rgba8 prev{0, 0, 0, 255};
    int run = 0;
    const std::span<const Rgba8> pixels = image.pixels();
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const Rgba8 p = pixels[i];
        if (sameColor(p, prev)) {
            ++run;
            if (run == 62 || i + 1 == pixels.size()) {
                out.push_back(static_cast<std::uint8_t>(0xC0 | (run - 1)));
                run = 0;
            }
            continue;
        }
        if (run > 0) {
            out.push_back(static_cast<std::uint8_t>(0xC0 | (run - 1)));
            run = 0;
        }
        const std::size_t slot = (p.r * 3u + p.g * 5u + p.b * 7u + p.a * 11u) % 64u;
        if (sameColor(seen[slot], p)) {
            out.push_back(static_cast<std::uint8_t>(slot)); // QOI_OP_INDEX
        } else {
            seen[slot] = p;
            if (p.a == prev.a) {
                const int dr = static_cast<std::int8_t>(p.r - prev.r);
                const int dg = static_cast<std::int8_t>(p.g - prev.g);
                const int db = static_cast<std::int8_t>(p.b - prev.b);
                const int drg = dr - dg;
                const int dbg = db - dg;
                if (dr > -3 && dr < 2 && dg > -3 && dg < 2 && db > -3 && db < 2) {
                    out.push_back(static_cast<std::uint8_t>(0x40 | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2)));
                } else if (drg > -9 && drg < 8 && dg > -33 && dg < 32 && dbg > -9 && dbg < 8) {
                    out.push_back(static_cast<std::uint8_t>(0x80 | (dg + 32)));
                    out.push_back(static_cast<std::uint8_t>(((drg + 8) << 4) | (dbg + 8)));
                } else {
                    out.push_back(0xFE);
                    out.push_back(p.r);
                    out.push_back(p.g);
                    out.push_back(p.b);
                }
            } else {
                out.push_back(0xFF);
                out.push_back(p.r);
                out.push_back(p.g);
                out.push_back(p.b);
                out.push_back(p.a);
            }
        }
        prev = p;
    }
    for (int i = 0; i < 7; ++i)
        out.push_back(0);
    out.push_back(1);
    return out;
}

// --- PPM ---------------------------------------------------------------------------------------

Bytes encodePpm(const Image& image) {
    Bytes out;
    putText(out, "P6\n" + std::to_string(image.width()) + " " + std::to_string(image.height()) + "\n255\n");
    putBytes(out, flattenOnWhite(image));
    return out;
}

// --- SVG ---------------------------------------------------------------------------------------

constexpr std::size_t kMaxSvgRuns = 400000;

std::string hexColor(const Rgba8& c) {
    std::array<char, 8> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "#%02x%02x%02x", c.r, c.g, c.b);
    return buffer.data();
}

// One path per color, each pixel run along a row a rectangle of it. Empty when there are more runs than
// kMaxSvgRuns (a photograph, not pixel art).
std::string pixelArtSvg(const Image& image) {
    std::map<std::uint32_t, std::string> paths; // by packed RGBA, so the order is stable
    std::size_t runs = 0;
    for (int y = 0; y < image.height(); ++y) {
        const std::span<const Rgba8> row = image.row(y);
        int x = 0;
        while (x < image.width()) {
            const Rgba8 p = row[static_cast<std::size_t>(x)];
            int end = x + 1;
            while (end < image.width() && sameColor(row[static_cast<std::size_t>(end)], p))
                ++end;
            if (p.a > 0) {
                if (++runs > kMaxSvgRuns)
                    return {};
                const std::uint32_t key = (static_cast<std::uint32_t>(p.r) << 24) | (static_cast<std::uint32_t>(p.g) << 16) |
                                          (static_cast<std::uint32_t>(p.b) << 8) | p.a;
                const int length = end - x;
                paths[key] += "M" + std::to_string(x) + " " + std::to_string(y) + "h" + std::to_string(length) + "v1h-" +
                              std::to_string(length) + "z";
            }
            x = end;
        }
    }

    const std::string w = std::to_string(image.width());
    const std::string h = std::to_string(image.height());
    std::string svg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + w +
                      "\" height=\"" + h + "\" viewBox=\"0 0 " + w + " " + h + "\" shape-rendering=\"crispEdges\">\n";
    for (const auto& [key, data] : paths) {
        const Rgba8 color{static_cast<std::uint8_t>(key >> 24), static_cast<std::uint8_t>((key >> 16) & 0xFFu),
                          static_cast<std::uint8_t>((key >> 8) & 0xFFu), static_cast<std::uint8_t>(key & 0xFFu)};
        svg += "<path fill=\"" + hexColor(color) + "\"";
        if (color.a != 255)
            svg += " fill-opacity=\"" + number(color.a / 255.0, "%.3f") + "\"";
        svg += " d=\"" + data + "\"/>\n";
    }
    svg += "</svg>\n";
    return svg;
}

Bytes encodeSvg(const Image& image, bool pixelArt) {
    std::string svg;
    if (pixelArt)
        svg = pixelArtSvg(image);
    if (svg.empty()) {
        const Bytes png = encodeImage(image, ImageFormat::Png);
        if (png.empty())
            return {};
        const std::string w = std::to_string(image.width());
        const std::string h = std::to_string(image.height());
        svg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" "
              "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"" + w + "\" height=\"" + h + "\" viewBox=\"0 0 " + w +
              " " + h + "\">\n<image width=\"" + w + "\" height=\"" + h + "\"" +
              (pixelArt ? " style=\"image-rendering:pixelated\"" : "") + " xlink:href=\"data:image/png;base64," + base64(png) +
              "\"/>\n</svg>\n";
    }
    return Bytes(svg.begin(), svg.end());
}

// --- PDF ---------------------------------------------------------------------------------------

constexpr double kMaxPdfPoints = 14400.0; // 200 inches, the limit most viewers honor

Bytes encodePdf(const Image& image, int dpi) {
    dpi = std::clamp(dpi, 1, 2400);
    const double width = image.width();
    const double height = image.height();
    double pageW = width * 72.0 / dpi;
    double pageH = height * 72.0 / dpi;
    const double over = std::max(pageW, pageH) / kMaxPdfPoints;
    if (over > 1.0) {
        pageW /= over;
        pageH /= over;
    }

    bool translucent = false;
    Bytes rgb;
    rgb.reserve(image.pixels().size() * 3);
    Bytes alpha;
    alpha.reserve(image.pixels().size());
    for (const Rgba8& p : image.pixels()) {
        rgb.push_back(p.r);
        rgb.push_back(p.g);
        rgb.push_back(p.b);
        alpha.push_back(p.a);
        translucent = translucent || p.a != 255;
    }
    const Bytes packedColor = zlibCompress(rgb.data(), rgb.size());
    if (packedColor.empty())
        return {};
    Bytes packedAlpha;
    if (translucent) {
        packedAlpha = zlibCompress(alpha.data(), alpha.size());
        if (packedAlpha.empty())
            return {};
    }

    Bytes out;
    std::vector<std::size_t> offsets; // object number n starts at offsets[n - 1]
    const auto begin = [&] {
        offsets.push_back(out.size());
        putText(out, std::to_string(offsets.size()) + " 0 obj\n");
    };
    const auto stream = [&](const std::string& dictionary, const Bytes& data) {
        putText(out, "<< " + dictionary + " /Length " + std::to_string(data.size()) + " >>\nstream\n");
        putBytes(out, data);
        putText(out, "\nendstream\nendobj\n");
    };

    putText(out, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
    begin();
    putText(out, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    begin();
    putText(out, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
    begin();
    putText(out, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + number(pageW) + " " + number(pageH) +
                     "] /Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 0 R >>\nendobj\n");

    const std::string content = "q\n" + number(pageW) + " 0 0 " + number(pageH) + " 0 0 cm\n/Im0 Do\nQ\n";
    begin();
    stream("", Bytes(content.begin(), content.end()));

    const std::string size = " /Width " + std::to_string(image.width()) + " /Height " + std::to_string(image.height());
    begin();
    stream("/Type /XObject /Subtype /Image" + size + " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode" +
               (translucent ? " /SMask 6 0 R" : ""),
           packedColor);
    if (translucent) {
        begin();
        stream("/Type /XObject /Subtype /Image" + size + " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode",
               packedAlpha);
    }

    const std::size_t xref = out.size();
    putText(out, "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n");
    for (const std::size_t offset : offsets) {
        std::array<char, 32> line{};
        std::snprintf(line.data(), line.size(), "%010zu 00000 n \n", offset);
        putText(out, line.data());
    }
    putText(out, "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
                     std::to_string(xref) + "\n%%EOF\n");
    return out;
}

// --- DOCX (a zip of XML parts, stored without compression: the picture is a PNG already) -------

std::uint32_t crc32(const Bytes& data) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[n] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::uint8_t byte : data)
        crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

struct ZipEntry {
    std::string name;
    Bytes data;
};

Bytes zipStored(const std::vector<ZipEntry>& entries) {
    constexpr unsigned kDosDate = 0x0021; // 1980-01-01, so the file does not depend on the clock
    Bytes out;
    std::vector<std::uint32_t> offsets;
    for (const ZipEntry& entry : entries) {
        if (entry.data.size() > 0x7FFFFFFFu || out.size() > 0x7FFFFFFFu)
            return {}; // would need ZIP64
        offsets.push_back(static_cast<std::uint32_t>(out.size()));
        put32le(out, 0x04034B50u);
        put16le(out, 20); // version needed
        put16le(out, 0);  // flags
        put16le(out, 0);  // stored
        put16le(out, 0);  // time
        put16le(out, kDosDate);
        put32le(out, crc32(entry.data));
        put32le(out, static_cast<std::uint32_t>(entry.data.size()));
        put32le(out, static_cast<std::uint32_t>(entry.data.size()));
        put16le(out, static_cast<unsigned>(entry.name.size()));
        put16le(out, 0); // no extra field
        putText(out, entry.name);
        putBytes(out, entry.data);
    }
    const std::uint32_t directory = static_cast<std::uint32_t>(out.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& entry = entries[i];
        put32le(out, 0x02014B50u);
        put16le(out, 20); // version made by
        put16le(out, 20); // version needed
        put16le(out, 0);
        put16le(out, 0);
        put16le(out, 0);
        put16le(out, kDosDate);
        put32le(out, crc32(entry.data));
        put32le(out, static_cast<std::uint32_t>(entry.data.size()));
        put32le(out, static_cast<std::uint32_t>(entry.data.size()));
        put16le(out, static_cast<unsigned>(entry.name.size()));
        put16le(out, 0); // extra
        put16le(out, 0); // comment
        put16le(out, 0); // disk
        put16le(out, 0); // internal attributes
        put32le(out, 0); // external attributes
        put32le(out, offsets[i]);
        putText(out, entry.name);
    }
    const std::uint32_t directorySize = static_cast<std::uint32_t>(out.size()) - directory;
    put32le(out, 0x06054B50u);
    put16le(out, 0);
    put16le(out, 0);
    put16le(out, static_cast<unsigned>(entries.size()));
    put16le(out, static_cast<unsigned>(entries.size()));
    put32le(out, directorySize);
    put32le(out, directory);
    put16le(out, 0);
    return out;
}

ZipEntry textEntry(std::string name, const std::string& text) {
    return ZipEntry{std::move(name), Bytes(text.begin(), text.end())};
}

Bytes encodeDocx(const Image& image, int dpi) {
    const Bytes png = encodeImage(image, ImageFormat::Png);
    if (png.empty())
        return {};
    dpi = std::clamp(dpi, 1, 2400);

    // A4 with 0.75 inch margins, turned to landscape for a picture that is wider than tall. Sizes in twips
    // (1/1440 inch); the picture's own size is in EMU (914400 per inch = 635 per twip), shrunk to fit the page.
    constexpr double kShortSide = 11906.0, kLongSide = 16838.0, kMargin = 1080.0, kSlack = 160.0;
    const bool landscape = image.width() > image.height();
    const double pageW = landscape ? kLongSide : kShortSide;
    const double pageH = landscape ? kShortSide : kLongSide;
    const double naturalW = image.width() * 1440.0 / dpi;
    const double naturalH = image.height() * 1440.0 / dpi;
    const double fit = std::min({1.0, (pageW - 2 * kMargin) / naturalW, (pageH - 2 * kMargin - kSlack) / naturalH});
    const std::string cx = std::to_string(std::max<long long>(1, std::llround(naturalW * fit * 635.0)));
    const std::string cy = std::to_string(std::max<long long>(1, std::llround(naturalH * fit * 635.0)));

    const std::string header = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    const std::string document =
        header +
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><w:body>"
        "<w:p><w:pPr><w:spacing w:before=\"0\" w:after=\"0\"/><w:jc w:val=\"center\"/></w:pPr><w:r><w:drawing>"
        "<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"" + cx + "\" cy=\"" + cy +
        "\"/><wp:docPr id=\"1\" name=\"Picture 1\"/><wp:cNvGraphicFramePr>"
        "<a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
        "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic>"
        "<pic:nvPicPr><pic:cNvPr id=\"0\" name=\"image1.png\"/><pic:cNvPicPr/></pic:nvPicPr>"
        "<pic:blipFill><a:blip r:embed=\"rId1\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
        "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" + cx + "\" cy=\"" + cy +
        "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>"
        "</a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>"
        "<w:sectPr><w:pgSz w:w=\"" + std::to_string(static_cast<int>(pageW)) + "\" w:h=\"" +
        std::to_string(static_cast<int>(pageH)) + "\"" + (landscape ? " w:orient=\"landscape\"" : "") +
        "/><w:pgMar w:top=\"1080\" w:right=\"1080\" w:bottom=\"1080\" w:left=\"1080\" w:header=\"720\" "
        "w:footer=\"720\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>";

    std::vector<ZipEntry> entries;
    entries.push_back(textEntry(
        "[Content_Types].xml",
        header + "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                 "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                 "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                 "<Default Extension=\"png\" ContentType=\"image/png\"/>"
                 "<Override PartName=\"/word/document.xml\" "
                 "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
                 "</Types>"));
    entries.push_back(textEntry(
        "_rels/.rels",
        header + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                 "<Relationship Id=\"rId1\" "
                 "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
                 "Target=\"word/document.xml\"/></Relationships>"));
    entries.push_back(textEntry("word/document.xml", document));
    entries.push_back(textEntry(
        "word/_rels/document.xml.rels",
        header + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                 "<Relationship Id=\"rId1\" "
                 "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
                 "Target=\"media/image1.png\"/></Relationships>"));
    entries.push_back(ZipEntry{"word/media/image1.png", png});
    return zipStored(entries);
}

} // namespace

std::vector<std::uint8_t> encodeExtra(const Image& image, ImageFormat format, const SaveOptions& options) {
    if (image.empty())
        return {};
    try {
        switch (format) {
        case ImageFormat::Tiff:
            return encodeTiff(image);
        case ImageFormat::Ico:
            return encodeIco(image);
        case ImageFormat::Qoi:
            return encodeQoi(image);
        case ImageFormat::Ppm:
            return encodePpm(image);
        case ImageFormat::Svg:
            return encodeSvg(image, options.pixelArt);
        case ImageFormat::Pdf:
            return encodePdf(image, options.dpi);
        case ImageFormat::Docx:
            return encodeDocx(image, options.dpi);
        case ImageFormat::Png:
        case ImageFormat::Jpeg:
        case ImageFormat::Bmp:
        case ImageFormat::Tga:
        case ImageFormat::Gif:
            break;
        }
    } catch (const std::exception&) {
        // out of memory: reported as a failed save by the caller
    }
    return {};
}

} // namespace trace
