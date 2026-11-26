#define NOMINMAX

#include "KeithExperiment.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
// KEITH++ V0.8 - FINAL EXPERIMENT
// ============================================================


constexpr int REPETITIONS = 6;
constexpr int SCENARIO_COUNT = 5;

// ============================================================
// EXPERIMENT STRUCTURES
// ============================================================

struct Scenario
{
    const char* name{};
    const wchar_t* tag{};

    DWORD ffmpegPriority{ NORMAL_PRIORITY_CLASS };
    DWORD sevenZipPriority{ NORMAL_PRIORITY_CLASS };

    DWORD sevenZipCpuCapPercent{};
    bool adaptiveRelease{ false };
};

struct Trial
{
    double ffmpegSeconds{};
    double sevenZipSeconds{};
    double makespanSeconds{};

    int sevenZipMips{};
    int dynamicChanges{};
};

struct Statistics
{
    double mean{};
    double standardDeviation{};
};

// ============================================================
// FORMATTING
// ============================================================

std::string number(double value)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());

    output << std::fixed
        << std::setprecision(6)
        << value;

    return output.str();
}

// ============================================================
// FFMPEG PARSER
// ============================================================

std::optional<double> readFFmpegTime(
    const std::wstring& path)
{
    std::ifstream file{ fs::path(path) };

    if (!file)
        return std::nullopt;

    std::string line;
    std::optional<double> result;

    while (std::getline(file, line))
    {
        const auto position = line.find("rtime=");

        if (position == std::string::npos)
            continue;

        std::istringstream input(
            line.substr(position + 6)
        );

        input.imbue(std::locale::classic());

        double seconds{};

        if (input >> seconds)
            result = seconds;
    }

    return result;
}

// ============================================================
// 7-ZIP PARSER
// ============================================================

std::optional<int> readSevenZipMips(
    const std::wstring& path)
{
    std::ifstream file{ fs::path(path) };

    if (!file)
        return std::nullopt;

    std::string line;

    while (std::getline(file, line))
    {
        std::istringstream input(line);

        std::string label;
        int usage{};
        int ratingPerUsage{};
        int totalRating{};

        if (input >> label &&
            label == "Tot:" &&
            input >> usage >>
            ratingPerUsage >>
            totalRating)
        {
            return totalRating;
        }
    }

    return std::nullopt;
}

// ============================================================
// STATISTICS
// ============================================================

Statistics calculateStatistics(
    const std::vector<double>& values)
{
    Statistics result{};

    if (values.empty())
        return result;

    result.mean = std::accumulate(
        values.begin(),
        values.end(),
        0.0
    ) / static_cast<double>(values.size());

    if (values.size() < 2)
        return result;

    double sumSquared{};

    for (double value : values)
    {
        const double difference =
            value - result.mean;

        sumSquared += difference * difference;
    }

    result.standardDeviation = std::sqrt(
        sumSquared /
        static_cast<double>(values.size() - 1)
    );

    return result;
}

bool successful(
    const ExperimentProcessResult& result)
{
    return result.completed &&
        result.errorCode == ERROR_SUCCESS &&
        result.exitCode == 0;
}

// ============================================================
// COUNT ADAPTIVE POLICY CHANGES
// ============================================================

int countDynamicChanges(
    const ExperimentResult& experiment)
{
    int changes = 0;

    for (const auto& event : experiment.events)
    {
        if (event.action == "cpu_cap_dynamic_25" ||
            event.action == "cpu_cap_dynamic_50" ||
            event.action == "cpu_cap_dynamic_75")
        {
            ++changes;
        }
    }

    return changes;
}

// ============================================================
// RUN EXPERIMENT
// ============================================================

