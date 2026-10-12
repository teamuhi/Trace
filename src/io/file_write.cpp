#include "io/file_write.hpp"

#include <exception>
#include <fstream>
#include <new>
#include <system_error>

#include "core/i18n.hpp"
#include "io/path_utf8.hpp"

namespace fs = std::filesystem;

namespace trace {

namespace {

std::string describe(const fs::path& path) {
    return utf8FromPath(path.filename());
}

void removeQuietly(const fs::path& path) {
    std::error_code ignored;
    fs::remove(path, ignored);
}

} // namespace

std::string writeFileAtomic(const fs::path& path, const std::function<bool(std::ostream&)>& writer) {
    fs::path temp = path;
    temp += ".trace-tmp";

    try {
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out)
                return trf(Str::IoCannotWriteTo, {describe(path)});
            if (!writer(out)) {
                out.close();
                removeQuietly(temp);
                return trf(Str::IoDiskFull, {describe(path)});
            }
            out.close();
            if (out.fail()) {
                removeQuietly(temp);
                return trf(Str::IoDiskFull, {describe(path)});
            }
        }

        std::error_code ec;
        fs::rename(temp, path, ec); // replaces an existing file
        if (ec) {
            removeQuietly(temp);
            return trf(Str::IoCannotReplace, {describe(path), ec.message()});
        }
    } catch (const std::bad_alloc&) {
        removeQuietly(temp);
        return trf(Str::IoMemorySave, {describe(path)});
    } catch (const std::exception& e) {
        removeQuietly(temp);
        return trf(Str::IoCannotSave, {describe(path), e.what()});
    }
    return {};
}

std::string writeFileAtomic(const fs::path& path, std::span<const unsigned char> bytes) {
    return writeFileAtomic(path, [&](std::ostream& out) {
        if (!bytes.empty())
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return out.good();
    });
}

} // namespace trace
