#include "platform/sysstats.hpp"

#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace trace::detail {

double processCpuSeconds() {
    std::FILE* f = std::fopen("/proc/self/stat", "r");
    if (!f)
        return 0.0;
    char buf[1024];
    const std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = '\0';

    // The command name (field 2) may contain spaces; parse after its closing parenthesis.
    const char* p = std::strrchr(buf, ')');
    if (!p)
        return 0.0;
    unsigned long utime = 0, stime = 0;
    if (std::sscanf(p + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &utime, &stime) != 2)
        return 0.0;

    const long ticks = sysconf(_SC_CLK_TCK);
    if (ticks <= 0)
        return 0.0;
    return static_cast<double>(utime + stime) / static_cast<double>(ticks);
}

std::uint64_t processResidentBytes() {
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f)
        return 0;
    unsigned long long totalPages = 0, residentPages = 0;
    const int fields = std::fscanf(f, "%llu %llu", &totalPages, &residentPages);
    std::fclose(f);
    if (fields != 2)
        return 0;

    const long pageSize = sysconf(_SC_PAGESIZE);
    return pageSize > 0 ? static_cast<std::uint64_t>(residentPages) * static_cast<std::uint64_t>(pageSize) : 0;
}

} // namespace trace::detail