std::optional<Trial> executeScenario(
    const Scenario& scenario,
    int round,
    const std::wstring& ffmpegExe,
    const std::wstring& sevenZipExe,
    const std::wstring& ffmpegArguments,
    const std::wstring& sevenZipArguments,
    std::ofstream& csv,
    std::ofstream& eventsCsv)
{
    const std::wstring prefix =
        L"benchmark_logs_v08\\round_" +
        std::to_wstring(round) +
        L"_" + scenario.tag;

    BenchmarkConfig ffmpeg{
        ffmpegExe,
        ffmpegArguments,
        prefix + L"_ffmpeg.txt",
        scenario.ffmpegPriority,
        0
    };

    BenchmarkConfig sevenZip{
        sevenZipExe,
        sevenZipArguments,
        prefix + L"_7zip.txt",
        scenario.sevenZipPriority,
        scenario.sevenZipCpuCapPercent
    };

    std::cout
        << "\n========================================\n"
        << "Round " << round
        << " | " << scenario.name << '\n'
        << "========================================\n";

    std::cout
        << "FFmpeg priority: "
        << scenario.ffmpegPriority << '\n'
        << "7-Zip priority: "
        << scenario.sevenZipPriority << '\n'
        << "Initial CPU Cap: "
        << scenario.sevenZipCpuCapPercent << "%\n"
        << "Adaptive: "
        << (scenario.adaptiveRelease
            ? "YES" : "NO")
        << '\n';

    // ========================================================
    // EXECUTE KEITHEXPERIMENT
    // ========================================================

    const ExperimentResult experiment =
        runExperiment(
            ffmpeg,
            &sevenZip,
            scenario.adaptiveRelease
        );

    // ========================================================
    // EXPORT EVENTS
    // ========================================================

    for (const auto& event : experiment.events)
    {
        eventsCsv
            << round << ','
            << scenario.name << ','
            << event.process << ','
            << event.action << ','
            << number(event.timeSeconds) << ','
            << event.pid << ','
            << event.priorityClass
            << '\n';
    }

    eventsCsv.flush();

    // ========================================================
    // VERIFY EXECUTION
    // ========================================================

    if (!successful(experiment.ffmpeg) ||
        !successful(experiment.sevenZip))
    {
        std::cerr
            << "Process execution failed.\n"
            << "FFmpeg error: "
            << experiment.ffmpeg.errorCode
            << " | exit: "
            << experiment.ffmpeg.exitCode
            << "\n7-Zip error: "
            << experiment.sevenZip.errorCode
            << " | exit: "
            << experiment.sevenZip.exitCode
            << '\n';

        return std::nullopt;
    }

    // Verify Job Object membership when a cap is used.
    if (scenario.sevenZipCpuCapPercent > 0 &&
        !experiment.sevenZip.cpuCapVerified)
    {
        std::cerr
            << "CPU Cap verification failed.\n";

        return std::nullopt;
    }

    // Verify the adaptive controller.
    if (scenario.adaptiveRelease)
    {
        if (experiment.adaptiveReleaseErrorCode !=
            ERROR_SUCCESS)
        {
            std::cerr
                << "Adaptive controller failed. Error: "
                << experiment.adaptiveReleaseErrorCode
                << '\n';

            return std::nullopt;
        }

        // If 7-Zip was still active after FFmpeg ended,
        // the release must have been applied.
        const bool releaseNeeded =
            experiment.sevenZip.endSeconds >
            experiment.ffmpeg.endSeconds;

        if (releaseNeeded &&
            !experiment.adaptiveReleaseApplied)
        {
            std::cerr
                << "Adaptive Release was expected "
                << "but was not applied.\n";

            return std::nullopt;
        }
    }

    // ========================================================
    // READ BENCHMARK RESULTS
    // ========================================================

    const auto ffmpegTime =
        readFFmpegTime(ffmpeg.logFile);

    if (!ffmpegTime)
    {
        std::cerr << "Cannot read FFmpeg rtime.\n";
        return std::nullopt;
    }

    const auto mips =
        readSevenZipMips(sevenZip.logFile);

    if (!mips)
    {
        std::cerr << "Cannot read 7-Zip MIPS.\n";
        return std::nullopt;
    }

    // ========================================================
    // BUILD TRIAL
    // ========================================================

    Trial trial{};

    trial.ffmpegSeconds = *ffmpegTime;
    trial.sevenZipMips = *mips;

    trial.sevenZipSeconds =
        experiment.sevenZip.elapsedSeconds;

    trial.dynamicChanges =
        countDynamicChanges(experiment);

    const auto& f = experiment.ffmpeg;
    const auto& z = experiment.sevenZip;

    trial.makespanSeconds =
        std::max(f.endSeconds, z.endSeconds) -
        std::min(f.startSeconds, z.startSeconds);

    // ========================================================
    // EXPORT RESULTS TO CSV
    // ========================================================

    csv
        << round << ','
        << scenario.name << ','
        << number(trial.ffmpegSeconds) << ','
        << number(f.elapsedSeconds) << ','
        << number(f.averageCpuPercent) << ','
        << trial.sevenZipMips << ','
        << number(z.elapsedSeconds) << ','
        << number(experiment.overlapSeconds) << ','
        << number(experiment.startSkewMilliseconds) << ','
        << number(f.startSeconds) << ','
        << number(f.endSeconds) << ','
        << number(z.startSeconds) << ','
        << number(z.endSeconds) << ','
        << scenario.sevenZipCpuCapPercent << ','
        << number(f.peakCpuPercent) << ','
        << number(f.cpuTimeSeconds) << ','
        << number(z.averageCpuPercent) << ','
        << number(z.peakCpuPercent) << ','
        << number(z.cpuTimeSeconds) << ','
        << (z.cpuCapVerified ? 1 : 0) << ','
        << (experiment.adaptiveReleaseApplied ? 1 : 0)
        << ',';

    if (experiment.adaptiveReleaseApplied)
    {
        csv << number(
            experiment.adaptiveReleaseTimeSeconds
        );
    }

    csv
        << ','
        << experiment.adaptiveReleaseErrorCode
        << ','
        << trial.dynamicChanges
        << ','
        << number(trial.makespanSeconds)
        << ','
        << f.priorityClass
        << ','
        << z.priorityClass
        << '\n';

    csv.flush();

    // ========================================================
    // CONSOLE OUTPUT
    // ========================================================

    std::cout << std::fixed
        << std::setprecision(3);

    std::cout
        << "\n--- PERFORMANCE RESULTS ---\n"
        << "FFmpeg: "
        << trial.ffmpegSeconds << " s\n"
        << "7-Zip: "
        << trial.sevenZipSeconds << " s\n"
        << "7-Zip MIPS: "
        << trial.sevenZipMips << '\n'
        << "Makespan: "
        << trial.makespanSeconds << " s\n"
        << "Overlap: "
        << experiment.overlapSeconds << " s\n";

    std::cout
        << "\nFFmpeg average CPU: "
        << f.averageCpuPercent << "%\n"
        << "FFmpeg peak CPU: "
        << f.peakCpuPercent << "%\n"
        << "FFmpeg CPU time: "
        << f.cpuTimeSeconds << " s\n";

    std::cout
        << "\n7-Zip average CPU: "
        << z.averageCpuPercent << "%\n"
        << "7-Zip peak CPU: "
        << z.peakCpuPercent << "%\n"
        << "7-Zip CPU time: "
        << z.cpuTimeSeconds << " s\n";

    if (scenario.sevenZipCpuCapPercent > 0)
    {
        std::cout
            << "CPU Cap verified: "
            << (z.cpuCapVerified ? "YES" : "NO")
            << '\n';
    }

    if (scenario.adaptiveRelease)
    {
        std::cout
            << "\n--- ADAPTIVE CONTROLLER ---\n"
            << "Dynamic changes: "
            << trial.dynamicChanges << '\n'
            << "Release applied: "
            << (experiment.adaptiveReleaseApplied
                ? "YES" : "NO")
            << '\n'
            << "Release time: "
            << experiment.adaptiveReleaseTimeSeconds
            << " s\n"
            << "Release error: "
            << experiment.adaptiveReleaseErrorCode
            << '\n';
    }

    return trial;
}

