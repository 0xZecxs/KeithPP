#define NOMINMAX

#include "KeithExperiment.h"
#include "KeithCore.h"
#include "KeithWatch.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

// ============================================================
// KEITH++ V0.8
// CPU POLICY EXPERIMENT CONTROLLER
//
// Features:
// - Process creation and monitoring
// - CPU priority policies
// - Job Object CPU hard caps
// - Dynamic CPU cap adjustments
// - Automatic cap release when FFmpeg finishes
// - CPU metrics and experiment events
// ============================================================

namespace
{
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    // ========================================================
    // TIME UTILITIES
    // ========================================================

    double secondsBetween(
        TimePoint start,
        TimePoint end)
    {
        return std::chrono::duration<double>(
            end - start
        ).count();
    }

    DWORD logicalProcessorCount()
    {
        const DWORD count =
            GetActiveProcessorCount(
                ALL_PROCESSOR_GROUPS
            );

        return count > 0 ? count : 1;
    }

    // ========================================================
    // KEITH++ V0.8 - ADAPTIVE POLICY PARAMETERS
    // ========================================================

    constexpr DWORD MIN_DYNAMIC_CAP = 25;
    constexpr DWORD MAX_DYNAMIC_CAP = 75;
    constexpr DWORD CAP_STEP = 25;

    constexpr double LOW_CPU_THRESHOLD = 70.0;
    constexpr double HIGH_CPU_THRESHOLD = 90.0;

    constexpr int REQUIRED_CONSECUTIVE_SAMPLES = 2;

    constexpr auto CONTROL_INTERVAL =
        std::chrono::seconds(1);

    constexpr auto MIN_TIME_BETWEEN_CHANGES =
        std::chrono::seconds(5);

    // ========================================================
    // SYSTEM CPU MONITOR
    // ========================================================

    class SystemCpuMonitor
    {
    private:

        struct SystemTimes
        {
            std::uint64_t idle{};
            std::uint64_t kernel{};
            std::uint64_t user{};
        };

        SystemTimes previous_{};
        bool initialized_{ false };

        static std::uint64_t toUint64(
            const FILETIME& value)
        {
            return
                (static_cast<std::uint64_t>(
                    value.dwHighDateTime) << 32) |
                value.dwLowDateTime;
        }

        static bool readTimes(SystemTimes& output)
        {
            FILETIME idle{};
            FILETIME kernel{};
            FILETIME user{};

            if (!GetSystemTimes(
                &idle,
                &kernel,
                &user))
            {
                return false;
            }

            output.idle = toUint64(idle);
            output.kernel = toUint64(kernel);
            output.user = toUint64(user);

            return true;
        }

    public:

        bool initialize()
        {
            initialized_ = readTimes(previous_);
            return initialized_;
        }

        bool sample(double& cpuPercent)
        {
            if (!initialized_)
                return false;

            SystemTimes current{};

            if (!readTimes(current))
                return false;

            if (current.idle < previous_.idle ||
                current.kernel < previous_.kernel ||
                current.user < previous_.user)
            {
                previous_ = current;
                return false;
            }

            const std::uint64_t idleDelta =
                current.idle - previous_.idle;

            const std::uint64_t kernelDelta =
                current.kernel - previous_.kernel;

            const std::uint64_t userDelta =
                current.user - previous_.user;

            previous_ = current;

            // Kernel time includes idle time.
            const std::uint64_t totalDelta =
                kernelDelta + userDelta;

            if (totalDelta == 0 ||
                idleDelta > totalDelta)
            {
                return false;
            }

            const double busyFraction =
                1.0 -
                static_cast<double>(idleDelta) /
                static_cast<double>(totalDelta);

            cpuPercent = std::clamp(
                busyFraction * 100.0,
                0.0,
                100.0
            );

            return std::isfinite(cpuPercent);
        }
    };

    // ========================================================
    // BENCHMARK PROCESS
    // ========================================================

    class BenchmarkProcess
    {
    public:

        BenchmarkProcess() = default;

        BenchmarkProcess(
            const BenchmarkProcess&) = delete;

        BenchmarkProcess& operator=(
            const BenchmarkProcess&) = delete;

        // ====================================================
        // RESOURCE CLEANUP
        // ====================================================

