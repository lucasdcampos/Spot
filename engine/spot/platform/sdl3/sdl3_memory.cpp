#include "spot/core/process.h"

#include <cstdint>

#if defined(_WIN32)
#include <windows.h>

#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <fstream>
#include <unistd.h>
#endif

namespace Spot {

std::uint64_t Process::GetResidentMemoryBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))
               ? static_cast<std::uint64_t>(counters.WorkingSetSize)
               : 0;
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t size = MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &size) ==
                   KERN_SUCCESS
               ? static_cast<std::uint64_t>(info.resident_size)
               : 0;
#else
    std::ifstream status("/proc/self/statm");
    std::uint64_t virtualPages = 0, residentPages = 0;
    const auto pageSize = sysconf(_SC_PAGESIZE);
    if (!(status >> virtualPages >> residentPages) || pageSize <= 0) {
        return 0;
    }
    return residentPages * static_cast<std::uint64_t>(pageSize);
#endif
}

} // namespace Spot
