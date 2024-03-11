#pragma once
#include "Rect.h"
#include "Settings.h"
#include <thread>
#include <chrono>
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>

//Represents a running instance of the browser
class process
{
    //Sends a keypress event to the window
    void simulateKey(WORD vkCode, bool press)
    {
        if (!valid())
            return;

        WPARAM wParam = vkCode;
        LPARAM lParam = 0;

        if (!press)
            lParam |= KEYEVENTF_KEYUP;

        //Generate the corresponding keyboard messages and send them
        UINT message = press ? WM_KEYDOWN : WM_KEYUP;
        SendMessage(windowHandle, message, wParam, lParam);
    }

public:
    DWORD processID = 0;
    HWND windowHandle = nullptr;
    std::string url;

    process(DWORD pid, HWND handle) : processID(pid), windowHandle(handle) {}

    //Returns true if the process is a valid window
    bool valid() const { return windowHandle && IsWindow(windowHandle); }

    //Waits for the process to be ready to accept input
    bool waitForProcessIdle(DWORD timeoutMillis = INFINITE) const
    {
        if (!valid())
            return false;

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processID);
        if (hProcess)
        {
            DWORD waitResult = WaitForInputIdle(hProcess, timeoutMillis);
            CloseHandle(hProcess);
            return (waitResult == 0);
        }
        //Failed to open the process
        return false;
    }

    //Sends a close request to the window
    void close() const
    {
        if (valid())
        {
            PostMessage(windowHandle, WM_CLOSE, 0, 0);
        }
    }

    //Sends a keypress to the window
    void sendMessage(WORD vkCode)
    {
        if (!valid())
            return;

        //Try and get window focus before sending keycodes
        waitForProcessIdle();
        bringToForeground();

        //Send a down, then an up (otherwise the window will think we're holding the key)
        simulateKey(vkCode, true);
        simulateKey(vkCode, false);
    }

    //Attempts to move the window to the given area and full screen it
    void moveToMonitor(rect monitor)
    {
        if (valid())
        {
            //Only try up to 5 times to sort the window, otherwise ignore it and move on
            int fail = 5;
            while (!getBounds().approximately(monitor) && fail-- > 0)
            {
                //Move to the given monitor
                SetWindowPos(windowHandle, NULL, monitor.left, monitor.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
                //Give the window a moment to relocate
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                //If the window was already full screened, don't undo it
                //Moving it should already have returned it to a window
                if (!getBounds().approximately(monitor))
                {
                    //Otherwise make it full screen
                    sendMessage(VK_F11);
                }
            }

            //When edge full screens, it likes to put a pop-up telling you how to undo it, but it doesn't dissappear until the process gets some key inputs
            //So after fullscreening, blast it with some inputs on the shift key (this shouldn't affect page content)
            for (int i = 0; i < 10; i++)
                sendMessage(VK_SHIFT);
        }
    }

    //Puts the focus on the window
    void bringToForeground()
    {
        //Restore and give focus
        if (IsIconic(windowHandle))
        {
            ShowWindow(windowHandle, SW_RESTORE);
        }

        SetForegroundWindow(windowHandle);
        SetFocus(windowHandle);
    }

    //Gets the window area
    rect getBounds() const
    {
        RECT windowBounds;
        GetWindowRect(windowHandle, &windowBounds);
        return { windowBounds.left, windowBounds.top, windowBounds.right - windowBounds.left, windowBounds.bottom - windowBounds.top };
    }
};

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
std::vector<process> getMostRecentProcessesWithName(const std::string& name)
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
    std::vector<process> result;
    for (const auto i : handles)
        result.emplace_back(finalPid, i);
    return result;
}
