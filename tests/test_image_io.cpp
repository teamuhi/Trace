#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include <msf_gif.h>

#include "io/image_io.hpp"
#include "io/path_utf8.hpp"

using namespace trace;
namespace fs = std::filesystem;

namespace {

constexpr Rgba8 kWhite{255, 255, 255, 255};
constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kGreen{0, 255, 0, 255};
constexpr Rgba8 kBlue{0, 0, 255, 255};

// A scratch directory that disappears with the test.
struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        std::random_device rd;
        path = fs::temp_directory_path() / ("trace_io_test_" + std::to_string(rd()) + "_" + std::to_string(++counter));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path operator/(const std::string& name) const { return path / name; }
};

// Four flat colors, one transparent corner and a half-transparent pixel.
Image sampleImage() {
    Image img(40, 30, kWhite);
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 40; ++x) {
            if (x < 20 && y < 15)
                img.set(x, y, kRed);
            else if (x >= 20 && y < 15)
                img.set(x, y, kGreen);
            else if (x < 20)
                img.set(x, y, kBlue);
        }
    }
    return img;
}

std::vector<unsigned char> readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeAll(const fs::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

int channelDiff(Rgba8 a, Rgba8 b) {
    return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b), std::abs(a.a - b.a)});
}

} // namespace

TEST_CASE("formatFromPath reads the extension case-insensitively") {
    CHECK(formatFromPath("a.png") == ImageFormat::Png);
    CHECK(formatFromPath("a.PNG") == ImageFormat::Png);
    CHECK(formatFromPath("a.jpg") == ImageFormat::Jpeg);
    CHECK(formatFromPath("a.JPEG") == ImageFormat::Jpeg);
    CHECK(formatFromPath("a.bmp") == ImageFormat::Bmp);
    CHECK(formatFromPath("a.tga") == ImageFormat::Tga);
    CHECK(formatFromPath("a.Gif") == ImageFormat::Gif);
    CHECK(formatFromPath("archive.tar.png") == ImageFormat::Png);
    CHECK_FALSE(formatFromPath("a.txt").has_value());
    CHECK_FALSE(formatFromPath("png").has_value());
    CHECK_FALSE(formatFromPath("").has_value());
    CHECK_FALSE(formatFromPath("dir.png/file").has_value());
}

TEST_CASE("Format metadata") {
    for (const ImageFormat f : {ImageFormat::Png, ImageFormat::Jpeg, ImageFormat::Bmp, ImageFormat::Tga, ImageFormat::Gif}) {
        CHECK(std::string(formatName(f)).size() > 0);
        const std::string ext = defaultExtension(f);
        CHECK(formatFromPath("x." + ext) == f);
    }
    CHECK_FALSE(formatSupportsAlpha(ImageFormat::Jpeg));
    CHECK(formatSupportsAlpha(ImageFormat::Png));
}

TEST_CASE("PNG round-trips every pixel exactly, including alpha") {
    TempDir dir;
    Image img = sampleImage();
    img.set(30, 25, Rgba8{12, 34, 56, 0});
    img.set(31, 25, Rgba8{200, 100, 50, 128});

    REQUIRE(saveImage(dir / "a.png", img, ImageFormat::Png).empty());
    LoadResult loaded = loadImage(dir / "a.png");
    REQUIRE(loaded.image.has_value());
    REQUIRE(loaded.image->width() == 40);
    REQUIRE(loaded.image->height() == 30);
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 40; ++x)
            REQUIRE(loaded.image->get(x, y) == img.get(x, y));
}

TEST_CASE("TGA round-trips exactly, including alpha") {
    TempDir dir;
    Image img = sampleImage();
    img.set(5, 20, Rgba8{12, 34, 56, 0});
    img.set(6, 20, Rgba8{200, 100, 50, 128});

    REQUIRE(saveImage(dir / "a.tga", img, ImageFormat::Tga).empty());
    LoadResult loaded = loadImage(dir / "a.tga");
    REQUIRE(loaded.image.has_value());
    CHECK(loaded.image->get(0, 0) == kRed);
    CHECK(loaded.image->get(25, 5) == kGreen);
    CHECK(loaded.image->get(6, 20).a == 128);
    CHECK(loaded.image->get(5, 20).a == 0);
}

