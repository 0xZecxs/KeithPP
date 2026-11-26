# Keith++

### CPU Monitoring, Management, and Performance Evaluation in Windows 11

**Authors:** Samuel Arandia, Miguel Zarabanda, Sebastian Rojas  
**University:** Universidad del Rosario  
**Course:** Operating Systems  
**Language:** C++20  
**Platform:** Windows 11  
**API:** Win32 API

---

## 1. Project Overview

Keith++ is a Windows 11 CPU monitoring and management tool developed using C++20 and the Win32 API.

The project investigates how different CPU management policies affect the performance of concurrent, CPU-intensive applications.

Keith++ allows users to modify process priority classes, enforce CPU usage limits through Windows Job Objects, and dynamically adjust CPU restrictions based on system utilization.

The project does not replace the Windows scheduler or modify the operating system kernel. Instead, it uses native Windows mechanisms to influence process execution and experimentally evaluate their effects.

## 2. Project Objectives

The main objectives of Keith++ are:

- Monitor CPU utilization and process execution.
- Modify process priority classes.
- Apply CPU restrictions using Windows Job Objects.
- Implement adaptive CPU resource management.
- Measure the performance of concurrent workloads.
- Compare individual application performance and overall completion time.

## 3. System Requirements

### Operating System

- Windows 11 (64-bit).

### Development Environment

- Visual Studio 2022 or later.
- Desktop development with C++ workload.
- MSVC compiler with C++20 support.
- Windows SDK.

### Hardware

- Multicore x64 processor.
- 8 GB RAM recommended.
- 16 GB RAM preferred.

### Required Software

**FFmpeg**

Used to perform CPU-intensive video encoding.

Official website:
https://ffmpeg.org/

The installed build must support the libx264 encoder.

**7-Zip**

Used to execute a multithreaded CPU benchmark.

Official website:
https://www.7-zip.org/

The command-line executable `7z.exe` is required.

### Input File

A video file named `input.avi` is used for the FFmpeg workload.

To reproduce the original experiments, use the same input video and benchmark configuration.

No dedicated GPU, CUDA, Python, or additional third-party C++ libraries are required to execute Keith++.

## 4. Project Architecture

Keith++ is divided into several modules.

### KeithCore

Responsible for interacting with Windows process-management mechanisms.

Main responsibilities:

- Process priority management.
- Windows Job Object management.
- CPU resource restrictions.
- CPU quota updates.

### KeithWatch

Responsible for collecting CPU performance measurements.

Main responsibilities:

- Process CPU time monitoring.
- CPU utilization calculations.
- Execution sampling.

### KeithExperiment

Responsible for managing experimental execution.

Main responsibilities:

- Creating FFmpeg and 7-Zip processes.
- Applying CPU management policies.
- Executing concurrent workloads.
- Detecting process completion.
- Recording execution events.
- Managing adaptive CPU restrictions.

### KeithBench

Defines benchmark configurations and experimental parameters.

### main.cpp

The main entry point of the application.

Responsibilities include:

- Configuring executable paths.
- Defining experimental scenarios.
- Running experimental repetitions.
- Processing benchmark output.
- Exporting measurements into CSV files.

## 5. Win32 API Functions

Keith++ uses several Windows API functions:

| Function | Purpose |
|---|---|
| CreateProcessW | Creates benchmark processes |
| ResumeThread | Starts a suspended process |
| SetPriorityClass | Changes process priority |
| GetPriorityClass | Retrieves process priority |
| GetProcessTimes | Measures process CPU time |
| GetSystemTimes | Measures system CPU activity |
| CreateJobObjectW | Creates a Job Object |
| AssignProcessToJobObject | Associates a process with a Job Object |
| SetInformationJobObject | Applies CPU restrictions |
| QueryInformationJobObject | Verifies Job Object settings |

Windows schedules threads rather than entire processes. Process priority classes influence thread scheduling preferences but do not guarantee fixed CPU allocations.

CPU hard caps, in contrast, impose resource restrictions through the operating system.

## 6. CPU Management Policies

Keith++ v0.8 implements five experimental policies.

### 1. Windows Default

- FFmpeg: Normal Priority.
- 7-Zip: Normal Priority.
- CPU Cap: None.

This is the reference configuration.

### 2. Keith Priority

- FFmpeg: Above Normal Priority.
- 7-Zip: Below Normal Priority.
- CPU Cap: None.

The objective is to favor FFmpeg through Windows scheduling priorities.

### 3. Keith Static 25

- FFmpeg: Normal Priority.
- 7-Zip: Normal Priority.
- 7-Zip CPU Cap: 25%.

The restriction remains active throughout the benchmark.

### 4. Keith Adaptive 25

- FFmpeg: Normal Priority.
- 7-Zip: Normal Priority.
- Initial 7-Zip CPU Cap: 25%.

