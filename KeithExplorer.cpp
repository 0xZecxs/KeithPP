
#include "KeithExplorer.h"
#include <TlHelp32.h>

bool enumerateProcesses(
    std::vector<ProcessInfo>& processes,
    DWORD& errorCode
)
{
    processes.clear();
    errorCode = ERROR_SUCCESS;

    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPPROCESS, 0
    );

    if (snapshot == INVALID_HANDLE_VALUE)
    {
        errorCode = GetLastError();
        return false;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (!Process32FirstW(snapshot, &entry))
    {
        errorCode = GetLastError();
        CloseHandle(snapshot);
        return false;
    }

    do
    {
        processes.push_back({
            entry.th32ProcessID,
            entry.szExeFile
            });

    } while (Process32NextW(snapshot, &entry));

    DWORD lastError = GetLastError();
    CloseHandle(snapshot);

    if (lastError != ERROR_NO_MORE_FILES)
    {
        errorCode = lastError;
        return false;
    }

    return true;
}
