#pragma once

#include <Windows.h>

// ============================================================
// KEITHCORE - PROCESS PRIORITY MANAGEMENT
// ============================================================

class KeithCore
{
public:
    KeithCore() = default;
    ~KeithCore();

    // Prevent accidental copying of process handles.
    KeithCore(const KeithCore&) = delete;
    KeithCore& operator=(const KeithCore&) = delete;

    // Open an existing process by PID.
    bool open(
        DWORD pid,
        DWORD& errorCode
    );

    // Apply a Windows process priority class.
    bool setPriority(
        DWORD priorityClass,
        DWORD& errorCode
    );

    // Restore the original priority class.
    bool restore(DWORD& errorCode);

    // Process information.
    HANDLE handle() const noexcept;

    DWORD originalPriority() const noexcept;

private:
    HANDLE processHandle_{ nullptr };

    DWORD originalPriority_{};

    bool modified_{ false };
};

// Convert a priority class to a readable name.
const wchar_t* priorityClassName(
    DWORD priorityClass
);

// ============================================================
// KEITH CPU CAP - JOB OBJECT MANAGEMENT
// ============================================================

class KeithCpuCap
{
public:
    KeithCpuCap() = default;
    ~KeithCpuCap();

    // Prevent accidental copying of the Job Object handle.
    KeithCpuCap(const KeithCpuCap&) = delete;
    KeithCpuCap& operator=(const KeithCpuCap&) = delete;

    // Create a Job Object with an initial CPU hard cap.
    // Valid values: 1-100.
    bool createHardCap(
        DWORD percent,
        DWORD& errorCode
    );

    // Associate a process with the Job Object.
    // Verifies membership using IsProcessInJob().
    bool attachProcess(
        HANDLE processHandle,
        DWORD& errorCode
    ) const;

    // ========================================================
    // KEITH++ V0.8 - DYNAMIC CPU CAP
    // ========================================================

    // Modify the CPU limit of an existing Job Object.
    // Does not create a new Job or restart the process.
    // Valid values: 1-100.
    bool updateHardCap(
        DWORD newPercent,
        DWORD& errorCode
    );

    // Return the last successfully verified CPU cap.
    DWORD percent() const noexcept;

private:
    // Handle of the Job Object owned by this instance.
    HANDLE job_{ nullptr };

    // Currently configured CPU cap percentage.
    DWORD percent_{};
};
