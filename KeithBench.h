#pragma once

#include <Windows.h>
#include <string>

// Configuration for a benchmark execution.
struct BenchmarkConfig
{
    std::wstring executable;
    std::wstring arguments;
    std::wstring logFile;

    DWORD priorityClass{ NORMAL_PRIORITY_CLASS };

    // 0 means no CPU limit.
    // 1-100 means a hard CPU cap percentage.
    DWORD cpuCapPercent{};
};

// Information collected after execution.
struct BenchmarkResult
{
    bool completed{ false };

    DWORD errorCode{};
    DWORD exitCode{};
    DWORD observedPriority{};

    double elapsedSeconds{};
};

// Executes a benchmark and returns its results.
BenchmarkResult runBenchmark(
    const BenchmarkConfig& config
);
