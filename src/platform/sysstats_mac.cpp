#include "platform/sysstats.hpp"

#include <mach/mach.h>
#include <sys/resource.h>

namespace trace::detail {

namespace {

double timevalSeconds(const timeval& tv) {
    return static_cast<double>(tv.tv_sec) + static_cast<double>(tv.tv_usec) / 1.0e6;
}

} // namespace

double processCpuSeconds() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0.0;
    return timevalSeconds(usage.ru_utime) + timevalSeconds(usage.ru_stime);
}

std::uint64_t processResidentBytes() {
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) !=
        KERN_SUCCESS)
        return 0;
    return static_cast<std::uint64_t>(info.resident_size);
}

} // namespace trace::detail