TEST_CASE("BMP round-trips the opaque colors") {
    TempDir dir;
    const Image img = sampleImage();
    REQUIRE(saveImage(dir / "a.bmp", img, ImageFormat::Bmp).empty());
    LoadResult loaded = loadImage(dir / "a.bmp");
    REQUIRE(loaded.image.has_value());
    REQUIRE(loaded.image->width() == 40);
    for (int y = 0; y < 30; y += 7)
        for (int x = 0; x < 40; x += 7)
            CHECK(channelDiff(loaded.image->get(x, y), img.get(x, y)) == 0);
}

TEST_CASE("JPEG is lossy but close, and transparency is flattened onto white") {
    TempDir dir;
    Image img(32, 32, kRed);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            img.set(x, y, Rgba8{0, 0, 0, 0}); // fully transparent corner

    SaveOptions options;
    options.jpegQuality = 95;
    REQUIRE(saveImage(dir / "a.jpg", img, ImageFormat::Jpeg, options).empty());
    LoadResult loaded = loadImage(dir / "a.jpg");
    REQUIRE(loaded.image.has_value());

    const Rgba8 center = loaded.image->get(20, 20);
    CHECK(channelDiff(center, kRed) < 25);
    CHECK(center.a == 255);
    CHECK(loaded.image->get(2, 2).r > 230); // flattened to white, not black
    CHECK(loaded.image->get(2, 2).g > 230);
}

TEST_CASE("JPEG quality controls file size, and out-of-range qualities are clamped") {
    TempDir dir;
    Image noisy(128, 128);
    std::mt19937 rng(1234);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            noisy.set(x, y, Rgba8{static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng()),
                                  static_cast<std::uint8_t>(rng()), 255});

    SaveOptions low;
    low.jpegQuality = 10;
    SaveOptions high;
    high.jpegQuality = 95;
    REQUIRE(saveImage(dir / "low.jpg", noisy, ImageFormat::Jpeg, low).empty());
    REQUIRE(saveImage(dir / "high.jpg", noisy, ImageFormat::Jpeg, high).empty());
    CHECK(fs::file_size(dir / "low.jpg") < fs::file_size(dir / "high.jpg"));

    SaveOptions silly;
    silly.jpegQuality = -50;
    CHECK(saveImage(dir / "neg.jpg", noisy, ImageFormat::Jpeg, silly).empty());
    silly.jpegQuality = 100000;
    CHECK(saveImage(dir / "huge.jpg", noisy, ImageFormat::Jpeg, silly).empty());
    CHECK(loadImage(dir / "neg.jpg").image.has_value());
    CHECK(loadImage(dir / "huge.jpg").image.has_value());
}

TEST_CASE("GIF round-trips flat colors and keeps on/off transparency") {
    TempDir dir;
    Image img = sampleImage();
    for (int y = 20; y < 30; ++y)
        for (int x = 25; x < 40; ++x)
            img.set(x, y, Rgba8{0, 0, 0, 0}); // transparent block
    img.set(22, 22, Rgba8{255, 0, 0, 200});    // mostly opaque: stays opaque
    img.set(23, 22, Rgba8{255, 0, 0, 40});     // mostly transparent: becomes transparent

    REQUIRE(saveImage(dir / "a.gif", img, ImageFormat::Gif).empty());
    LoadResult loaded = loadImage(dir / "a.gif");
    REQUIRE(loaded.image.has_value());
    REQUIRE(loaded.image->width() == 40);
    REQUIRE(loaded.image->height() == 30);

    CHECK(channelDiff(loaded.image->get(2, 2), kRed) <= 8);
    CHECK(channelDiff(loaded.image->get(30, 2), kGreen) <= 8);
    CHECK(channelDiff(loaded.image->get(2, 25), kBlue) <= 8);
    CHECK(loaded.image->get(30, 25).a == 0);
    CHECK(loaded.image->get(22, 22).a == 255);
    CHECK(loaded.image->get(23, 22).a == 0);
}

TEST_CASE("A fully opaque GIF has no accidental transparency") {
    TempDir dir;
    const Image img = sampleImage();
    REQUIRE(saveImage(dir / "a.gif", img, ImageFormat::Gif).empty());
    LoadResult loaded = loadImage(dir / "a.gif");
    REQUIRE(loaded.image.has_value());
    for (const Rgba8& p : loaded.image->pixels())
        REQUIRE(p.a == 255);
}

