#ifdef _WIN32
#include <osmanip/manipulators/colsty.hpp>
#include "ProcessManagement.h"
#include <Psapi.h>
#include <thread>
#include <chrono>
#include <optional>
#include <algorithm>
#include <ranges>
#include "Settings.h"
#include <iostream>
#define NOMINMAX
#include <Windows.h>
#undef RGB //Windows leaks this macro and it conflicts with osmanip

void createProcess(const std::string& path, const std::string& args)
{
    HINSTANCE hInstance = ShellExecuteA(nullptr, "open", path.c_str(), (args + " --new-window " + appSettings::get().startArgs).c_str(), nullptr, SW_SHOWDEFAULT);
    if (reinterpret_cast<std::uintptr_t>(hInstance) <= HINSTANCE_ERROR)
    {
        throw std::exception("Failed to start process.\n");
    }
}

std::vector<processId> getActiveProcesses()
{
    std::vector<processId> result(1024);
    while (true)
    {
        processId bytesUsed;
        if (!EnumProcesses(result.data(), static_cast<DWORD>(result.size() * sizeof(processId)), &bytesUsed))
        {
            throw std::exception("Failed to enumerate processes.");
        }
        size_t used = bytesUsed / sizeof(processId);
        if (used == result.size())
        {
            result.resize(result.size() + 1024);
            continue;
        }
        else
        {
            result.resize(used);
            std::erase_if(result, [](processId pid) { return pid == 0; });
            return result;
        }
    }
}

//Represents a PID and all visible windows associated with it
using findWindowUserData = std::pair<DWORD, std::vector<HWND>>;

BOOL CALLBACK EnumWindowsProc(windowHandle hwnd, LPARAM lParam)
{
    auto& userData = *reinterpret_cast<findWindowUserData*>(lParam);
    processId procId;
    GetWindowThreadProcessId(hwnd, &procId);
    LONG_PTR windowStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
    auto windowIsPopup = windowStyle & WS_POPUP;
    if (procId == userData.first && IsWindowVisible(hwnd) && !windowIsPopup)
    {
        userData.second.push_back(hwnd);
    }
    return TRUE;
}

std::vector<windowHandle> FindVisibleWindowsByProcessId(processId procId)
{
    findWindowUserData userData{ procId, {} };
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&userData));
    return userData.second;
}

std::vector<std::pair<processId, windowHandle>> getMostRecentProcessesWithName(const std::string& name)
{
    const auto pids = getActiveProcesses();
    FILETIME mostRecentCreationTime = { 0 };
    processId finalPid = 0;
    std::vector<windowHandle> handles;
    for (const auto& pid : pids)
    {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProcess)
        {
            HMODULE hMod;
            DWORD cbNeeded;
            TCHAR szProcessName[MAX_PATH] = TEXT("<unknown>");
            if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded))
            {
                GetModuleBaseName(hProcess, hMod, szProcessName, sizeof(szProcessName) / sizeof(TCHAR));
                if (name != szProcessName)
                    continue;
            }
            else
            {
                continue;
            }
            auto wHnds = FindVisibleWindowsByProcessId(pid);
            if (wHnds.empty())
                continue;
            FILETIME createTime, exitTime, kernelTime, userTime;
            if (GetProcessTimes(hProcess, &createTime, &exitTime, &kernelTime, &userTime))
            {
                if (CompareFileTime(&createTime, &mostRecentCreationTime) > 0)
                {
                    mostRecentCreationTime = createTime;
                    finalPid = pid;
                    handles = wHnds;
                }
            }
            CloseHandle(hProcess);
        }
    }
    std::vector<std::pair<processId, windowHandle>> result;
    for (const auto i : handles)
        result.emplace_back(finalPid, i);
    return result;
}

void closeAllExisting()
{
    std::cout << osm::feat(osm::col, "orange") << "Closing all instances of " << appSettings::get().processName << ".\n" << osm::feat(osm::rst, "all");
    auto processes = getMostRecentProcessesWithName(appSettings::get().processName);
    for (auto& i : processes)
        PostMessage(i.second, WM_CLOSE, 0, 0);
}

std::optional<std::pair<processId, windowHandle>> startProcess(const std::string& url, std::span<const windowHandle> existing, windowHandle self)
{
    createProcess(appSettings::get().executableName, url);
    std::this_thread::sleep_for(std::chrono::seconds(appSettings::get().loadTime));
    auto instances = getMostRecentProcessesWithName(appSettings::get().processName);
    std::erase_if(instances, [&](const auto& l) 
        { return l.second != self && std::find(existing.begin(), existing.end(), l.second) != existing.end(); });
    if (instances.size() == 0)
    {
        std::cout << osm::feat(osm::col, "orange") << "Failed to register process and will reset. Consider increasing LOADTIME.\n" << osm::feat(osm::rst, "all");
        closeAllExisting();
        return std::nullopt;
    }
    if (instances.size() > 1)
    {
        std::cout << osm::feat(osm::col, "orange") << "Failed to register process and will reset. Too many processes were found.\n" << osm::feat(osm::rst, "all");
        closeAllExisting();
        return std::nullopt;
    }
    return instances[0];
}
#endif