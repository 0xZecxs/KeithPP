#include "KeithCore.h"

// ============================================================
// KEITHCORE - PROCESS PRIORITY MANAGEMENT
// ============================================================

KeithCore::~KeithCore()
{
    if (processHandle_ != nullptr)
    {
        DWORD errorCode{};

        restore(errorCode);

        CloseHandle(processHandle_);
        processHandle_ = nullptr;
    }
}

bool KeithCore::open(
    DWORD pid,
    DWORD& errorCode)
{
    if (processHandle_ != nullptr)
    {
        errorCode = ERROR_ALREADY_EXISTS;
        return false;
    }

    processHandle_ = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION |
        PROCESS_SET_INFORMATION |
        SYNCHRONIZE,
        FALSE,
        pid
    );

    if (processHandle_ == nullptr)
    {
        errorCode = GetLastError();
        return false;
    }

    originalPriority_ = GetPriorityClass(processHandle_);

    if (originalPriority_ == 0)
    {
        errorCode = GetLastError();

        CloseHandle(processHandle_);
        processHandle_ = nullptr;

        return false;
    }

    modified_ = false;

    errorCode = ERROR_SUCCESS;
    return true;
}

// ============================================================
// APPLY PROCESS PRIORITY
// ============================================================

bool KeithCore::setPriority(
    DWORD priorityClass,
    DWORD& errorCode)
{
    if (processHandle_ == nullptr)
    {
        errorCode = ERROR_INVALID_HANDLE;
        return false;
    }

    // Restrict priority changes to safe experimental classes.
    if (priorityClass != BELOW_NORMAL_PRIORITY_CLASS &&
        priorityClass != NORMAL_PRIORITY_CLASS &&
        priorityClass != ABOVE_NORMAL_PRIORITY_CLASS)
    {
        errorCode = ERROR_INVALID_PARAMETER;
        return false;
    }

    if (!SetPriorityClass(
        processHandle_,
        priorityClass))
    {
        errorCode = GetLastError();
        return false;
    }

    modified_ = true;

    // Verify the priority applied by Windows.
    const DWORD verifiedPriority =
        GetPriorityClass(processHandle_);

    if (verifiedPriority == 0)
    {
        errorCode = GetLastError();
        return false;
    }

    if (verifiedPriority != priorityClass)
    {
        errorCode = ERROR_INVALID_DATA;
        return false;
    }

    errorCode = ERROR_SUCCESS;
    return true;
}

// ============================================================
// RESTORE ORIGINAL PRIORITY
// ============================================================

bool KeithCore::restore(DWORD& errorCode)
{
    if (processHandle_ == nullptr)
    {
        errorCode = ERROR_INVALID_HANDLE;
        return false;
    }

    if (!modified_)
    {
        errorCode = ERROR_SUCCESS;
        return true;
    }

    if (!SetPriorityClass(
        processHandle_,
        originalPriority_))
    {
        errorCode = GetLastError();
        return false;
    }

    const DWORD verifiedPriority =
        GetPriorityClass(processHandle_);

    if (verifiedPriority == 0)
    {
        errorCode = GetLastError();
        return false;
    }

    if (verifiedPriority != originalPriority_)
    {
        errorCode = ERROR_INVALID_DATA;
        return false;
    }

    modified_ = false;

    errorCode = ERROR_SUCCESS;
    return true;
}

// ============================================================
// PROCESS ACCESSORS
// ============================================================

HANDLE KeithCore::handle() const noexcept
{
    return processHandle_;
}

DWORD KeithCore::originalPriority() const noexcept
{
    return originalPriority_;
}

// ============================================================
// PRIORITY CLASS NAMES
// ============================================================

const wchar_t* priorityClassName(
    DWORD priorityClass)
{
    switch (priorityClass)
    {
    case IDLE_PRIORITY_CLASS:
        return L"Idle";

    case BELOW_NORMAL_PRIORITY_CLASS:
        return L"Below Normal";

    case NORMAL_PRIORITY_CLASS:
        return L"Normal";

    case ABOVE_NORMAL_PRIORITY_CLASS:
        return L"Above Normal";

    case HIGH_PRIORITY_CLASS:
        return L"High";

    case REALTIME_PRIORITY_CLASS:
        return L"Realtime";

    default:
        return L"Unknown";
    }
}

// ============================================================
// KEITH CPU CAP - JOB OBJECT MANAGEMENT
// ============================================================

KeithCpuCap::~KeithCpuCap()
{
    if (job_ != nullptr)
    {
        CloseHandle(job_);
        job_ = nullptr;
    }
}

// ============================================================
// CREATE CPU HARD CAP
// ============================================================

