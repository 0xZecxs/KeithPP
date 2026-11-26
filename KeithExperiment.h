#pragma once

#include "KeithBench.h"

#include <string>
#include <vector>

struct ExperimentProcessResult
{
    bool completed{ false };

    DWORD pid{};
    DWORD errorCode{};
    DWORD exitCode{};
    DWORD priorityClass{};


    double elapsedSeconds{};
    double averageCpuPercent{};
    double peakCpuPercent{};

    // Total CPU time consumed by the process.
    double cpuTimeSeconds{};

    // Applied CPU cap and Job Object verification.
    DWORD cpuCapPercent{};
    bool cpuCapVerified{ false };


    // Relative to the start of the experiment.
    double startSeconds{};
    double endSeconds{};
};

struct ExperimentEvent
{
    std::string process;
    std::string action;

    double timeSeconds{};

    DWORD pid{};
    DWORD priorityClass{};
};

struct ExperimentResult
{
    ExperimentProcessResult ffmpeg{};
    ExperimentProcessResult sevenZip{};

    bool concurrent{ false };

    // Simultaneous process lifetime, not CPU execution time.
    double overlapSeconds{};
    double startSkewMilliseconds{};


    bool adaptiveReleaseApplied{ false };
    DWORD adaptiveReleaseErrorCode{};
    double adaptiveReleaseTimeSeconds{};


    std::vector<ExperimentEvent> events;
};


ExperimentResult runExperiment(
    const BenchmarkConfig& ffmpeg,
    const BenchmarkConfig* sevenZip = nullptr,
    bool adaptiveRelease = false);