        ~BenchmarkProcess()
        {
            if (process_ != nullptr)
            {
                const DWORD state =
                    WaitForSingleObject(process_, 0);

                if (state == WAIT_TIMEOUT)
                {
                    // Only terminate processes launched
                    // by this BenchmarkProcess instance.
                    TerminateProcess(process_, 1);

                    WaitForSingleObject(
                        process_,
                        2000
                    );
                }
            }

            if (thread_ != nullptr)
                CloseHandle(thread_);

            if (process_ != nullptr)
                CloseHandle(process_);
        }

        // ====================================================
        // PREPARE PROCESS
        // ====================================================

        bool prepare(const BenchmarkConfig& config)
        {
            SECURITY_ATTRIBUTES security{};

            security.nLength = sizeof(security);
            security.bInheritHandle = TRUE;

            // Create output log.
            HANDLE log = CreateFileW(
                config.logFile.c_str(),
                GENERIC_WRITE,
                FILE_SHARE_READ,
                &security,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );

            if (log == INVALID_HANDLE_VALUE)
            {
                result_.errorCode = GetLastError();
                return false;
            }

            // Redirect standard input to NUL.
            HANDLE input = CreateFileW(
                L"NUL",
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                &security,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );

            if (input == INVALID_HANDLE_VALUE)
            {
                result_.errorCode = GetLastError();

                CloseHandle(log);
                return false;
            }

            STARTUPINFOW startup{};

            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;

            startup.hStdInput = input;
            startup.hStdOutput = log;
            startup.hStdError = log;

            PROCESS_INFORMATION information{};

            std::wstring command =
                L"\"" + config.executable +
                L"\" " + config.arguments;

            // Create the process suspended so that
            // scheduling policies are applied first.
            const BOOL created = CreateProcessW(
                config.executable.c_str(),
                command.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_SUSPENDED |
                CREATE_NO_WINDOW |
                NORMAL_PRIORITY_CLASS,
                nullptr,
                nullptr,
                &startup,
                &information
            );

            const DWORD creationError =
                created ? ERROR_SUCCESS : GetLastError();

            CloseHandle(input);
            CloseHandle(log);

            if (!created)
            {
                result_.errorCode = creationError;
                return false;
            }

            process_ = information.hProcess;
            thread_ = information.hThread;

            result_.pid = information.dwProcessId;

            // -----------------------------------------------
            // APPLY PRIORITY
            // -----------------------------------------------

            DWORD errorCode{};

            if (!core_.open(
                result_.pid,
                errorCode))
            {
                result_.errorCode = errorCode;
                return false;
            }

            if (!core_.setPriority(
                config.priorityClass,
                errorCode))
            {
                result_.errorCode = errorCode;
                return false;
            }

            result_.priorityClass =
                GetPriorityClass(core_.handle());

            if (result_.priorityClass == 0)
            {
                result_.errorCode = GetLastError();
                return false;
            }

            // -----------------------------------------------
            // APPLY INITIAL CPU CAP
            // -----------------------------------------------

            if (config.cpuCapPercent > 0)
            {
                DWORD cpuCapError{};

                if (!cpuCap_.createHardCap(
                    config.cpuCapPercent,
                    cpuCapError))
                {
                    result_.errorCode = cpuCapError;
                    return false;
                }

                if (!cpuCap_.attachProcess(
                    process_,
                    cpuCapError))
                {
                    result_.errorCode = cpuCapError;
                    return false;
                }

                // Preserve the initial cap for the
                // experiment's configuration metadata.
                result_.cpuCapPercent =
                    config.cpuCapPercent;

                result_.cpuCapVerified = true;
            }

            return true;
        }

        // ====================================================
        // START PROCESS
        // ====================================================

        bool start(TimePoint experimentOrigin)
        {
            if (process_ == nullptr ||
                thread_ == nullptr)
            {
                result_.errorCode =
                    ERROR_INVALID_HANDLE;

                return false;
            }

            const DWORD resumeResult =
                ResumeThread(thread_);

            if (resumeResult ==
                static_cast<DWORD>(-1))
            {
                result_.errorCode = GetLastError();
                return false;
            }

            startTime_ = Clock::now();
            started_ = true;

            result_.startSeconds =
                secondsBetween(
                    experimentOrigin,
                    startTime_
                );

            CpuSample initial{};
            DWORD errorCode{};

            if (readCpuSample(
                process_,
                initial,
                errorCode))
            {
                firstSample_ = initial;
                previousSample_ = initial;

                initialSampleValid_ = true;
                previousSampleValid_ = true;
            }

            return true;
        }

        // ====================================================
        // MONITOR PROCESS CPU
        // ====================================================