The controller adjusts the CPU quota based on global CPU utilization.

### 5. Keith Hybrid

- FFmpeg: Above Normal Priority.
- 7-Zip: Below Normal Priority.
- Initial 7-Zip CPU Cap: 25%.

This policy combines priority modification with adaptive CPU resource management.

## 7. Adaptive Controller

The adaptive controller monitors global CPU utilization and adjusts the CPU cap assigned to 7-Zip.

Decision rules:

- CPU utilization below 70%: increase the CPU cap.
- CPU utilization between 70% and 90%: maintain the current cap.
- CPU utilization above 90%: decrease the CPU cap.

The controller changes the limit in increments of 25 percentage points.

During concurrent execution, the supported range is 25% to 75%.

To avoid excessive changes:

- Two consecutive qualifying samples are required.
- At least five seconds must pass between adjustments.

When FFmpeg completes, Keith++ increases the 7-Zip CPU quota to 100%.

## 8. Configuration

### Open the project

Open the Visual Studio solution file (`.sln`) included in the repository.

### Configure Visual Studio

Select:

- Platform: x64.
- C++ Language Standard: ISO C++20.
- Configuration: Debug or Release.

### Install dependencies

Install FFmpeg and 7-Zip.

Verify that both executables are available.

### Configure executable paths

Open `main.cpp`.

Locate the variables:

```cpp
const std::wstring ffmpegExe;
const std::wstring sevenZipExe;
const std::wstring inputFile;
```

Update their assigned paths to match your computer.

Example:

```cpp
const std::wstring ffmpegExe =
    LR"(C:\Tools\ffmpeg\bin\ffmpeg.exe)";

const std::wstring sevenZipExe =
    LR"(C:\Program Files\7-Zip\7z.exe)";

const std::wstring inputFile =
    LR"(C:\KeithBench\input.avi)";
```

These are example paths. Use the actual locations of the files on your system.

### Build the project

In Visual Studio:

1. Open the solution.
2. Select the x64 platform.
3. Select Build > Build Solution.
4. Confirm that compilation finishes successfully.

### Run Keith++

Execute the application using Visual Studio.

Keith++ will launch the benchmark processes, apply the configured CPU management policies, collect measurements, and save results.

For an initial functional test, configure one repetition in the source code.

For the complete v0.8 experiment, use six repetitions per policy.

## 9. Experimental Workloads

### FFmpeg

The benchmark uses:

- libx264 video encoding.
- Medium encoding preset.
- Constant Rate Factor (CRF): 23.
- Repeated video input.
- Output discarded to NUL.

FFmpeg execution time is one of the primary performance metrics.

### 7-Zip

The benchmark uses:

- Built-in CPU benchmark.
- Ten benchmark passes.
- Dictionary parameter: -md22.
- Multithreaded execution.
- Up to 16 threads, depending on the available logical processors.

The application records execution time and benchmark performance in MIPS.

## 10. Experimental Results

Keith++ was evaluated in two stages.

### Version 0.7

- 6 experimental scenarios.
- 6 repetitions per scenario.
- 36 experimental runs.

### Version 0.8

- 5 experimental scenarios.
- 6 repetitions per scenario.
- 30 experimental runs.

**Total: 66 experimental runs.**

### Final Results: Version 0.8

| Policy | FFmpeg (s) | Makespan (s) |
|---|---:|---:|
| Windows Default | 27.497 | 39.338 |
| Keith Priority | 27.455 | 39.890 |
| Keith Static 25 | 23.385 | 76.005 |
| Keith Adaptive 25 | 26.697 | 43.653 |
| Keith Hybrid | 25.261 | 43.116 |

Values represent averages across six repetitions.

### Main Findings

**Keith Static 25**

- Reduced FFmpeg execution time by 14.96%.
- Increased overall makespan by 93.21%.

**Keith Hybrid**

- Reduced FFmpeg execution time by 8.13%.
- Increased overall makespan by 9.60%.

**Windows Default**

- Achieved the lowest average makespan.

These results demonstrate that improving the execution time of one application does not necessarily reduce the total completion time of all concurrent workloads.

## 11. Output Files

Keith++ exports experimental data as CSV files.

Depending on the version, these include:

- `keith_results.csv`
- `keith_events.csv`
- `keith_results_v08.csv`
- `keith_events_v08.csv`

The results files contain measurements such as:

- FFmpeg execution time.
- 7-Zip benchmark performance.
- CPU utilization.
- Process CPU time.
- Process start and end timestamps.
- CPU cap configuration.

The event files record important actions, including process execution, applied priorities, CPU quota changes, and process completion.

Output locations depend on the working directory configured in Visual Studio.

Existing result files may be overwritten by new experimental executions. Preserve the original datasets before rerunning benchmarks.





---

