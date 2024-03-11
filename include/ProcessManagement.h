#pragma once
#include <vector>
#include <iostream>
#include <tuple>
#include <osmanip/manipulators/colsty.hpp>
#define NOMINMAX
#include <Windows.h>
#include "Settings.h"
#include <Psapi.h>
#include <optional>
#include <algorithm>
#include <ranges>

//Starts the given process with the provided arguments
void createProcess(const std::string& path, const std::string& args)
{
    HINSTANCE hInstance = ShellExecuteA(nullptr, "open", path.c_str(), (args + " " + appSettings::get().startArgs).c_str(), nullptr, SW_SHOWDEFAULT);

    if (reinterpret_cast<std::uintptr_t>(hInstance) <= HINSTANCE_ERROR)
    {
        throw std::exception("Failed to start process.\n");
    }
}

//Returns the PIDs of all active processes
std::vector<DWORD> getActiveProcesses()
{
    std::vector<DWORD> result;
    result.resize(1024);

    while (true)
    {
        DWORD bytesUsed;
        if (!EnumProcesses(result.data(), static_cast<DWORD>(result.size() * sizeof(DWORD)), &bytesUsed))
        {
            throw std::exception("Failed to enumerate processes.");
        }

        //Returns the size in bytes, but we want the size of values so divide by the size of an individual value
        size_t used = bytesUsed / sizeof(DWORD);

        //If we ran out of space for results, try again with a larger buffer
        if (used == result.size())
        {
            result.resize(result.size() + 1024);
            continue;
        }
        else
        {
            result.resize(used);
            //Skip those with invalid PIDs
            std::erase_if(result, [](DWORD pid) { return pid == 0; });
            return result;
        }
    }
}

//Represents a PID and all visible windows associated with it
using findWindowUserData = std::pair<DWORD, std::vector<HWND>>;

//Called for each window, filters out those that match the provided PID and are visible
//Userdata must be an instance of findWindowUserData
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto& userData = *reinterpret_cast<findWindowUserData*>(lParam);
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);

    //Some edge windows cam out as popups, so skip those (they aren't the ones we wanted)
    LONG_PTR windowStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
    auto windowIsPopup = windowStyle & WS_POPUP;

    //Only add this window if it's the process we want, visible (not a background process) and a non-popup window
    if (processId == userData.first && IsWindowVisible(hwnd) && !windowIsPopup)
    {
        userData.second.push_back(hwnd);
    }
    return TRUE;
}

//Finds all the visible windows for a given process
std::vector<HWND> FindVisibleWindowsByProcessId(DWORD processId)
{
    findWindowUserData userData{ processId, {} };
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&userData));
    return userData.second;
}

//Finds the most recent process with the given name and pulls all its visible windows
std::vector<std::pair<DWORD, HWND>> getMostRecentProcessesWithName(const std::string& name)
{
    const auto pids = getActiveProcesses();
    FILETIME mostRecentCreationTime = { 0 };

    DWORD finalPid;
    std::vector<HWND> handles;

    for (const auto& pid : pids)
    {
        //Get a process handle we can query
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProcess)
        {
            HMODULE hMod;
            DWORD cbNeeded;

            TCHAR szProcessName[MAX_PATH] = TEXT("<unknown>");

            //Get the name of the process and compare it to the name we're looking for
            if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded))
            {
                GetModuleBaseName(hProcess, hMod, szProcessName,
                    sizeof(szProcessName) / sizeof(TCHAR));
                if (name != szProcessName)
                    continue;
            }
            else
            {
                //If we can't get a process name, skip it
                continue;
            }

            //Get all windows for this process
            auto wHnds = FindVisibleWindowsByProcessId(pid);
            //If there aren't any, skip it
            if (wHnds.empty())
                continue;

            FILETIME createTime, exitTime, kernelTime, userTime;
            if (GetProcessTimes(hProcess, &createTime, &exitTime, &kernelTime, &userTime))
            {
                //Compare the process creation time to our current one, if this is more recent then use it
                if (CompareFileTime(&createTime, &mostRecentCreationTime) > 0)
                {
                    //Update the most recent process information
                    mostRecentCreationTime = createTime;
                    finalPid = pid;
                    handles = wHnds;
                }
            }
            CloseHandle(hProcess);
        }
    }

    //Convert from PIDs/handles to processes
    std::vector<std::pair<DWORD, HWND>> result;
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


//Starts a new instance of the process and adds it to the process list at the given location
[[nodiscard]]
std::optional<std::pair<DWORD, HWND>> startProcess(const std::string& url, std::span<HWND>& existing)
{
    //Start the process, give it a moment to load and then capture its ids
    createProcess(appSettings::get().executableName, url);

    std::this_thread::sleep_for(std::chrono::seconds(appSettings::get().loadTime));
    auto instances = getMostRecentProcessesWithName(appSettings::get().processName);

    //A single process can have many windows, filter out the ones we're already using as we know we don't want them
    std::erase_if(instances, [&](auto l) {return std::ranges::any_of(existing, [&](HWND r) { return r == l.second; }); });

    //Ensure we only find one window, if we find more then it's likely some were already running 
    if (instances.size() == 0)
    {
        //If we haven't found any windows, it's likely they haven't loaded yet and we should reset and try again
        std::cout << osm::feat(osm::col, "orange") << "Failed to register process and will reset. Consider increasing LOADTIME.\n" << osm::feat(osm::rst, "all");
        closeAllExisting();
        return std::nullopt;
    }

    //If we find too many processes, something's gone wrong and we can't continue (we don't know which process to hook onto)
    if (instances.size() > 1)
    {
        std::cout << osm::feat(osm::col, "orange") << "Failed to register process and will reset. Too many processes were found.\n" << osm::feat(osm::rst, "all");
        closeAllExisting();
        return std::nullopt;
    }
    return instances[0];
}