        void sample()
        {
            if (!started_ || finished_)
                return;

            CpuSample current{};
            DWORD errorCode{};

            if (!readCpuSample(
                process_,
                current,
                errorCode))
            {
                return;
            }

            if (!initialSampleValid_)
            {
                firstSample_ = current;
                initialSampleValid_ = true;
            }

            if (previousSampleValid_)
            {
                const double usage =
                    calculateCpuUsage(
                        previousSample_,
                        current,
                        logicalProcessorCount()
                    );

                if (std::isfinite(usage))
                {
                    lastCpuPercent_ =
                        std::clamp(
                            usage,
                            0.0,
                            100.0
                        );

                    result_.peakCpuPercent =
                        std::max(
                            result_.peakCpuPercent,
                            lastCpuPercent_
                        );
                }
            }

            previousSample_ = current;
            previousSampleValid_ = true;
        }

        // ====================================================
        // KEITH++ V0.8 - CHANGE CPU CAP
        // ====================================================

        bool setCpuCap(
            DWORD newPercent,
            DWORD& errorCode)
        {
            if (!started_ || finished_)
            {
                errorCode = ERROR_INVALID_PARAMETER;
                return false;
            }

            return cpuCap_.updateHardCap(
                newPercent,
                errorCode
            );
        }

        DWORD currentCpuCap() const noexcept
        {
            return cpuCap_.percent();
        }

        // ====================================================
        // FINISH PROCESS AND COLLECT METRICS
        // ====================================================

        void finish(TimePoint experimentOrigin)
        {
            if (finished_)
                return;

            sample();

            CpuSample finalCpu{};
            DWORD cpuError{};

            if (readCpuSample(
                process_,
                finalCpu,
                cpuError))
            {
                result_.cpuTimeSeconds =
                    static_cast<double>(
                        finalCpu.cpuTime100ns
                        ) / 10'000'000.0;

                previousSample_ = finalCpu;
                previousSampleValid_ = true;

                if (!initialSampleValid_)
                {
                    firstSample_ = finalCpu;
                    initialSampleValid_ = true;
                }
            }

            endTime_ = Clock::now();
            finished_ = true;

            result_.endSeconds =
                secondsBetween(
                    experimentOrigin,
                    endTime_
                );

            result_.elapsedSeconds =
                secondsBetween(
                    startTime_,
                    endTime_
                );

            if (initialSampleValid_ &&
                previousSampleValid_)
            {
                const double average =
                    calculateCpuUsage(
                        firstSample_,
                        previousSample_,
                        logicalProcessorCount()
                    );

                if (std::isfinite(average))
                {
                    result_.averageCpuPercent =
                        std::clamp(
                            average,
                            0.0,
                            100.0
                        );
                }
            }

            DWORD exitCode{};

            if (!GetExitCodeProcess(
                process_,
                &exitCode))
            {
                result_.errorCode = GetLastError();
                return;
            }

            result_.exitCode = exitCode;
            result_.completed = true;
        }

        // ====================================================
        // ACCESSORS
        // ====================================================

        void setError(DWORD errorCode)
        {
            result_.errorCode = errorCode;
        }

        HANDLE handle() const noexcept
        {
            return process_;
        }

        bool finished() const noexcept
        {
            return finished_;
        }

        double cpuUsage() const noexcept
        {
            return finished_
                ? 0.0
                : lastCpuPercent_;
        }

        ExperimentProcessResult result() const
        {
            return result_;
        }

    private:

        HANDLE process_{ nullptr };
        HANDLE thread_{ nullptr };

        KeithCore core_{};
        KeithCpuCap cpuCap_{};

        bool started_{ false };
        bool finished_{ false };

        bool initialSampleValid_{ false };
        bool previousSampleValid_{ false };

        CpuSample firstSample_{};
        CpuSample previousSample_{};

        double lastCpuPercent_{};

        TimePoint startTime_{};
        TimePoint endTime_{};

        ExperimentProcessResult result_{};
    };
}

// ============================================================
// EXPERIMENT CONTROLLER
// ============================================================

