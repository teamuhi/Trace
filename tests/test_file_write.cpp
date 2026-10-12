#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <stdexcept>
#include <random>
#include <string>
#include <vector>

#include "io/file_write.hpp"

using namespace trace;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        std::random_device rd;
        path = fs::temp_directory_path() / ("trace_write_test_" + std::to_string(rd()) + "_" + std::to_string(++counter));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("writeFileAtomic writes bytes and replaces an existing file") {
    TempDir dir;
    const fs::path path = dir.path / "a.bin";
    const std::vector<unsigned char> first = {'o', 'n', 'e'};
    REQUIRE(writeFileAtomic(path, first).empty());
    CHECK(readText(path) == "one");

    const std::vector<unsigned char> second = {'t', 'w', 'o', 's'};
    REQUIRE(writeFileAtomic(path, second).empty());
    CHECK(readText(path) == "twos");
    CHECK_FALSE(fs::exists(dir.path / "a.bin.trace-tmp"));
}

TEST_CASE("writeFileAtomic accepts an empty payload") {
    TempDir dir;
    const fs::path path = dir.path / "empty.bin";
    REQUIRE(writeFileAtomic(path, std::vector<unsigned char>{}).empty());
    CHECK(fs::file_size(path) == 0);
}

TEST_CASE("A writer that fails leaves the existing file untouched and no temporary file") {
    TempDir dir;
    const fs::path path = dir.path / "keep.txt";
    REQUIRE(writeFileAtomic(path, std::vector<unsigned char>{'o', 'k'}).empty());

    const std::string error = writeFileAtomic(path, [](std::ostream& out) {
        out << "partial";
        return false;
    });
    CHECK_FALSE(error.empty());
    CHECK(error.find("keep.txt") != std::string::npos);
    CHECK(readText(path) == "ok");
    CHECK_FALSE(fs::exists(dir.path / "keep.txt.trace-tmp"));
}

TEST_CASE("A writer that throws is reported, not propagated") {
    TempDir dir;
    const fs::path path = dir.path / "t.txt";
    const std::string error = writeFileAtomic(path, [](std::ostream&) -> bool { throw std::runtime_error("boom"); });
    CHECK(error.find("boom") != std::string::npos);
    CHECK_FALSE(fs::exists(path));
    CHECK_FALSE(fs::exists(dir.path / "t.txt.trace-tmp"));

    const std::string memory = writeFileAtomic(path, [](std::ostream&) -> bool { throw std::bad_alloc(); });
    CHECK_FALSE(memory.empty());
    CHECK_FALSE(fs::exists(dir.path / "t.txt.trace-tmp"));
}

TEST_CASE("Writing into a missing folder explains itself") {
    TempDir dir;
    const std::string error = writeFileAtomic(dir.path / "nope" / "x.bin", std::vector<unsigned char>{1});
    CHECK_FALSE(error.empty());
    CHECK(error.find("x.bin") != std::string::npos);
}

TEST_CASE("Non-ASCII file names work") {
    TempDir dir;
    const fs::path path = dir.path / fs::path(u8"テスト_тест_é.bin");
    REQUIRE(writeFileAtomic(path, std::vector<unsigned char>{'x'}).empty());
    CHECK(readText(path) == "x");
}
