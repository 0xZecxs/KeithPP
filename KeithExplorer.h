
#pragma once

#include <Windows.h>
#include <string>
#include <vector>

struct ProcessInfo
{
    DWORD pid{};
    std::wstring name;
};

bool enumerateProcesses(
    std::vector<ProcessInfo>& processes,
    DWORD& errorCode
);
