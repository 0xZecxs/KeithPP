#include "KeithBench.h"

#include <chrono>

BenchmarkResult runBenchmark(
    const BenchmarkConfig& config
)
{
    BenchmarkResult result{};

    // Configure inheritable handles.
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    // Create a file for benchmark output.
    HANDLE logFile = CreateFileW(
        config.logFile.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &security,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (logFile == INVALID_HANDLE_VALUE)
    {
        result.errorCode = GetLastError();
        return result;
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
        result.errorCode = GetLastError();

        CloseHandle(logFile);
        return result;
    }

    // Configure child process input and output.
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;

    startup.hStdInput = input;
    startup.hStdOutput = logFile;
    startup.hStdError = logFile;

    PROCESS_INFORMATION process{};

    // Build the command line.
    std::wstring commandLine =
        L"\"" + config.executable + L"\" "
        + config.arguments;

    // Start measuring elapsed time.
    const auto start =
        std::chrono::steady_clock::now();

    // Start the benchmark process.
    BOOL created = CreateProcessW(
        config.executable.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | config.priorityClass,
        nullptr,
        nullptr,
        &startup,
        &process
    );

    DWORD creationError =
        created ? ERROR_SUCCESS : GetLastError();

    // The parent no longer needs these handles.
    CloseHandle(input);
    CloseHandle(logFile);

    if (!created)
    {
        result.errorCode = creationError;
        return result;
    }

    CloseHandle(process.hThread);

    // Check the process priority.
    result.observedPriority =
        GetPriorityClass(process.hProcess);

    // Wait for the benchmark to finish.
    DWORD waitResult = WaitForSingleObject(
        process.hProcess,
        INFINITE
    );

    if (waitResult != WAIT_OBJECT_0)
    {
        result.errorCode =
            waitResult == WAIT_FAILED
            ? GetLastError()
            : ERROR_GEN_FAILURE;

        CloseHandle(process.hProcess);
        return result;
    }

    // Retrieve the application exit code.
    DWORD exitCode{};

    if (!GetExitCodeProcess(
        process.hProcess,
        &exitCode))
    {
        result.errorCode = GetLastError();

        CloseHandle(process.hProcess);
        return result;
    }

    // Stop measuring elapsed time.
    const auto end =
        std::chrono::steady_clock::now();

    result.elapsedSeconds =
        std::chrono::duration<double>(
            end - start
        ).count();

    result.exitCode = exitCode;
    result.completed = true;

    // Release the process handle.
    CloseHandle(process.hProcess);

    return result;
}