TEST_CASE("Tiny and one-pixel images save and load in every format") {
    TempDir dir;
    for (const ImageFormat f : {ImageFormat::Png, ImageFormat::Jpeg, ImageFormat::Bmp, ImageFormat::Tga, ImageFormat::Gif}) {
        const Image one(1, 1, kGreen);
        const fs::path path = dir / (std::string("one.") + defaultExtension(f));
        REQUIRE_MESSAGE(saveImage(path, one, f).empty(), formatName(f));
        LoadResult loaded = loadImage(path);
        REQUIRE_MESSAGE(loaded.image.has_value(), formatName(f));
        CHECK(loaded.image->width() == 1);
        CHECK(loaded.image->height() == 1);
        CHECK(channelDiff(loaded.image->get(0, 0), kGreen) <= 40); // JPEG chroma may shift it a little

        const Image wide(300, 1, kBlue);
        const fs::path wpath = dir / (std::string("wide.") + defaultExtension(f));
        REQUIRE_MESSAGE(saveImage(wpath, wide, f).empty(), formatName(f));
        LoadResult wloaded = loadImage(wpath);
        REQUIRE_MESSAGE(wloaded.image.has_value(), formatName(f));
        CHECK(wloaded.image->width() == 300);
        CHECK(wloaded.image->height() == 1);
    }
}

TEST_CASE("Images with odd sizes keep their dimensions (stride handling)") {
    TempDir dir;
    for (const int w : {1, 2, 3, 5, 7, 13, 61}) {
        for (const ImageFormat f : {ImageFormat::Png, ImageFormat::Bmp, ImageFormat::Tga, ImageFormat::Gif}) {
            Image img(w, 5, kWhite);
            img.set(w - 1, 4, kRed);
            const fs::path path = dir / ("odd" + std::to_string(w) + "." + defaultExtension(f));
            REQUIRE_MESSAGE(saveImage(path, img, f).empty(), formatName(f));
            LoadResult loaded = loadImage(path);
            REQUIRE_MESSAGE(loaded.image.has_value(), formatName(f));
            REQUIRE(loaded.image->width() == w);
            REQUIRE(loaded.image->height() == 5);
            CHECK_MESSAGE(channelDiff(loaded.image->get(w - 1, 4), kRed) <= 8, formatName(f) << " w=" << w);
            CHECK_MESSAGE(channelDiff(loaded.image->get(0, 0), kWhite) <= 8, formatName(f) << " w=" << w);
        }
    }
}

TEST_CASE("Files are decoded by content, not by extension") {
    TempDir dir;
    REQUIRE(saveImage(dir / "real.png", sampleImage(), ImageFormat::Png).empty());
    fs::copy_file(dir / "real.png", dir / "liar.jpg");
    fs::copy_file(dir / "real.png", dir / "noext");

    LoadResult a = loadImage(dir / "liar.jpg");
    LoadResult b = loadImage(dir / "noext");
    REQUIRE(a.image.has_value());
    REQUIRE(b.image.has_value());
    CHECK(a.image->get(0, 0) == kRed);
}

TEST_CASE("Non-ASCII file names work") {
    TempDir dir;
    const fs::path path = dir.path / fs::path(u8"テスト_тест_é.png");
    REQUIRE(saveImage(path, sampleImage(), ImageFormat::Png).empty());
    CHECK(fs::exists(path));
    LoadResult loaded = loadImage(path);
    REQUIRE(loaded.image.has_value());
    CHECK(loaded.image->get(0, 0) == kRed);
}

// --- failures never crash and always explain themselves -------------------------------------------

TEST_CASE("Loading a missing file fails with a message") {
    TempDir dir;
    LoadResult r = loadImage(dir / "nothing.png");
    CHECK_FALSE(r.image.has_value());
    CHECK_FALSE(r.error.empty());
    CHECK(r.error.find("nothing.png") != std::string::npos);
}

