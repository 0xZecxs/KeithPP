
#pragma once

#include <Windows.h>
#include <chrono>
#include <cstdint>

struct CpuSample
{
    std::uint64_t cpuTime100ns{};
    std::chrono::steady_clock::time_point timestamp{};
};

bool readCpuSample(
    HANDLE processHandle,
    CpuSample& sample,
    DWORD& errorCode
);

double calculateCpuUsage(
    const CpuSample& previous,
    const CpuSample& current,
    DWORD logicalProcessors
);
