#include <windows.h>
#include <tlhelp32.h>
#include <iostream>

int main()
{
    // 1. Create a snapshot of running processes.
    HANDLE snapshot{
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    };

    if (snapshot == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Failed to create process snapshot. Error: "
            << GetLastError() << '\n';
        return 1;
    }

    // 2. Prepare the process information structure.
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);

    // 3. Retrieve the first process.
    if (!Process32FirstW(snapshot, &process))
    {
        DWORD error{ GetLastError() };
        CloseHandle(snapshot);

        std::cerr << "Failed to enumerate processes. Error: "
            << error << '\n';
        return 1;
    }

    // 4. Enumerate all processes in the snapshot.
    std::wcout << L"PID\tPROCESS NAME\n";
    std::wcout << L"----------------------------\n";

    do
    {
        std::wcout << process.th32ProcessID
            << L'\t'
            << process.szExeFile
            << L'\n';

    } while (Process32NextW(snapshot, &process));

    // 5. Check why enumeration stopped.
    DWORD error{ GetLastError() };

    // 6. Release the Windows resource.
    CloseHandle(snapshot);

    if (error != ERROR_NO_MORE_FILES)
    {
        std::cerr << "Enumeration failed. Error: "
            << error << '\n';
        return 1;
    }

    return 0;
}
