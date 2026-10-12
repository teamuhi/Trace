#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <span>
#include <string>

namespace trace {

// Writes a file so that a failure never damages an existing one: the data goes to "<path>.trace-tmp" and is
// renamed over `path` only when complete (an existing file is replaced). Returns an empty string on success,
// otherwise a message fit to show the user. Does not throw.

// The bytes of `bytes`.
std::string writeFileAtomic(const std::filesystem::path& path, std::span<const unsigned char> bytes);

// Whatever `writer` streams into the file. `writer` returns false when it could not produce the data (the
// message then says the disk may be full, like a failed write).
std::string writeFileAtomic(const std::filesystem::path& path, const std::function<bool(std::ostream&)>& writer);

} // namespace trace
