
#include "KeithWatch.h"

// Convert a FILETIME value into a 64-bit integer.
static std::uint64_t fileTimeToUint64(FILETIME time)
{
    return (
        static_cast<std::uint64_t>(time.dwHighDateTime) << 32
        ) | time.dwLowDateTime;
}

bool readCpuSample(
    HANDLE processHandle,
    CpuSample& sample,
    DWORD& errorCode
)
{
    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};

    if (!GetProcessTimes(
        processHandle,
        &creationTime,
        &exitTime,
        &kernelTime,
        &userTime))
    {
        errorCode = GetLastError();
        return false;
    }

    sample.timestamp = std::chrono::steady_clock::now();

    sample.cpuTime100ns =
        fileTimeToUint64(kernelTime) +
        fileTimeToUint64(userTime);

    errorCode = ERROR_SUCCESS;
    return true;
}

double calculateCpuUsage(
    const CpuSample& previous,
    const CpuSample& current,
    DWORD logicalProcessors
)
{
    if (logicalProcessors == 0 ||
        current.cpuTime100ns < previous.cpuTime100ns)
    {
        return 0.0;
    }

    const double elapsedSeconds =
        std::chrono::duration<double>(
            current.timestamp - previous.timestamp
        ).count();

    if (elapsedSeconds <= 0.0)
    {
        return 0.0;
    }

    const double cpuSeconds =
        static_cast<double>(
            current.cpuTime100ns - previous.cpuTime100ns
            ) / 10'000'000.0;

    return (cpuSeconds / elapsedSeconds)
        / static_cast<double>(logicalProcessors)
        * 100.0;
}