// ============================================================
// MAIN
// ============================================================

int main()
{
    // ========================================================
    // EXECUTABLE PATHS
    // ========================================================

    const std::wstring ffmpegExe =
        LR"(C:\Users\usuario\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-9.1-full_build\bin\ffmpeg.exe)";

    const std::wstring sevenZipExe =
        LR"(C:\Program Files\7-Zip\7z.exe)";

    const std::wstring inputFile =
        LR"(C:\Users\usuario\KeithBench\input.avi)";

    // ========================================================
    // VALIDATE PATHS
    // ========================================================

    if (!fs::exists(ffmpegExe) ||
        !fs::exists(sevenZipExe) ||
        !fs::exists(inputFile))
    {
        std::cerr
            << "FFmpeg, 7-Zip or input file not found.\n";

        return 1;
    }

    // ========================================================
    // OUTPUT FILES
    // ========================================================

    fs::create_directories(
        "benchmark_logs_v08"
    );

    std::ofstream csv(
        "keith_results_v08.csv"
    );

    std::ofstream eventsCsv(
        "keith_events_v08.csv"
    );

    if (!csv || !eventsCsv)
    {
        std::cerr
            << "Cannot create output CSV files.\n";

        return 1;
    }

    csv.imbue(std::locale::classic());
    eventsCsv.imbue(std::locale::classic());

    csv << std::fixed << std::setprecision(6);
    eventsCsv << std::fixed << std::setprecision(6);

    // ========================================================
    // CSV HEADERS
    // ========================================================

    csv
        << "round,scenario,"
        << "ffmpeg_rtime_s,"
        << "ffmpeg_wall_s,"
        << "ffmpeg_avg_cpu_pct,"
        << "sevenzip_mips,"
        << "sevenzip_wall_s,"
        << "overlap_s,"
        << "start_skew_ms,"
        << "ffmpeg_start_s,"
        << "ffmpeg_end_s,"
        << "sevenzip_start_s,"
        << "sevenzip_end_s,"
        << "sevenzip_initial_cap_pct,"
        << "ffmpeg_peak_cpu_pct,"
        << "ffmpeg_cpu_time_s,"
        << "sevenzip_avg_cpu_pct,"
        << "sevenzip_peak_cpu_pct,"
        << "sevenzip_cpu_time_s,"
        << "sevenzip_cap_verified,"
        << "adaptive_release_applied,"
        << "adaptive_release_time_s,"
        << "adaptive_release_error,"
        << "adaptive_dynamic_changes,"
        << "makespan_s,"
        << "ffmpeg_priority_class,"
        << "sevenzip_priority_class\n";

    eventsCsv
        << "round,scenario,process,action,"
        << "time_s,pid,priority_class\n";

    // ========================================================
    // BENCHMARK CONFIGURATION
    // ========================================================

    const DWORD processors =
        GetActiveProcessorCount(
            ALL_PROCESSOR_GROUPS
        );

    const DWORD threads =
        std::clamp<DWORD>(
            processors,
            1,
            16
        );

    const std::wstring ffmpegArguments =
        L"-nostdin -benchmark -stream_loop 5 -i \"" +
        inputFile +
        L"\" -c:v libx264 -preset medium "
        L"-crf 23 -threads:v " +
        std::to_wstring(threads) +
        L" -an -f null NUL";

    const std::wstring sevenZipArguments =
        L"b 10 -mmt" +
        std::to_wstring(threads) +
        L" -md22";

    // ========================================================
    // FIVE EXPERIMENTAL POLICIES
    // ========================================================

    const std::array<Scenario, SCENARIO_COUNT> scenarios{ {
        {
            "Windows_Default",
            L"DEFAULT",
            NORMAL_PRIORITY_CLASS,
            NORMAL_PRIORITY_CLASS,
            0,
            false
        },
        {
            "Keith_Priority",
            L"PRIORITY",
            ABOVE_NORMAL_PRIORITY_CLASS,
            BELOW_NORMAL_PRIORITY_CLASS,
            0,
            false
        },
        {
            "Keith_Static_25",
            L"STATIC25",
            NORMAL_PRIORITY_CLASS,
            NORMAL_PRIORITY_CLASS,
            25,
            false
        },
        {
            "Keith_Adaptive_25",
            L"ADAPTIVE25",
            NORMAL_PRIORITY_CLASS,
            NORMAL_PRIORITY_CLASS,
            25,
            true
        },
        {
            "Keith_Hybrid",
            L"HYBRID",
            ABOVE_NORMAL_PRIORITY_CLASS,
            BELOW_NORMAL_PRIORITY_CLASS,
            25,
            true
        }
    } };

    // ========================================================
    // BALANCED ORDER
    // ========================================================

    const std::array<int, SCENARIO_COUNT> baseOrder{
        0, 2, 4, 1, 3
    };

    std::array<
        std::vector<double>,
        SCENARIO_COUNT
    > ffmpegTimes;

    std::array<
        std::vector<double>,
        SCENARIO_COUNT
    > sevenZipTimes;

    std::array<
        std::vector<double>,
        SCENARIO_COUNT
    > sevenZipMips;

    std::array<
        std::vector<double>,
        SCENARIO_COUNT
    > makespanTimes;

    // ========================================================
    // EXPERIMENT INFORMATION
    // ========================================================

    std::cout
        << "========================================\n"
        << "KEITH++ V0.8 - FINAL CPU EXPERIMENT\n"
        << "========================================\n"
        << "Scenarios: " << SCENARIO_COUNT << '\n'
        << "Repetitions: " << REPETITIONS << '\n'
        << "Total tests: "
        << SCENARIO_COUNT * REPETITIONS << '\n'
        << "Logical processors: "
        << processors << '\n'
        << "Benchmark threads: "
        << threads << '\n';

    // ========================================================
    // EXECUTE EXPERIMENTS
    // ========================================================

    for (int round = 0;
        round < REPETITIONS;
        ++round)
    {
        std::cout
            << "\n========== ROUND "
            << round + 1
            << " ==========\n";

        for (int position = 0;
            position < SCENARIO_COUNT;
            ++position)
        {
            const int index =
                (baseOrder[position] + round)
                % SCENARIO_COUNT;

            const Scenario& scenario =
                scenarios[index];

            const auto trial = executeScenario(
                scenario,
                round + 1,
                ffmpegExe,
                sevenZipExe,
                ffmpegArguments,
                sevenZipArguments,
                csv,
                eventsCsv
            );

            if (!trial)
            {
                std::cerr
                    << "\nExperiment failed.\n"
                    << "Scenario: "
                    << scenario.name << '\n'
                    << "Round: " << round + 1
                    << '\n';

                return 1;
            }

            ffmpegTimes[index].push_back(
                trial->ffmpegSeconds
            );

            sevenZipTimes[index].push_back(
                trial->sevenZipSeconds
            );

            sevenZipMips[index].push_back(
                static_cast<double>(
                    trial->sevenZipMips
                    )
            );

            makespanTimes[index].push_back(
                trial->makespanSeconds
            );
        }
    }

    // ========================================================
    // STATISTICS
    // ========================================================

    std::array<
        Statistics, SCENARIO_COUNT
    > ffStats{};

    std::array<
        Statistics, SCENARIO_COUNT
    > zipTimeStats{};

    std::array<
        Statistics, SCENARIO_COUNT
    > zipMipsStats{};

    std::array<
        Statistics, SCENARIO_COUNT
    > makespanStats{};

    for (int i = 0; i < SCENARIO_COUNT; ++i)
    {
        ffStats[i] =
            calculateStatistics(ffmpegTimes[i]);

        zipTimeStats[i] =
            calculateStatistics(sevenZipTimes[i]);

        zipMipsStats[i] =
            calculateStatistics(sevenZipMips[i]);

        makespanStats[i] =
            calculateStatistics(makespanTimes[i]);
    }

    std::cout
        << std::fixed
        << std::setprecision(3);

    // ========================================================
    // FINAL RESULTS
    // ========================================================

    std::cout
        << "\n========================================\n"
        << "FINAL RESULTS - KEITH++ V0.8\n"
        << "========================================\n";

    for (int i = 0; i < SCENARIO_COUNT; ++i)
    {
        std::cout
            << "\n" << scenarios[i].name << '\n'
            << "FFmpeg: "
            << ffStats[i].mean << " +/- "
            << ffStats[i].standardDeviation
            << " s\n"
            << "7-Zip time: "
            << zipTimeStats[i].mean << " +/- "
            << zipTimeStats[i].standardDeviation
            << " s\n"
            << "7-Zip MIPS: "
            << zipMipsStats[i].mean << " +/- "
            << zipMipsStats[i].standardDeviation
            << '\n'
            << "Makespan: "
            << makespanStats[i].mean << " +/- "
            << makespanStats[i].standardDeviation
            << " s\n";
    }

    // ========================================================
    // COMPARE WITH WINDOWS DEFAULT
    // ========================================================

    const double defaultFFmpeg =
        ffStats[0].mean;

    const double defaultZipTime =
        zipTimeStats[0].mean;

    const double defaultZipMips =
        zipMipsStats[0].mean;

    const double defaultMakespan =
        makespanStats[0].mean;

    if (defaultFFmpeg <= 0.0 ||
        defaultZipTime <= 0.0 ||
        defaultZipMips <= 0.0 ||
        defaultMakespan <= 0.0)
    {
        std::cerr
            << "Invalid benchmark statistics.\n";

        return 1;
    }

    std::cout
        << "\n========================================\n"
        << "COMPARISON VS WINDOWS DEFAULT\n"
        << "========================================\n";

    for (int i = 1; i < SCENARIO_COUNT; ++i)
    {
        const double ffImprovement =
            (defaultFFmpeg - ffStats[i].mean) /
            defaultFFmpeg * 100.0;

        const double zipTimeImprovement =
            (defaultZipTime - zipTimeStats[i].mean) /
            defaultZipTime * 100.0;

        const double zipMipsChange =
            (zipMipsStats[i].mean - defaultZipMips) /
            defaultZipMips * 100.0;

        const double makespanImprovement =
            (defaultMakespan - makespanStats[i].mean) /
            defaultMakespan * 100.0;

        std::cout
            << "\n" << scenarios[i].name << '\n'
            << "FFmpeg improvement: "
            << ffImprovement << "%\n"
            << "7-Zip time improvement: "
            << zipTimeImprovement << "%\n"
            << "7-Zip MIPS change: "
            << zipMipsChange << "%\n"
            << "Makespan improvement: "
            << makespanImprovement << "%\n";
    }

    // ========================================================
    // DIRECT COMPARISON: ADAPTIVE VS STATIC
    // ========================================================

    const double staticFFmpeg =
        ffStats[2].mean;

    const double staticZipTime =
        zipTimeStats[2].mean;

    const double staticMakespan =
        makespanStats[2].mean;

    if (staticFFmpeg > 0.0 &&
        staticZipTime > 0.0 &&
        staticMakespan > 0.0)
    {
        std::cout
            << "\n========================================\n"
            << "ADAPTIVE VS STATIC 25\n"
            << "========================================\n";

        std::cout
            << "FFmpeg improvement: "
            << (staticFFmpeg - ffStats[3].mean) /
            staticFFmpeg * 100.0
            << "%\n";

        std::cout
            << "7-Zip time improvement: "
            << (staticZipTime - zipTimeStats[3].mean) /
            staticZipTime * 100.0
            << "%\n";

        std::cout
            << "Makespan improvement: "
            << (staticMakespan - makespanStats[3].mean) /
            staticMakespan * 100.0
            << "%\n";
    }

    // ========================================================
    // DIRECT COMPARISON: HYBRID VS ADAPTIVE
    // ========================================================

    if (ffStats[3].mean > 0.0 &&
        zipTimeStats[3].mean > 0.0 &&
        makespanStats[3].mean > 0.0)
    {
        std::cout
            << "\n========================================\n"
            << "HYBRID VS ADAPTIVE\n"
            << "========================================\n";

        std::cout
            << "FFmpeg improvement: "
            << (ffStats[3].mean - ffStats[4].mean) /
            ffStats[3].mean * 100.0
            << "%\n";

        std::cout
            << "7-Zip time improvement: "
            << (zipTimeStats[3].mean -
                zipTimeStats[4].mean) /
            zipTimeStats[3].mean * 100.0
            << "%\n";

        std::cout
            << "Makespan improvement: "
            << (makespanStats[3].mean -
                makespanStats[4].mean) /
            makespanStats[3].mean * 100.0
            << "%\n";
    }

    // ========================================================
    // OUTPUT FILES
    // ========================================================

    std::cout
        << "\n========================================\n"
        << "KEITH++ V0.8 COMPLETED\n"
        << "========================================\n"
        << "\nFiles:\n"
        << "keith_results_v08.csv\n"
        << "keith_events_v08.csv\n"
        << "benchmark_logs_v08/\n";

    return 0;
}
