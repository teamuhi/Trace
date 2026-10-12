#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "io/image_io.hpp"
#include "io/project_io.hpp"

using namespace trace;
namespace fs = std::filesystem;

namespace {

constexpr Rgba8 kRed{255, 0, 0, 255};
constexpr Rgba8 kBlue{0, 0, 255, 255};

struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        std::random_device rd;
        path = fs::temp_directory_path() / ("trace_proj_test_" + std::to_string(rd()) + "_" + std::to_string(++counter));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

using Bytes = std::vector<unsigned char>;

Bytes readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeAll(const fs::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void put32(Bytes& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}

void putText(Bytes& out, const char* text) {
    out.insert(out.end(), text, text + std::strlen(text));
}

// A header followed by whatever chunks the caller appends.
Bytes header(std::uint32_t version = 1, std::uint32_t kind = kProjectPixel) {
    Bytes out;
    const char magic[8] = {'T', 'R', 'C', 'P', 'R', 'O', 'J', '\0'};
    out.insert(out.end(), magic, magic + 8);
    put32(out, version);
    put32(out, kind);
    return out;
}

void chunk(Bytes& out, const char (&tag)[5], const Bytes& body) {
    putText(out, tag);
    put32(out, static_cast<std::uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
}

void end(Bytes& out) {
    putText(out, "END ");
    put32(out, 0);
}

Bytes textBytes(const std::string& text) {
    return Bytes(text.begin(), text.end());
}

ProjectData sampleProject() {
    ProjectData data;
    data.kind = kProjectPixel;
    data.meta.set("canvas", "width", "3");
    data.meta.set("canvas", "height", "2");
    data.meta.set("note", "text", "caf\xC3\xA9");
    Image a(3, 2, kRed);
    a.set(1, 1, kBlue);
    data.images.push_back(std::move(a));
    data.images.emplace_back(); // an empty placeholder keeps the numbering
    data.images.emplace_back(5, 4, Rgba8{10, 20, 30, 0});
    return data;
}

} // namespace

TEST_CASE("A project survives a write and a read") {
    const ProjectData original = sampleProject();
    const ProjectWriteResult written = writeProject(original);
    REQUIRE(written.error.empty());
    REQUIRE_FALSE(written.bytes.empty());

    const ProjectReadResult read = readProject(written.bytes, "p.tpix");
    REQUIRE(read.project.has_value());
    CHECK(read.error.empty());
    CHECK(read.undecodableImages == 0);
    CHECK(read.project->kind == kProjectPixel);
    CHECK(*read.project->meta.get("canvas", "width") == "3");
    CHECK(*read.project->meta.get("canvas", "height") == "2");
    CHECK(*read.project->meta.get("note", "text") == "caf\xC3\xA9");

    REQUIRE(read.project->images.size() == 3);
    CHECK(read.project->images[0].width() == 3);
    CHECK(read.project->images[0].get(0, 0) == kRed);
    CHECK(read.project->images[0].get(1, 1) == kBlue);
    CHECK(read.project->images[1].empty());
    REQUIRE(read.project->images[2].width() == 5);
    CHECK(read.project->images[2].get(4, 3).a == 0);
}

TEST_CASE("The container layout is exactly as documented") {
    ProjectData data;
    data.kind = kProjectFlow;
    data.meta.set("board", "zoom", "1");
    const Bytes bytes = writeProject(data).bytes;
    REQUIRE(bytes.size() > 24);

    CHECK(std::memcmp(bytes.data(), "TRCPROJ\0", 8) == 0);
    CHECK(bytes[8] == 1);  // version, little-endian
    CHECK(bytes[9] == 0);
    CHECK(bytes[12] == 1); // kind
    CHECK(std::memcmp(bytes.data() + 16, "META", 4) == 0);
    const std::uint32_t metaLength = bytes[20] | (bytes[21] << 8) | (bytes[22] << 16) | (static_cast<std::uint32_t>(bytes[23]) << 24);
    CHECK(metaLength == data.meta.serialize().size());
    CHECK(std::memcmp(bytes.data() + 24 + metaLength, "END ", 4) == 0);
}

TEST_CASE("Unknown chunks are skipped and a second META is ignored") {
    Bytes b = header();
    chunk(b, "META", textBytes("[a]\nk = first\n"));
    chunk(b, "FUTR", textBytes("from the future"));
    chunk(b, "META", textBytes("[a]\nk = second\n"));
    chunk(b, "FUTR", {});
    end(b);

    const ProjectReadResult r = readProject(b);
    REQUIRE(r.project.has_value());
    CHECK(*r.project->meta.get("a", "k") == "first");
    CHECK(r.project->images.empty());
}

TEST_CASE("A project without META reads as an empty one") {
    Bytes b = header();
    end(b);
    const ProjectReadResult r = readProject(b);
    REQUIRE(r.project.has_value());
    CHECK(r.project->meta.sections().empty());
}

TEST_CASE("Bad headers are refused with a message") {
    CHECK_FALSE(readProject({}).project.has_value());
    CHECK_FALSE(readProject({}).error.empty());
    CHECK_FALSE(readProject(textBytes("not a project at all, just some text")).project.has_value());

    Bytes badMagic = header();
    badMagic[0] = 'X';
    end(badMagic);
    CHECK_FALSE(readProject(badMagic).project.has_value());

    Bytes zero = header(0);
    end(zero);
    const ProjectReadResult z = readProject(zero, "zero.tpix");
    CHECK_FALSE(z.project.has_value());
    CHECK(z.error.find("zero.tpix") != std::string::npos);

    Bytes newer = header(2);
    end(newer);
    const ProjectReadResult n = readProject(newer, "new.tpix");
    CHECK_FALSE(n.project.has_value());
    CHECK(n.error.find("newer") != std::string::npos);
}

TEST_CASE("Unknown kinds still parse; the app decides what to do with them") {
    Bytes b = header(1, 77);
    end(b);
    const ProjectReadResult r = readProject(b);
    REQUIRE(r.project.has_value());
    CHECK(r.project->kind == 77);
}

TEST_CASE("Chunk lengths longer than the file are rejected") {
    Bytes b = header();
    putText(b, "META");
    put32(b, 0xFFFFFFFFu); // claims 4 GB
    putText(b, "tiny");
    end(b);
    CHECK_FALSE(readProject(b).project.has_value());

    Bytes c = header();
    putText(c, "PNG ");
    put32(c, 100);
    putText(c, "only ten b"); // 10 bytes of the 100 promised
    CHECK_FALSE(readProject(c).project.has_value());
}

TEST_CASE("A missing END chunk means the file was cut off") {
    Bytes b = header();
    chunk(b, "META", textBytes("[a]\nk = v\n"));
    const ProjectReadResult r = readProject(b, "cut.tflow");
    CHECK_FALSE(r.project.has_value());
    CHECK(r.error.find("cut.tflow") != std::string::npos);
}

TEST_CASE("Too many chunks are refused") {
    Bytes b = header();
    for (std::size_t i = 0; i < kMaxProjectChunks + 1; ++i)
        chunk(b, "FUTR", {});
    end(b);
    CHECK_FALSE(readProject(b).project.has_value());

    // Right at the limit (END counts as a chunk) is fine.
    Bytes ok = header();
    for (std::size_t i = 0; i < kMaxProjectChunks - 1; ++i)
        chunk(ok, "FUTR", {});
    end(ok);
    CHECK(readProject(ok).project.has_value());
}

TEST_CASE("A damaged PNG chunk becomes an empty image and the others keep their numbers") {
    const ProjectData original = sampleProject();
    Bytes b = header();
    chunk(b, "META", textBytes(original.meta.serialize()));
    chunk(b, "PNG ", encodeImage(original.images[0], ImageFormat::Png));
    chunk(b, "PNG ", textBytes("this is not a png"));
    chunk(b, "PNG ", encodeImage(original.images[2], ImageFormat::Png));
    end(b);

    const ProjectReadResult r = readProject(b);
    REQUIRE(r.project.has_value());
    CHECK(r.undecodableImages == 1);
    REQUIRE(r.project->images.size() == 3);
    CHECK(r.project->images[0].get(1, 1) == kBlue);
    CHECK(r.project->images[1].empty());
    CHECK(r.project->images[2].width() == 5);
}

TEST_CASE("Writing refuses what cannot be saved") {
    ProjectData tooMany;
    tooMany.images.resize(kMaxProjectChunks); // META and END would make it one chunk too many
    const ProjectWriteResult many = writeProject(tooMany);
    CHECK(many.bytes.empty());
    CHECK_FALSE(many.error.empty());

    ProjectData fine;
    fine.images.resize(kMaxProjectChunks - 2);
    CHECK_FALSE(writeProject(fine).bytes.empty());

    ProjectData huge;
    huge.images.emplace_back(kMaxImageDimension + 1, 1); // wider than an image may be
    const ProjectWriteResult wide = writeProject(huge);
    CHECK(wide.bytes.empty());
    CHECK_FALSE(wide.error.empty());
}

TEST_CASE("saveProject and loadProject round trip through a file, atomically") {
    TempDir dir;
    const fs::path path = dir.path / "art.tpix";
    REQUIRE(saveProject(path, sampleProject()).empty());
    CHECK_FALSE(fs::exists(dir.path / "art.tpix.trace-tmp"));

    const ProjectReadResult r = loadProject(path);
    REQUIRE(r.project.has_value());
    CHECK(r.project->images.size() == 3);

    // A failed save leaves the old file as it was.
    const Bytes before = readAll(path);
    ProjectData bad;
    bad.images.emplace_back(kMaxImageDimension + 1, 1);
    CHECK_FALSE(saveProject(path, bad).empty());
    CHECK(readAll(path) == before);
}

TEST_CASE("loadProject reports missing, empty and foreign files") {
    TempDir dir;
    const ProjectReadResult missing = loadProject(dir.path / "gone.tflow");
    CHECK_FALSE(missing.project.has_value());
    CHECK(missing.error.find("gone.tflow") != std::string::npos);

    writeAll(dir.path / "empty.tflow", {});
    const ProjectReadResult empty = loadProject(dir.path / "empty.tflow");
    CHECK_FALSE(empty.project.has_value());
    CHECK(empty.error.find("empty") != std::string::npos);

    REQUIRE(saveImage(dir.path / "pic.png", Image(4, 4, kRed), ImageFormat::Png).empty());
    const ProjectReadResult png = loadProject(dir.path / "pic.png");
    CHECK_FALSE(png.project.has_value());
    CHECK(png.error.find("not a Trace project") != std::string::npos);

    CHECK_FALSE(loadProject(dir.path).project.has_value()); // a folder
}

TEST_CASE("sniffProjectKind reads only the header") {
    TempDir dir;
    ProjectData flow;
    flow.kind = kProjectFlow;
    REQUIRE(saveProject(dir.path / "a.tflow", flow).empty());
    REQUIRE(saveProject(dir.path / "b.tpix", sampleProject()).empty());
    CHECK(sniffProjectKind(dir.path / "a.tflow") == kProjectFlow);
    CHECK(sniffProjectKind(dir.path / "b.tpix") == kProjectPixel);

    // The name does not matter, and a kind or version this build does not know is still reported.
    fs::copy_file(dir.path / "b.tpix", dir.path / "liar.png");
    CHECK(sniffProjectKind(dir.path / "liar.png") == kProjectPixel);
    Bytes odd = header(9, 1234);
    writeAll(dir.path / "odd.bin", odd);
    CHECK(sniffProjectKind(dir.path / "odd.bin") == 1234);

    // Not projects: images, short files, text, missing files, folders.
    REQUIRE(saveImage(dir.path / "p.png", Image(2, 2, kRed), ImageFormat::Png).empty());
    CHECK_FALSE(sniffProjectKind(dir.path / "p.png").has_value());
    writeAll(dir.path / "short.bin", Bytes(odd.begin(), odd.begin() + 15));
    CHECK_FALSE(sniffProjectKind(dir.path / "short.bin").has_value());
    writeAll(dir.path / "empty.bin", {});
    CHECK_FALSE(sniffProjectKind(dir.path / "empty.bin").has_value());
    CHECK_FALSE(sniffProjectKind(dir.path / "missing").has_value());
    CHECK_FALSE(sniffProjectKind(dir.path).has_value());
}

// --- hostile input -------------------------------------------------------------------------------

TEST_CASE("Truncating a project at any point fails cleanly") {
    const Bytes full = writeProject(sampleProject()).bytes;
    REQUIRE(full.size() > 60);

    // Every prefix that stops before the END tag is complete is missing its END chunk, so none may be accepted.
    for (std::size_t keep = 0; keep < full.size() - 4; ++keep) {
        CAPTURE(keep);
        const ProjectReadResult r = readProject(Bytes(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(keep)));
        CHECK_FALSE(r.project.has_value());
        CHECK_FALSE(r.error.empty());
    }
    // The tag of END alone is enough: its length is never read.
    Bytes noLength(full.begin(), full.end() - 4);
    CHECK(readProject(noLength).project.has_value());
}

TEST_CASE("Random bytes never crash the reader") {
    std::mt19937 rng(2024);
    for (int round = 0; round < 300; ++round) {
        Bytes junk(rng() % 400);
        for (auto& b : junk)
            b = static_cast<unsigned char>(rng());
        (void)readProject(junk); // must not crash; almost always an error
    }
    // Random bytes behind a valid header exercise the chunk walk.
    for (int round = 0; round < 300; ++round) {
        Bytes b = header();
        const std::size_t extra = rng() % 300;
        for (std::size_t i = 0; i < extra; ++i)
            b.push_back(static_cast<unsigned char>(rng()));
        const ProjectReadResult r = readProject(b);
        if (!r.project.has_value())
            CHECK_FALSE(r.error.empty());
    }
}

TEST_CASE("Corrupting a valid project never crashes the reader") {
    const Bytes full = writeProject(sampleProject()).bytes;
    std::mt19937 rng(7);
    for (int round = 0; round < 400; ++round) {
        Bytes mangled = full;
        const int flips = 1 + static_cast<int>(rng() % 6);
        for (int i = 0; i < flips; ++i)
            mangled[rng() % mangled.size()] = static_cast<unsigned char>(rng());
        const ProjectReadResult r = readProject(mangled);
        if (!r.project.has_value())
            CHECK_FALSE(r.error.empty());
        else
            CHECK(r.project->images.size() <= kMaxProjectChunks);
    }
}

TEST_CASE("Names in messages come from the caller, not the file") {
    const ProjectReadResult r = readProject(textBytes("junk junk junk junk junk"), "my art.tpix");
    CHECK(r.error.find("my art.tpix") != std::string::npos);
}
