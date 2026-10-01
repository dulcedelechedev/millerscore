// SPDX-License-Identifier: GPL-3.0-only

#include <windows.h>

#include <cwctype>
#include <string>
#include <vector>

namespace {
std::wstring executableDirectory()
{
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return {};
    }

    std::wstring result(path.data(), length);
    const std::wstring::size_type separator = result.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring() : result.substr(0, separator);
}

const wchar_t* argumentsTail()
{
    const wchar_t* cursor = GetCommandLineW();
    if (*cursor == L'\"') {
        ++cursor;
        while (*cursor && *cursor != L'\"') {
            ++cursor;
        }
        if (*cursor == L'\"') {
            ++cursor;
        }
    } else {
        while (*cursor && !std::iswspace(*cursor)) {
            ++cursor;
        }
    }

    while (std::iswspace(*cursor)) {
        ++cursor;
    }
    return cursor;
}

int showLaunchError(const std::wstring& executable)
{
    const DWORD error = GetLastError();
    std::wstring message = L"MillerScore could not be opened.\n\nExpected executable:\n";
    message += executable;
    message += L"\n\nWindows error: ";
    message += std::to_wstring(error);
    MessageBoxW(nullptr, message.c_str(), L"MillerScore", MB_OK | MB_ICONERROR);
    return static_cast<int>(error == 0 ? ERROR_FILE_NOT_FOUND : error);
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const std::wstring root = executableDirectory();
    if (root.empty()) {
        return showLaunchError(L"build\\bin\\MillerScore.exe");
    }

    const std::wstring binDirectory = root + L"\\bin";
    const std::wstring executable = binDirectory + L"\\MillerScore.exe";
    const DWORD attributes = GetFileAttributesW(executable.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return showLaunchError(executable);
    }

    std::wstring commandLine = L"\"" + executable + L"\"";
    const wchar_t* tail = argumentsTail();
    if (*tail) {
        commandLine += L" ";
        commandLine += tail;
    }

    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startupInfo {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo {};

    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, binDirectory.c_str(), &startupInfo, &processInfo)) {
        return showLaunchError(executable);
    }

    CloseHandle(processInfo.hThread);
    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hProcess);
    return static_cast<int>(exitCode);
}