ExperimentResult runExperiment(
    const BenchmarkConfig& ffmpegConfig,
    const BenchmarkConfig* sevenZipConfig,
    bool adaptiveRelease)
{
    ExperimentResult experiment{};

    experiment.concurrent =
        sevenZipConfig != nullptr;

    // An adaptive policy requires a process with a CPU cap.
    if (adaptiveRelease &&
        (sevenZipConfig == nullptr ||
            sevenZipConfig->cpuCapPercent < MIN_DYNAMIC_CAP ||
            sevenZipConfig->cpuCapPercent > MAX_DYNAMIC_CAP))
    {
        experiment.adaptiveReleaseErrorCode =
            ERROR_INVALID_PARAMETER;

        return experiment;
    }

    BenchmarkProcess ffmpeg;
    BenchmarkProcess sevenZip;

    const auto origin = Clock::now();

    // ========================================================
    // EVENT REGISTRATION
    // ========================================================

    auto recordEvent = [&](
        const std::string& process,
        const std::string& action,
        const BenchmarkProcess& target)
        {
            const auto state = target.result();

            experiment.events.push_back({
                process,
                action,
                secondsBetween(origin, Clock::now()),
                state.pid,
                state.priorityClass
                });
        };

    // ========================================================
    // PREPARE PROCESSES
    // ========================================================

    if (!ffmpeg.prepare(ffmpegConfig))
    {
        experiment.ffmpeg = ffmpeg.result();
        return experiment;
    }

    recordEvent(
        "FFmpeg",
        "priority_applied",
        ffmpeg
    );

    if (sevenZipConfig != nullptr)
    {
        if (!sevenZip.prepare(*sevenZipConfig))
        {
            experiment.ffmpeg = ffmpeg.result();
            experiment.sevenZip = sevenZip.result();

            return experiment;
        }

        recordEvent(
            "7-Zip",
            "priority_applied",
            sevenZip
        );

        if (sevenZipConfig->cpuCapPercent > 0)
        {
            recordEvent(
                "7-Zip",
                "cpu_cap_verified",
                sevenZip
            );
        }
    }

    // ========================================================
    // START PROCESSES
    // ========================================================

    if (!ffmpeg.start(origin))
    {
        experiment.ffmpeg = ffmpeg.result();
        return experiment;
    }

    recordEvent(
        "FFmpeg",
        "started",
        ffmpeg
    );

    if (sevenZipConfig != nullptr)
    {
        if (!sevenZip.start(origin))
        {
            experiment.ffmpeg = ffmpeg.result();
            experiment.sevenZip = sevenZip.result();

            return experiment;
        }

        recordEvent(
            "7-Zip",
            "started",
            sevenZip
        );
    }

    // ========================================================
    // INITIALIZE ADAPTIVE CONTROLLER
    // ========================================================

    SystemCpuMonitor systemCpu;

    if (adaptiveRelease)
    {
        if (!systemCpu.initialize())
        {
            const DWORD error = GetLastError();

            experiment.adaptiveReleaseErrorCode =
                error != ERROR_SUCCESS
                ? error
                : ERROR_GEN_FAILURE;

            ffmpeg.setError(
                experiment.adaptiveReleaseErrorCode
            );

            sevenZip.setError(
                experiment.adaptiveReleaseErrorCode
            );

            experiment.ffmpeg = ffmpeg.result();
            experiment.sevenZip = sevenZip.result();

            return experiment;
        }

        recordEvent(
            "7-Zip",
            "adaptive_controller_started",
            sevenZip
        );
    }

    // ========================================================
    // ADAPTIVE CONTROLLER STATE
    // ========================================================

    double smoothedSystemCpu = 0.0;
    bool smoothedCpuValid = false;

    int lowCpuSamples = 0;
    int highCpuSamples = 0;

    TimePoint lastCapChange = Clock::now();

    auto nextControl =
        Clock::now() + CONTROL_INTERVAL;

    const auto printInterval =
        std::chrono::seconds(1);

    auto nextPrint =
        Clock::now() + printInterval;

    // ========================================================
    // MAIN EXPERIMENT LOOP
    // ========================================================

    while (true)
    {
        HANDLE handles[2]{};
        DWORD handleCount = 0;

        if (!ffmpeg.finished())
        {
            handles[handleCount++] =
                ffmpeg.handle();
        }

        if (sevenZipConfig != nullptr &&
            !sevenZip.finished())
        {
            handles[handleCount++] =
                sevenZip.handle();
        }

        if (handleCount == 0)
            break;

        const DWORD waitResult =
            WaitForMultipleObjects(
                handleCount,
                handles,
                FALSE,
                250
            );

        if (waitResult == WAIT_FAILED)
        {
            const DWORD error = GetLastError();

            ffmpeg.setError(error);
            sevenZip.setError(error);

            experiment.ffmpeg = ffmpeg.result();
            experiment.sevenZip = sevenZip.result();

            return experiment;
        }

        // ====================================================
        // CHECK FFMPEG TERMINATION
        // ====================================================

        if (!ffmpeg.finished())
        {
            const DWORD state =
                WaitForSingleObject(
                    ffmpeg.handle(),
                    0
                );

            if (state == WAIT_FAILED)
            {
                ffmpeg.setError(GetLastError());

                experiment.ffmpeg = ffmpeg.result();
                experiment.sevenZip = sevenZip.result();

                return experiment;
            }

            if (state == WAIT_OBJECT_0)
            {
                ffmpeg.finish(origin);

                recordEvent(
                    "FFmpeg",
                    "finished",
                    ffmpeg
                );

                // ============================================
                // ADAPTIVE RELEASE
                // ============================================

                if (adaptiveRelease &&
                    sevenZipConfig != nullptr &&
                    !sevenZip.finished())
                {
                    const DWORD zipState =
                        WaitForSingleObject(
                            sevenZip.handle(),
                            0
                        );

                    if (zipState == WAIT_FAILED)
                    {
                        experiment.adaptiveReleaseErrorCode =
                            GetLastError();

                        sevenZip.setError(
                            experiment.adaptiveReleaseErrorCode
                        );

                        experiment.ffmpeg = ffmpeg.result();
                        experiment.sevenZip = sevenZip.result();

                        return experiment;
                    }

                    if (zipState == WAIT_TIMEOUT)
                    {
                        DWORD errorCode{};

                        if (sevenZip.setCpuCap(
                            100,
                            errorCode))
                        {
                            experiment.adaptiveReleaseApplied =
                                true;

                            experiment.adaptiveReleaseTimeSeconds =
                                secondsBetween(
                                    origin,
                                    Clock::now()
                                );

                            recordEvent(
                                "7-Zip",
                                "cpu_cap_released_100",
                                sevenZip
                            );

                            std::cout
                                << "[Keith Adaptive] "
                                << "FFmpeg finished. "
                                << "7-Zip cap raised to 100%.\n";
                        }
                        else
                        {
                            experiment.adaptiveReleaseErrorCode =
                                errorCode;

                            recordEvent(
                                "7-Zip",
                                "cpu_cap_release_failed",
                                sevenZip
                            );

                            std::cerr
                                << "[Keith Adaptive] "
                                << "Cap release failed. Error: "
                                << errorCode << '\n';
                        }
                    }
                }
            }
        }

        // ====================================================
        // CHECK 7-ZIP TERMINATION
        // ====================================================

        if (sevenZipConfig != nullptr &&
            !sevenZip.finished())
        {
            const DWORD state =
                WaitForSingleObject(
                    sevenZip.handle(),
                    0
                );

            if (state == WAIT_FAILED)
            {
                sevenZip.setError(GetLastError());

                experiment.ffmpeg = ffmpeg.result();
                experiment.sevenZip = sevenZip.result();

                return experiment;
            }

            if (state == WAIT_OBJECT_0)
            {
                sevenZip.finish(origin);

                recordEvent(
                    "7-Zip",
                    "finished",
                    sevenZip
                );
            }
        }

        // ====================================================
        // COLLECT CPU SAMPLES
        // ====================================================

        if (!ffmpeg.finished())
            ffmpeg.sample();

        if (sevenZipConfig != nullptr &&
            !sevenZip.finished())
        {
            sevenZip.sample();
        }

        // ====================================================
        // KEITH++ V0.8 - DYNAMIC CPU POLICY
        // ====================================================

        const TimePoint now = Clock::now();

        if (adaptiveRelease &&
            !ffmpeg.finished() &&
            !sevenZip.finished() &&
            now >= nextControl)
        {
            nextControl =
                now + CONTROL_INTERVAL;

            double systemUsage{};

            if (systemCpu.sample(systemUsage))
            {
                // Exponential smoothing reduces sensitivity
                // to short CPU utilization spikes.
                if (!smoothedCpuValid)
                {
                    smoothedSystemCpu = systemUsage;
                    smoothedCpuValid = true;
                }
                else
                {
                    constexpr double alpha = 0.40;

                    smoothedSystemCpu =
                        alpha * systemUsage +
                        (1.0 - alpha) * smoothedSystemCpu;
                }

                // ============================================
                // EVALUATE CPU PRESSURE
                // ============================================

                if (smoothedSystemCpu <
                    LOW_CPU_THRESHOLD)
                {
                    ++lowCpuSamples;
                    highCpuSamples = 0;
                }
                else if (smoothedSystemCpu >
                    HIGH_CPU_THRESHOLD)
                {
                    ++highCpuSamples;
                    lowCpuSamples = 0;
                }
                else
                {
                    lowCpuSamples = 0;
                    highCpuSamples = 0;
                }

                const bool canAdjust =
                    now - lastCapChange >=
                    MIN_TIME_BETWEEN_CHANGES;

                if (canAdjust)
                {
                    const DWORD currentCap =
                        sevenZip.currentCpuCap();

                    DWORD newCap = currentCap;

                    // Low global utilization:
                    // allow 7-Zip to use more CPU.
                    if (lowCpuSamples >=
                        REQUIRED_CONSECUTIVE_SAMPLES)
                    {
                        newCap = std::min(
                            currentCap + CAP_STEP,
                            MAX_DYNAMIC_CAP
                        );
                    }

                    // High global utilization:
                    // restrict 7-Zip to reduce contention.
                    else if (highCpuSamples >=
                        REQUIRED_CONSECUTIVE_SAMPLES)
                    {
                        newCap =
                            currentCap > MIN_DYNAMIC_CAP
                            ? std::max(
                                currentCap - CAP_STEP,
                                MIN_DYNAMIC_CAP
                            )
                            : MIN_DYNAMIC_CAP;
                    }

                    // ========================================
                    // APPLY DYNAMIC CPU CAP
                    // ========================================

                    if (newCap != currentCap)
                    {
                        DWORD capError{};

                        if (sevenZip.setCpuCap(
                            newCap,
                            capError))
                        {
                            lastCapChange = Clock::now();

                            lowCpuSamples = 0;
                            highCpuSamples = 0;

                            recordEvent(
                                "7-Zip",
                                "cpu_cap_dynamic_" +
                                std::to_string(newCap),
                                sevenZip
                            );

                            std::cout
                                << "[Keith Adaptive] "
                                << "System CPU: "
                                << std::fixed
                                << std::setprecision(2)
                                << smoothedSystemCpu
                                << "% | Cap: "
                                << currentCap
                                << "% -> "
                                << newCap
                                << "%\n";
                        }
                        else
                        {
                            experiment.adaptiveReleaseErrorCode =
                                capError;

                            recordEvent(
                                "7-Zip",
                                "cpu_cap_dynamic_failed",
                                sevenZip
                            );

                            ffmpeg.setError(capError);
                            sevenZip.setError(capError);

                            experiment.ffmpeg =
                                ffmpeg.result();

                            experiment.sevenZip =
                                sevenZip.result();

                            return experiment;
                        }
                    }
                }
            }
        }

        // ====================================================
        // LIVE CONSOLE MONITOR
        // ====================================================

        if (Clock::now() >= nextPrint)
        {
            std::cout << std::fixed
                << std::setprecision(2);

            std::cout
                << "FFmpeg CPU: "
                << ffmpeg.cpuUsage()
                << "%";

            if (sevenZipConfig != nullptr)
            {
                std::cout
                    << " | 7-Zip CPU: "
                    << sevenZip.cpuUsage()
                    << "%";

                if (adaptiveRelease)
                {
                    std::cout
                        << " | Cap: "
                        << sevenZip.currentCpuCap()
                        << "%";

                    if (smoothedCpuValid)
                    {
                        std::cout
                            << " | System CPU: "
                            << smoothedSystemCpu
                            << "%";
                    }
                }
            }

            std::cout << '\n';

            nextPrint =
                Clock::now() + printInterval;
        }
    }

    // ========================================================
    // STORE FINAL RESULTS
    // ========================================================

    experiment.ffmpeg = ffmpeg.result();

    if (sevenZipConfig != nullptr)
    {
        experiment.sevenZip = sevenZip.result();

        const auto& a = experiment.ffmpeg;
        const auto& b = experiment.sevenZip;

        if (a.completed && b.completed)
        {
            const double overlapStart =
                std::max(
                    a.startSeconds,
                    b.startSeconds
                );

            const double overlapEnd =
                std::min(
                    a.endSeconds,
                    b.endSeconds
                );

            experiment.overlapSeconds =
                std::max(
                    0.0,
                    overlapEnd - overlapStart
                );

            experiment.startSkewMilliseconds =
                std::abs(
                    a.startSeconds -
                    b.startSeconds
                ) * 1000.0;
        }
    }

    return experiment;
}