TEST_CASE("Loading a directory fails with a message") {
    TempDir dir;
    LoadResult r = loadImage(dir.path);
    CHECK_FALSE(r.image.has_value());
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("An empty file is rejected") {
    TempDir dir;
    writeAll(dir / "empty.png", {});
    LoadResult r = loadImage(dir / "empty.png");
    CHECK_FALSE(r.image.has_value());
    CHECK(r.error.find("empty") != std::string::npos);
}

TEST_CASE("Random bytes and text are rejected") {
    TempDir dir;
    std::vector<unsigned char> junk(5000);
    std::mt19937 rng(99);
    for (auto& b : junk)
        b = static_cast<unsigned char>(rng());
    writeAll(dir / "junk.png", junk);
    CHECK_FALSE(loadImage(dir / "junk.png").image.has_value());

    const std::string text = "this is definitely not an image";
    writeAll(dir / "text.gif", std::vector<unsigned char>(text.begin(), text.end()));
    CHECK_FALSE(loadImage(dir / "text.gif").image.has_value());
}

TEST_CASE("Truncated files of every format fail cleanly") {
    TempDir dir;
    for (const ImageFormat f : {ImageFormat::Png, ImageFormat::Jpeg, ImageFormat::Bmp, ImageFormat::Tga, ImageFormat::Gif}) {
        const fs::path path = dir / (std::string("full.") + defaultExtension(f));
        REQUIRE(saveImage(path, sampleImage(), f).empty());
        const std::vector<unsigned char> bytes = readAll(path);
        REQUIRE(bytes.size() > 40);

        for (const std::size_t keep : {std::size_t{0}, std::size_t{4}, std::size_t{20}, bytes.size() / 2, bytes.size() - 1}) {
            writeAll(dir / "cut.bin", std::vector<unsigned char>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep)));
            LoadResult r = loadImage(dir / "cut.bin"); // may succeed with a partial image or fail; must not crash
            if (!r.image.has_value())
                CHECK_FALSE(r.error.empty());
        }
    }
}

TEST_CASE("Corrupted bytes never crash the decoder") {
    TempDir dir;
    REQUIRE(saveImage(dir / "base.png", sampleImage(), ImageFormat::Png).empty());
    const std::vector<unsigned char> base = readAll(dir / "base.png");

    std::mt19937 rng(7);
    for (int round = 0; round < 60; ++round) {
        std::vector<unsigned char> bytes = base;
        for (int i = 0; i < 4; ++i)
            bytes[rng() % bytes.size()] = static_cast<unsigned char>(rng());
        writeAll(dir / "fuzz.png", bytes);
        LoadResult r = loadImage(dir / "fuzz.png");
        if (!r.image.has_value())
            CHECK_FALSE(r.error.empty());
    }
}

