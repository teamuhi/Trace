#include "platform/sysstats.hpp"

#include <algorithm>
#include <thread>

namespace trace {

SysStatsSampler::SysStatsSampler()
    : lastWall_(Clock::now()), lastCpuSeconds_(detail::processCpuSeconds()),
      cores_(std::max(1u, std::thread::hardware_concurrency())) {}

SysStats SysStatsSampler::sample() {
    const auto now = Clock::now();
    const double cpuSeconds = detail::processCpuSeconds();
    const double wallSeconds = std::chrono::duration<double>(now - lastWall_).count();

    SysStats stats;
    if (wallSeconds > 0.0)
        stats.cpuPercent =
            std::clamp((cpuSeconds - lastCpuSeconds_) / (wallSeconds * cores_) * 100.0, 0.0, 100.0);
    stats.rssBytes = detail::processResidentBytes();

    lastWall_ = now;
    lastCpuSeconds_ = cpuSeconds;
    return stats;
}

} // namespace trace