bool KeithCpuCap::createHardCap(
    DWORD percent,
    DWORD& errorCode)
{
    errorCode = ERROR_SUCCESS;

    // Valid CPU cap range: 1% to 100%.
    if (percent < 1 || percent > 100)
    {
        errorCode = ERROR_INVALID_PARAMETER;
        return false;
    }

    if (job_ != nullptr)
    {
        errorCode = ERROR_ALREADY_EXISTS;
        return false;
    }

    HANDLE job = CreateJobObjectW(
        nullptr,
        nullptr
    );

    if (job == nullptr)
    {
        errorCode = GetLastError();
        return false;
    }

    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION info{};

    info.ControlFlags =
        JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |
        JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;

    // Windows uses units of 1/100 percent.
    // 25% = 2500, 50% = 5000, 100% = 10000.
    info.CpuRate = percent * 100;

    if (!SetInformationJobObject(
        job,
        JobObjectCpuRateControlInformation,
        &info,
        sizeof(info)))
    {
        errorCode = GetLastError();

        CloseHandle(job);
        return false;
    }

    // Read back the effective Job Object configuration.
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION observed{};

    if (!QueryInformationJobObject(
        job,
        JobObjectCpuRateControlInformation,
        &observed,
        sizeof(observed),
        nullptr))
    {
        errorCode = GetLastError();

        CloseHandle(job);
        return false;
    }

    const DWORD requiredFlags =
        JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |
        JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;

    if (observed.CpuRate != info.CpuRate ||
        (observed.ControlFlags & requiredFlags)
        != requiredFlags)
    {
        errorCode = ERROR_INVALID_DATA;

        CloseHandle(job);
        return false;
    }

    // Save the verified Job Object configuration.
    job_ = job;
    percent_ = percent;

    errorCode = ERROR_SUCCESS;
    return true;
}

// ============================================================
// ATTACH PROCESS TO JOB OBJECT
// ============================================================

bool KeithCpuCap::attachProcess(
    HANDLE processHandle,
    DWORD& errorCode) const
{
    errorCode = ERROR_SUCCESS;

    if (job_ == nullptr ||
        processHandle == nullptr ||
        processHandle == INVALID_HANDLE_VALUE)
    {
        errorCode = ERROR_INVALID_HANDLE;
        return false;
    }

    if (!AssignProcessToJobObject(
        job_,
        processHandle))
    {
        errorCode = GetLastError();
        return false;
    }

    // Verify that Windows associated the process with our Job.
    BOOL inJob = FALSE;

    if (!IsProcessInJob(
        processHandle,
        job_,
        &inJob))
    {
        errorCode = GetLastError();
        return false;
    }

    if (inJob == FALSE)
    {
        errorCode = ERROR_INVALID_DATA;
        return false;
    }

    errorCode = ERROR_SUCCESS;
    return true;
}

// ============================================================
// KEITH++ V0.8 - DYNAMIC CPU CAP UPDATE
// ============================================================

bool KeithCpuCap::updateHardCap(
    DWORD newPercent,
    DWORD& errorCode)
{
    errorCode = ERROR_SUCCESS;

    // A Job Object must already exist.
    if (job_ == nullptr ||
        job_ == INVALID_HANDLE_VALUE)
    {
        errorCode = ERROR_INVALID_HANDLE;
        return false;
    }

    // Reject invalid CPU percentages.
    if (newPercent < 1 || newPercent > 100)
    {
        errorCode = ERROR_INVALID_PARAMETER;
        return false;
    }

    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION info{};

    info.ControlFlags =
        JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |
        JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;

    info.CpuRate = newPercent * 100;

    // Change the CPU hard cap of the existing Job Object.
    // The processes remain associated with the same Job.
    if (!SetInformationJobObject(
        job_,
        JobObjectCpuRateControlInformation,
        &info,
        sizeof(info)))
    {
        errorCode = GetLastError();
        return false;
    }

    // Verify the updated CPU cap.
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION observed{};

    if (!QueryInformationJobObject(
        job_,
        JobObjectCpuRateControlInformation,
        &observed,
        sizeof(observed),
        nullptr))
    {
        errorCode = GetLastError();
        return false;
    }

    const DWORD requiredFlags =
        JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |
        JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;

    if ((observed.ControlFlags & requiredFlags)
        != requiredFlags ||
        observed.CpuRate != info.CpuRate)
    {
        errorCode = ERROR_INVALID_DATA;
        return false;
    }

    // Update the stored value only after verification.
    percent_ = newPercent;

    errorCode = ERROR_SUCCESS;
    return true;
}

// ============================================================
// CURRENT CPU CAP
// ============================================================

DWORD KeithCpuCap::percent() const noexcept
{
    return percent_;
}