TEST_CASE("Images declaring absurd dimensions are refused before allocating") {
    TempDir dir;
    REQUIRE(saveImage(dir / "small.png", Image(4, 4, kWhite), ImageFormat::Png).empty());
    std::vector<unsigned char> bytes = readAll(dir / "small.png");
    REQUIRE(bytes.size() > 24);

    // PNG layout: signature (8), IHDR length (4), "IHDR" (4), width (4), height (4).
    bytes[16] = 0x7f;
    bytes[17] = 0xff;
    bytes[18] = 0xff;
    bytes[19] = 0xff; // width = 2^31 - 1
    writeAll(dir / "huge.png", bytes);

    LoadResult r = loadImage(dir / "huge.png");
    CHECK_FALSE(r.image.has_value());
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("Saving into a folder that does not exist fails without a crash") {
    TempDir dir;
    const std::string error = saveImage(dir / "missing" / "a.png", sampleImage(), ImageFormat::Png);
    CHECK_FALSE(error.empty());
    CHECK_FALSE(fs::exists(dir / "missing"));
}

TEST_CASE("Saving over an existing file replaces it and leaves no temporary file behind") {
    TempDir dir;
    REQUIRE(saveImage(dir / "a.png", Image(10, 10, kRed), ImageFormat::Png).empty());
    REQUIRE(saveImage(dir / "a.png", Image(20, 5, kBlue), ImageFormat::Png).empty());

    LoadResult r = loadImage(dir / "a.png");
    REQUIRE(r.image.has_value());
    CHECK(r.image->width() == 20);
    CHECK(r.image->get(0, 0) == kBlue);

    int files = 0;
    for (const auto& entry : fs::directory_iterator(dir.path)) {
        (void)entry;
        ++files;
    }
    CHECK(files == 1);
}

TEST_CASE("A failed save leaves the original file untouched") {
    TempDir dir;
    REQUIRE(saveImage(dir / "keep.png", Image(10, 10, kRed), ImageFormat::Png).empty());
    const std::vector<unsigned char> before = readAll(dir / "keep.png");

    // An empty image cannot be saved; the existing file must survive the attempt.
    CHECK_FALSE(saveImage(dir / "keep.png", Image(), ImageFormat::Png).empty());
    CHECK(readAll(dir / "keep.png") == before);
}

TEST_CASE("Saving to a path that is a directory fails cleanly") {
    TempDir dir;
    fs::create_directory(dir / "folder.png");
    CHECK_FALSE(saveImage(dir / "folder.png", sampleImage(), ImageFormat::Png).empty());
    CHECK(fs::is_directory(dir / "folder.png"));
}

TEST_CASE("Loading a real PNG larger than a tile and a typical canvas") {
    TempDir dir;
    Image img(800, 600, kWhite);
    for (int i = 0; i < 600; ++i)
        img.set(i, i, kRed);
    REQUIRE(saveImage(dir / "canvas.png", img, ImageFormat::Png).empty());
    LoadResult r = loadImage(dir / "canvas.png");
    REQUIRE(r.image.has_value());
    CHECK(r.image->get(300, 300) == kRed);
    CHECK(r.image->get(301, 300) == kWhite);
}

// --- GIF frame counting ----------------------------------------------------------------------------

TEST_CASE("A still GIF reports one frame and an animated GIF several") {
    TempDir dir;
    REQUIRE(saveImage(dir / "still.gif", sampleImage(), ImageFormat::Gif).empty());
    CHECK(loadImage(dir / "still.gif").frameCount == 1);

    std::vector<std::uint8_t> red(8 * 8 * 4, 0);
    std::vector<std::uint8_t> green(8 * 8 * 4, 0);
    for (std::size_t i = 0; i < red.size(); i += 4) {
        red[i] = 255;
        red[i + 3] = 255;
        green[i + 1] = 255;
        green[i + 3] = 255;
    }
    MsfGifState state = {};
    REQUIRE(msf_gif_begin(&state, 8, 8) != 0);
    REQUIRE(msf_gif_frame(&state, red.data(), 10, 16, 32) != 0);
    REQUIRE(msf_gif_frame(&state, green.data(), 10, 16, 32) != 0);
    REQUIRE(msf_gif_frame(&state, red.data(), 10, 16, 32) != 0);
    MsfGifResult result = msf_gif_end(&state);
    REQUIRE(result.data != nullptr);
    writeAll(dir / "anim.gif", std::vector<unsigned char>(static_cast<unsigned char*>(result.data),
                                                         static_cast<unsigned char*>(result.data) + result.dataSize));
    msf_gif_free(result);

    LoadResult anim = loadImage(dir / "anim.gif");
    REQUIRE(anim.image.has_value());
    CHECK(anim.frameCount == 3);
    CHECK(anim.image->width() == 8);
}

TEST_CASE("Non-GIF files and mangled GIFs report a single frame without crashing") {
    TempDir dir;
    REQUIRE(saveImage(dir / "a.png", sampleImage(), ImageFormat::Png).empty());
    CHECK(loadImage(dir / "a.png").frameCount == 1);

    REQUIRE(saveImage(dir / "a.gif", sampleImage(), ImageFormat::Gif).empty());
    const std::vector<unsigned char> bytes = readAll(dir / "a.gif");
    std::mt19937 rng(5);
    for (int round = 0; round < 80; ++round) {
        std::vector<unsigned char> mangled(bytes.begin(),
                                           bytes.begin() + static_cast<std::ptrdiff_t>(rng() % bytes.size()));
        for (int i = 0; i < 3 && !mangled.empty(); ++i)
            mangled[rng() % mangled.size()] = static_cast<unsigned char>(rng());
        writeAll(dir / "m.gif", mangled);
        const LoadResult r = loadImage(dir / "m.gif");
        CHECK(r.frameCount >= 1);
    }
}

// --- save targets and paths ------------------------------------------------------------------------

TEST_CASE("resolveSaveTarget keeps a recognised extension and appends one otherwise") {
    const SaveTarget a = resolveSaveTarget("holiday.JPG", ImageFormat::Png);
    CHECK(a.format == ImageFormat::Jpeg);
    CHECK_FALSE(a.extensionAdded);
    CHECK(a.path == fs::path("holiday.JPG"));

    const SaveTarget b = resolveSaveTarget("holiday", ImageFormat::Gif);
    CHECK(b.format == ImageFormat::Gif);
    CHECK(b.extensionAdded);
    CHECK(b.path == fs::path("holiday.gif"));

    const SaveTarget c = resolveSaveTarget("notes.txt", ImageFormat::Bmp);
    CHECK(c.format == ImageFormat::Bmp);
    CHECK(c.extensionAdded);
    CHECK(c.path == fs::path("notes.txt.bmp"));

    const SaveTarget d = resolveSaveTarget("v1.2 final", ImageFormat::Png);
    CHECK(d.path.filename() == fs::path("v1.2 final.png"));
}

TEST_CASE("Every format listed for the save dialog has a name and an extension that maps back") {
    for (const ImageFormat f : kAllFormats) {
        CHECK(formatFromPath(std::string("x.") + defaultExtension(f)) == f);
        CHECK(std::string(formatName(f)).size() >= 3);
    }
}

TEST_CASE("UTF-8 path conversion round-trips non-ASCII text") {
    const std::string utf8 = "caf\xc3\xa9/\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88.png";
    const fs::path path = pathFromUtf8(utf8);
    CHECK(utf8FromPath(path) == utf8);
    CHECK(pathFromUtf8("").empty());
    CHECK(utf8FromPath(fs::path()).empty());
}

TEST_CASE("Errors about non-ASCII file names do not throw") {
    TempDir dir;
    const fs::path missing = dir.path / fs::path(u8"\u30c6\u30b9\u30c8_\u0442\u0435\u0441\u0442.png");
    LoadResult r = loadImage(missing);
    CHECK_FALSE(r.image.has_value());
    CHECK_FALSE(r.error.empty());
    CHECK_FALSE(saveImage(dir.path / "nope" / fs::path(u8"\u30c6.png"), sampleImage(), ImageFormat::Png).empty());
}

TEST_CASE("encodeImage and decodeImage round-trip in memory (the clipboard path)") {
    Image img = sampleImage();
    img.set(3, 3, Rgba8{10, 20, 30, 77});

    const std::vector<unsigned char> png = encodeImage(img, ImageFormat::Png);
    REQUIRE_FALSE(png.empty());
    LoadResult fromPng = decodeImage(png, "clipboard image");
    REQUIRE(fromPng.image.has_value());
    CHECK(fromPng.image->get(3, 3) == Rgba8{10, 20, 30, 77});
    CHECK(fromPng.image->get(0, 0) == kRed);

    const std::vector<unsigned char> bmp = encodeImage(img, ImageFormat::Bmp);
    REQUIRE_FALSE(bmp.empty());
    LoadResult fromBmp = decodeImage(bmp);
    REQUIRE(fromBmp.image.has_value());
    CHECK(fromBmp.image->width() == 40);

    CHECK(encodeImage(Image(), ImageFormat::Png).empty());
    LoadResult junk = decodeImage(std::vector<unsigned char>{1, 2, 3, 4, 5});
    CHECK_FALSE(junk.image.has_value());
    CHECK(junk.error.find("clipboard") == std::string::npos);
    LoadResult named = decodeImage(std::vector<unsigned char>{1, 2, 3, 4, 5}, "clipboard image");
    CHECK(named.error.find("clipboard image") != std::string::npos);
    CHECK_FALSE(decodeImage({}).image.has_value());
}

// --- animated GIF ---------------------------------------------------------------------------------

namespace {

// The delay (centiseconds) of every frame, read from the Graphic Control Extension blocks (21 F9 04 ...).
std::vector<int> gifDelays(const std::vector<unsigned char>& d) {
    std::vector<int> delays;
    for (std::size_t i = 0; i + 7 < d.size(); ++i) {
        if (d[i] == 0x21 && d[i + 1] == 0xF9 && d[i + 2] == 0x04)
            delays.push_back(d[i + 4] | (d[i + 5] << 8));
    }
    return delays;
}

std::vector<Image> solidFrames(int count, int w = 8, int h = 6) {
    const Rgba8 colors[] = {kRed, kGreen, kBlue, kWhite};
    std::vector<Image> frames;
    for (int i = 0; i < count; ++i)
        frames.emplace_back(w, h, colors[i % 4]);
    return frames;
}

} // namespace

TEST_CASE("saveAnimatedGif writes frames that loadImage counts") {
    TempDir dir;
    const std::vector<Image> frames = solidFrames(3);
    const std::vector<int> delays = {10, 20, 30};
    REQUIRE(saveAnimatedGif(dir / "anim.gif", frames, delays).empty());

    LoadResult loaded = loadImage(dir / "anim.gif");
    REQUIRE(loaded.image.has_value());
    CHECK(loaded.frameCount == 3);
    CHECK(loaded.image->width() == 8);
    CHECK(loaded.image->height() == 6);
    CHECK(channelDiff(loaded.image->get(0, 0), kRed) <= 8); // the first frame is the one that opens
    CHECK(gifDelays(readAll(dir / "anim.gif")) == delays);
    CHECK_FALSE(fs::exists(dir / "anim.gif.trace-tmp"));
}

TEST_CASE("saveAnimatedGif raises short delays and fills in missing ones") {
    TempDir dir;
    const std::vector<Image> frames = solidFrames(4);
    const std::vector<int> delays = {0, 1, -5}; // the fourth is missing
    REQUIRE(saveAnimatedGif(dir / "d.gif", frames, delays).empty());
    const std::vector<int> written = gifDelays(readAll(dir / "d.gif"));
    REQUIRE(written.size() == 4);
    CHECK(written[0] == kMinGifDelayCs);
    CHECK(written[1] == kMinGifDelayCs);
    CHECK(written[2] == kMinGifDelayCs);
    CHECK(written[3] == kDefaultGifDelayCs);

    // A huge delay fits in the 16-bit field instead of wrapping around.
    const std::vector<int> big = {1000000, 5, 5, 5};
    REQUIRE(saveAnimatedGif(dir / "big.gif", frames, big).empty());
    CHECK(gifDelays(readAll(dir / "big.gif"))[0] == 65535);
}

TEST_CASE("saveAnimatedGif alpha threshold decides what is transparent, and restores the encoder") {
    TempDir dir;
    std::vector<Image> frames;
    for (int i = 0; i < 2; ++i) {
        Image img(4, 4, kWhite);
        img.set(0, 0, Rgba8{255, 0, 0, 40}); // mostly transparent
        img.set(1, 0, Rgba8{255, 0, 0, 200}); // mostly opaque
        frames.push_back(std::move(img));
    }
    const std::vector<int> delays = {10, 10};

    REQUIRE(saveAnimatedGif(dir / "t.gif", frames, delays, 128).empty());
    LoadResult cut = loadImage(dir / "t.gif");
    REQUIRE(cut.image.has_value());
    CHECK(cut.image->get(0, 0).a == 0);
    CHECK(cut.image->get(1, 0).a == 255);
    CHECK(cut.image->get(2, 2).a == 255);

    // Threshold 0 keeps every pixel opaque.
    REQUIRE(saveAnimatedGif(dir / "o.gif", frames, delays, 0).empty());
    LoadResult opaque = loadImage(dir / "o.gif");
    REQUIRE(opaque.image.has_value());
    CHECK(opaque.image->get(0, 0).a == 255);

    // The encoder global is back to "no transparency", so a plain still GIF is not affected.
    REQUIRE(saveImage(dir / "still.gif", frames[0], ImageFormat::Gif).empty());
    CHECK(loadImage(dir / "still.gif").image->get(0, 0).a == 0); // saveImage uses its own threshold (128)
    CHECK(msf_gif_alpha_threshold == 0);
}

TEST_CASE("saveAnimatedGif rejects frames it cannot write and leaves existing files alone") {
    TempDir dir;
    const fs::path path = dir / "keep.gif";
    REQUIRE(saveImage(path, sampleImage(), ImageFormat::Gif).empty());
    const std::vector<unsigned char> before = readAll(path);

    const std::vector<int> delays = {10, 10};
    CHECK_FALSE(saveAnimatedGif(path, std::vector<Image>{}, delays).empty());
    std::vector<Image> mixed = solidFrames(1, 8, 6);
    mixed.emplace_back(9, 6, kRed); // a different size
    CHECK_FALSE(saveAnimatedGif(path, mixed, delays).empty());
    std::vector<Image> blank;
    blank.emplace_back();
    CHECK_FALSE(saveAnimatedGif(path, blank, delays).empty());
    std::vector<Image> huge;
    huge.emplace_back(kMaxImageDimension + 1, 1);
    CHECK_FALSE(saveAnimatedGif(path, huge, delays).empty());

    CHECK(readAll(path) == before);
    CHECK_FALSE(fs::exists(dir / "keep.gif.trace-tmp"));

    // A folder that does not exist is reported, not thrown.
    const std::string error = saveAnimatedGif(dir / "no" / "such" / "x.gif", solidFrames(2), delays);
    CHECK_FALSE(error.empty());
}
