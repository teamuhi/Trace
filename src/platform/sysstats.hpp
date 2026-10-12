#pragma once

#include <chrono>
#include <cstdint>

namespace trace {

struct SysStats {
    double cpuPercent = 0.0; // process CPU use since the previous sample, normalized by core count
    std::uint64_t rssBytes = 0;
};

class SysStatsSampler {
public:
    SysStatsSampler();

    SysStats sample();

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point lastWall_;
    double lastCpuSeconds_ = 0.0;
    unsigned cores_ = 1;
};

namespace detail {

// Implemented per platform in sysstats_{win,linux,mac}.cpp.
double processCpuSeconds();
std::uint64_t processResidentBytes();

} // namespace detail

} // namespace trace
