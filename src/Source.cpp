//Make sure to include this BEFORE including the windows header, as they have conflicting names
#include <osmanip/manipulators/colsty.hpp>

#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include <vector>
#include <exception>
#include <algorithm>
#include <utility>
#include <thread>
#include <Psapi.h>
#include <fstream>
#include <string>
#include <iomanip>
#include <sstream>
#include <array>
#include <filesystem>

//Path to settings file
static constexpr auto settingsSource = "Settings.txt";

struct appSettings
{
    //The executable location to be run
    std::string executableName = "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";
    //The process name of the executable
    std::string processName = "msedge.exe";
    //Args passed to process on start
    std::string startArgs = "--new-window";

    enum class invalidMonitorMode
    {
        FAIL,   //Stop program
        PASS,   //Show as many as possible
        NONE    //Don't show any
    };

    static constexpr std::array<std::pair<std::string_view, appSettings::invalidMonitorMode>, 3> monitorModeConversions
    {
        std::pair<std::string_view, appSettings::invalidMonitorMode>{ "FAIL", appSettings::invalidMonitorMode::FAIL },
        std::pair<std::string_view, appSettings::invalidMonitorMode>{ "PASS", appSettings::invalidMonitorMode::PASS },
        std::pair<std::string_view, appSettings::invalidMonitorMode>{ "NONE", appSettings::invalidMonitorMode::NONE },
    };

    //How to act when we don't have the correct number of monitors
    invalidMonitorMode monitorMode = invalidMonitorMode::PASS;
    //Expected number of monitors
    int monitors = 1;

    //How many seconds to wait before checking files again
    int refreshTime = 2;
    //Source urls file
    std::string urlsFile = "URLs.txt";

    //Whether to close all instances of the process on start up
    bool closeAllOnStart = true;
    //How long to wait after starting a process before trying to pull its PID/HWND
    int loadTime = 1;
};

appSettings settings;

//Returns true if l/r are within +/-v of each other
bool within(int l, int r, int v)
{
    return std::abs(l - r) <= v;
}

//Used to represent screen/window sizes
struct rect
{
    static constexpr int comparisonLeeway = 3;

    int left, top, width, height;

    bool operator==(const rect&) const = default;

    //Returns true if the rects are nearly the same, within comparisonLeeway units
    bool approximately(const rect& other) const
    {
        return
            within(left, other.left, comparisonLeeway) && 
            within(top, other.top, comparisonLeeway) && 
            within(width, other.width, comparisonLeeway) && 
            within(height, other.height, comparisonLeeway);
    }
};

//Called once for each monitor, records monitor details to the user data, which must be a std::vector<rect>
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
    auto& monitors = *reinterpret_cast<std::vector<rect>*>(dwData);
    //Convert from a windows rect to our rect
    monitors.push_back({ lprcMonitor->left, lprcMonitor->top, lprcMonitor->right - lprcMonitor->left, lprcMonitor->bottom - lprcMonitor->top });
    return TRUE;
}

//Returns an ordered list of rects representing monitor spaces, ordered left to right, top to bottom
std::vector<rect> getMonitors()
{
    std::vector<rect> result;
    if (!EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&result)))
    {
        throw std::exception("Failed to enumerate monitors");
    }
    std::sort(result.begin(), result.end(), [](const rect& a, const rect& b) 
        {
            //Compare left, unless equal then compare top
            return a.left == b.left ? a.top < b.top : a.left < b.left; 
        });
    return result;
}

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
        auto r = SendMessage(windowHandle, message, wParam, lParam);
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
    HINSTANCE hInstance = ShellExecuteA(nullptr, "open", path.c_str(), (args + " " + settings.startArgs).c_str(), nullptr, SW_SHOWDEFAULT);

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
        if (!EnumProcesses(result.data(), result.size() * sizeof(DWORD), &bytesUsed))
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

//Starts a new instance of the process and adds it to the process list at the given location
[[nodiscard]]
bool addProcess(std::vector<process>& processes, const std::string& url, int sleepSecs, size_t where)
{
    //Start the process, give it a moment to load and then capture its ids
    createProcess(settings.executableName, url);
    
    std::this_thread::sleep_for(std::chrono::seconds(sleepSecs));
    auto instances = getMostRecentProcessesWithName(settings.processName);

    //Record the urls
    for (auto& i : instances)
        i.url = url;

    //A single process can have many windows, filter out the ones we're already using as we know we don't want them
    std::erase_if(instances, [&](process l) {return std::ranges::any_of(processes, [&](process r) { return r.windowHandle == l.windowHandle; }); });

    //Ensure we only find one window, if we find more then it's likely some were already running 
    if (instances.size() == 0)
    {
        //If we haven't found any windows, it's likely they haven't loaded yet and we should reset and try again
        std::cout << osm::feat(osm::col, "orange") << "Failed to register process and will reset. Consider increasing LOADTIME.\n";
        return false;
    }

    //If we find too many processes, something's gone wrong and we can't continue (we don't know which process to hook onto)
    if (instances.size() > 1)
    {
        std::cout << osm::feat(osm::col, "orange") << "Failed to register process and will reset. Too many processes were found.\n";
        return false;
    }

    processes.insert(processes.begin() + where, instances[0]);
    return true;
}

//Closes all instances of the process
void closeAllExisting()
{
    std::cout << osm::feat(osm::col, "orange") << "Closing all instances of " << settings.processName << ".\n";
    auto processes = getMostRecentProcessesWithName(settings.processName);
    for (auto& i : processes)
        i.close();
}

//Checks if the file update time has been reset since the given time
bool hasFileUpdatedSince(const std::string_view& filePath, const std::chrono::system_clock::time_point specificTime)
{
    if (!std::filesystem::exists(filePath))
        return false;
    auto updateTime = std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(filePath));
    return updateTime > specificTime;
}

struct basicWatch
{
    enum class action
    {
        RESET,
        REFRESH
    };

    static constexpr std::array<std::pair<std::string_view, basicWatch::action>, 2> actionConversions
    {
        std::pair<std::string_view, basicWatch::action>{ "RESET", basicWatch::action::RESET},
        std::pair<std::string_view, basicWatch::action>{ "REFRESH", basicWatch::action::REFRESH },
    };

    //What to do when the file changes
    action onUpdate = action::RESET;
    //Last detected file change time
    std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();

    //Which monitors to update
    std::vector<int> relatedMonitors;
};

//Parameters used for a file watch
struct fileWatch : basicWatch
{
    //Path to the watched file
    std::string filePath;
};

//Parameters used for a refresh timer
struct refreshTimer : basicWatch
{
    //How many seconds to wait per update
    unsigned int delaySeconds = 2;
};

//Converts a string to uppercase
std::string toUpper(std::string str) 
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::toupper(c); });
    return str;
}

//Removes leading/trailing whitespace
std::string trim(const std::string& src)
{
    size_t start = std::find_if_not(src.begin(), src.end(), [](char v) { return std::isspace(static_cast<unsigned char>(v)); }) - src.begin();
    size_t end = src.size() - (std::find_if_not(src.rbegin(), src.rend(), [](char v) { return std::isspace(static_cast<unsigned char>(v)); }) - src.rbegin());
    if (start >= end)
    {
        //Input contains only whitespace
        return ""; 
    }
    return src.substr(start, end - start);
}


struct urlWithWatch
{
    std::string url;
    std::optional<refreshTimer> watch;
};

//Reads in the urls file
std::vector<urlWithWatch> readUrls()
{
    std::ifstream file{ settings.urlsFile };
    if (!file.is_open())
        throw std::exception("Unable to read urls file.");

    std::vector<urlWithWatch> result;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line.front() == '#')
            continue;
        std::stringstream stream{ line };
        urlWithWatch current;
        stream >> current.url;

        std::string arg;
        stream >> std::ws >> arg;
        if (!stream)
        {
            result.emplace_back(std::move(current));
            continue;
        }
        arg = toUpper(arg);

        auto convIt = std::ranges::find_if(fileWatch::actionConversions, [&](auto kv) {return kv.first == arg; });
        if (convIt == fileWatch::actionConversions.end())
        {
            throw std::exception("Invalid URL action found in URLs file, acceptable options are RESET or REFRESH.");
        }

        unsigned int refreshTime = 0;
        stream >> refreshTime;
        if (!stream)
        {
            throw std::exception("Unable to parse delay from URL action command.");
        }


        refreshTimer timer;
        timer.delaySeconds = refreshTime;
        timer.onUpdate = convIt->second;
        //Note that this constructs to the current time
        timer.lastTime += std::chrono::seconds(refreshTime);
        current.watch = std::move(timer);
        result.emplace_back(std::move(current));
    }
    return result;
}


//Reads in settings file and updates global settings
//Resets watches
void parseSettings(std::vector<fileWatch>& watches, std::vector<refreshTimer>& refreshes)
{
    std::ifstream file{ settingsSource };
    if (!file.is_open())
        throw std::exception("Unable to read settings file.");

    watches.clear();
    refreshes.clear();

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line.front() == '#')
            continue;

        std::stringstream stream{ line };

        std::string settingName;
        stream >> settingName;
        settingName = toUpper(settingName);

        //Read known settings with some basic type checking/formatting
        if (settingName == "EXECUTABLE")
        {
            std::string path;
            stream >> std::quoted(path);
            settings.executableName = trim(path);
        }
        else if (settingName == "PROCESS")
        {
            std::string name;
            stream >> std::quoted(name);
            settings.processName = trim(name);
        }
        else if (settingName == "ARGS")
        {
            settings.startArgs = trim(line.substr(settingName.size()));
        }
        else if (settingName == "INVALIDMONITORS")
        {
            std::string arg;
            stream >> arg;
            arg = toUpper(arg);

            auto convIt = std::ranges::find_if(appSettings::monitorModeConversions, [&](auto kv) {return kv.first == arg; });
            if (convIt == appSettings::monitorModeConversions.end())
            {
                throw std::exception("Invalid INVALIDMONITORS setting found in settings file, acceptable options are FAIL, PASS or NONE.");
            }
            settings.monitorMode = convIt->second;
        }
        else if (settingName == "MONITORS")
        {
            int mons;
            stream >> mons;
            if (!stream)
                throw std::exception("Invalid option for MONITORS, expected a number.");
            settings.monitors = mons;
        }
        else if (settingName == "REFRESHSECONDS")
        {
            int secs;
            stream >> secs;
            if (!stream)
                throw std::exception("Invalid option for REFRESHSECONDS, expected a number in seconds.");
            settings.refreshTime = secs;
        }
        else if (settingName == "URLS")
        {
            std::string path;
            stream >> std::quoted(path);
            settings.urlsFile = trim(path);
        }
        else if (settingName == "CLOSEONSTART")
        {
            bool val;
            stream >> val;
            if (!stream)
                throw std::exception("Invalid option for CLOSEONSTART, expected a bool (TRUE, FALSE, 1 or 0).");
            settings.closeAllOnStart = val;
        }
        else if (settingName == "LOADTIME")
        {
            int secs;
            stream >> secs;
            if (!stream)
                throw std::exception("Invalid option for LOADTIME, expected a number in seconds.");
            settings.loadTime = secs;
        }
        else if (settingName == "WATCH")
        {
            std::string path;
            stream >> std::quoted(path);

            std::string arg;
            stream >> std::ws >> arg;
            arg = toUpper(arg);

            auto convIt = std::ranges::find_if(fileWatch::actionConversions, [&](auto kv) {return kv.first == arg; });
            if (convIt == fileWatch::actionConversions.end())
            {
                throw std::exception("Invalid WATCH action found in settings file, acceptable options are RESET or REFRESH.");
            }

            std::vector<int> mons;
            while (stream)
            {
                int val;
                stream >> val;
                if (stream)
                    mons.push_back(val);
            }

            fileWatch watch;
            watch.relatedMonitors = std::move(mons);
            watch.filePath = std::move(path);
            watch.onUpdate = convIt->second;

            watches.emplace_back(std::move(watch));
        }
        else if (settingName == "EVERY")
        {
            unsigned int refreshTime = 0;
            stream >> refreshTime;
            if (!stream)
            {
                throw std::exception("Unable to parse delay from EVERY command.");
            }

            std::string arg;
            stream >> std::ws >> arg;
            arg = toUpper(arg);

            auto convIt = std::ranges::find_if(fileWatch::actionConversions, [&](auto kv) {return kv.first == arg; });
            if (convIt == fileWatch::actionConversions.end())
            {
                throw std::exception("Invalid WATCH action found in settings file, acceptable options are RESET or REFRESH.");
            }

            std::vector<int> mons;
            while (stream)
            {
                int val;
                stream >> val;
                if (stream)
                    mons.push_back(val);
            }

            refreshTimer timer;
            timer.relatedMonitors = std::move(mons);
            timer.delaySeconds = refreshTime;
            timer.onUpdate = convIt->second;
            //Note that this constructs to the current time
            timer.lastTime += std::chrono::seconds(refreshTime);

            refreshes.emplace_back(std::move(timer));
        }
        else
        {
            throw std::exception(("Unknown setting name found: \"" + settingName + "\".").c_str());
        }
    }
}

//Pretty prints details about the given setting
void printSetting(std::string_view summary, std::string_view defaultValue, std::vector<std::pair<std::string_view, std::string_view>> otherSpecifiers = {})
{
    std::cout << '\t' << summary << '\n';
    std::cout << osm::feat(osm::col, "cyan") << osm::feat(osm::sty, "italics") << 
        "\t\tdefault: " << osm::feat(osm::sty, "bold") << defaultValue << osm::feat(osm::rst, "bd/ft") << '\n' <<
        osm::feat(osm::rst, "all") << osm::feat(osm::col, "orange");
    for (const auto&[name, val] : otherSpecifiers)
    {
        std::cout << "\t\t" << osm::feat(osm::sty, "bold") << name << osm::feat(osm::rst, "bd/ft") << ": " << val << '\n';
    }
    std::cout << osm::feat(osm::rst, "color");
}

//Prints the currently found urls or a warning if none are found
void printUrls(const std::vector<urlWithWatch>& urls)
{
    if (urls.empty())
    {
        std::cout << osm::feat(osm::col, "red") << "No urls found in file, are you sure the path is set correctly? Tried to read from: " << settings.urlsFile << ".\n";
    }
    else
    {
        std::cout << osm::feat(osm::col, "green") << "Read " << urls.size() << " urls from file: \n";
        for (const auto& i : urls)
            std::cout << '\t' << i.url << '\n';
    }
}

bool ansiEnabledPriorToExecution = false;

void enableAnsiSequences()
{
    std::cout << "Enabling ANSI escape sequences for this console session...\n";
    //Get the handle to the console output
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    //Get the current output mode
    DWORD consoleMode;
    GetConsoleMode(hConsole, &consoleMode);

    ansiEnabledPriorToExecution = (consoleMode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
  
    if (!ansiEnabledPriorToExecution)
    {
        //Enable the ENABLE_VIRTUAL_TERMINAL_PROCESSING flag
        consoleMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        //Set the updated mode
        SetConsoleMode(hConsole, consoleMode);
    }
    else
    {
        std::cout << "Ansi sequences already enabled!\n";
    }
}

void disableAnsiSequences()
{
    std::cout << "Disabling ANSI escape sequences for this console session...\n";
    //Get the handle to the console output
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    //Get the current output mode
    DWORD consoleMode;
    GetConsoleMode(hConsole, &consoleMode);
    //Disable the ENABLE_VIRTUAL_TERMINAL_PROCESSING flag
    consoleMode &= ~ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    //Set the updated mode
    SetConsoleMode(hConsole, consoleMode);
}

//Clears any ansi state, existing processes and resets the terminal ansi status
void cleanUp()
{
    std::cout << "Cleaning up.\n";
    closeAllExisting();
    //Reset ansi sequence
    std::cout << osm::feat(osm::rst, "all");
    //Only disable ansi sequences if they were off to begin with
    if (!ansiEnabledPriorToExecution)
        disableAnsiSequences();
}

//Intercepts CTRL+C or close events and cleans up first
BOOL WINAPI closeHandler(DWORD signal) 
{
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT)
    {
        cleanUp();
        std::cout << "Goodbye!\n";
        std::exit(0);
    }
    return TRUE;
}

void resetWindows(std::vector<rect>& monitors)
{
    std::cout << osm::feat(osm::col, "orange") << "Resetting...\n";
    monitors.clear();
    closeAllExisting();
}

//Performs the effect of the watch, returns false if the update failed and the process needs to reset
[[nodiscard]]
bool applyWatch(const basicWatch& watch, std::vector<process>& processes, std::vector<urlWithWatch>& urls, std::vector<rect>& monitors)
{
    if (watch.onUpdate == fileWatch::action::RESET)
    {
        for (auto p : watch.relatedMonitors)
        {
            if (p >= processes.size())
            {
                std::cout << osm::feat(osm::col, "orange") << "\tEvent specified that monitor " << p << " should be reset, but this monitor is not initialised. Skipped.\n";
                continue;
            }

            //Close the process and restart it
            std::cout << osm::feat(osm::col, "lt cyan") << "\tEvent specified that monitor " << p << " should be reset, resetting...\n";
            processes[p].close();
            processes.erase(processes.begin() + p);

            if (addProcess(processes, urls[p].url, settings.loadTime, p))
            {
                processes[p].moveToMonitor(monitors[p]);
                if (urls[p].watch)
                {
                    //Reset the action time
                    urls[p].watch.value().lastTime = std::chrono::system_clock::now() + std::chrono::seconds(urls[p].watch.value().delaySeconds);
                }
            }
            else
            {
                resetWindows(monitors);
                return false;
            }
        }
    }
    else if (watch.onUpdate == fileWatch::action::REFRESH)
    {
        for (auto p : watch.relatedMonitors)
        {
            if (p >= processes.size())
            {
                std::cout << osm::feat(osm::col, "orange") << "\tEvent specified that monitor " << p << " should be refreshed, but this monitor is not initialised. Skipped.\n";
                continue;
            }

            std::cout << osm::feat(osm::col, "lt cyan") << "\tEvent specified that monitor " << p << " should be refreshed, refreshing...\n";
            //Send a refresh (f5) request
            processes[p].sendMessage(VK_F5);
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    if (!SetConsoleCtrlHandler(closeHandler, TRUE))
    {
        std::cout << "Error: Could not set control handler.\n";
    }

    enableAnsiSequences();
    std::cout << osm::feat(osm::rst, "all");


    //If any arguments were provided, print the help message
    //The application is not meant to be passed any arguments
    if (argc != 1)
    {
        std::cout <<
            "Basic kiosk system for displaying multiple web browser windows over multiple monitors.\n"
            "Does not accept any command line arguments, please write a " + std::string(settingsSource) + " file and place it local to the executable.\n"
            "The settings file should be formed of lines containing the details described below. Empty lines and lines starting with a # are ignored.\n"
            "If a setting isn't provided, the default will be used. If a setting is provided multiple times, only the last value will be read with the exception of WATCH settings.\n\n"
            "SETTINGS:\n";

        printSetting("EXECUTABLE [PATH] - The path to the browser executable.", settings.executableName);

        printSetting("PROCESS [NAME] - The name of the process spawned by the executable.", settings.processName);

        printSetting("ARGS [ARG] [ARG]... - Arguments passed when starting the executable.", settings.startArgs);

        printSetting("MONITORS [INT] - How many monitors the application should expect.", std::to_string(settings.monitors));

        auto invalidMonitorDefaultString = std::ranges::find_if(appSettings::monitorModeConversions, [&](const auto& kvp) {return kvp.second == settings.monitorMode; })->first;

        printSetting("INVALIDMONITORS [FAIL|PASS|NONE] - How the application should react if the number of monitors doesn't match.", invalidMonitorDefaultString, std::vector<std::pair<std::string_view, std::string_view>> {
            std::pair<std::string_view, std::string_view>{"FAIL", "Stop the application if the monitors don't match." },
            std::pair<std::string_view, std::string_view>{"PASS", "Show as many urls on the monitors that are present (e.g. if there are three urls and 2 monitors, shows the first two). The MONITORS setting is not considered, and will show as many urls as possible." },
            std::pair<std::string_view, std::string_view>{"NONE", "No urls are shown, but the application will continue to poll the monitors in case they change." }});

        printSetting("REFRESHSECONDS [INT] - How many seconds to wait between file change or monitor change checks.", std::to_string(settings.refreshTime));

        printSetting("URLS [PATH] - Path to the urls file.", settings.urlsFile);

        printSetting("CLOSEONSTART [1|0|TRUE|FALSE] - Whether the application should close all existing instances of the process on start.", std::to_string(settings.closeAllOnStart));

        printSetting("LOADTIME [INT] - Delay in seconds after starting the process for it to become visible, higher numbers may be required on slower systems.", std::to_string(settings.loadTime));

        printSetting("WATCH [PATH] [REFRESH|RESET] [INT] [INT]... - Adds a file watcher to the given path that performs the specified action to the given monitors when it detects a change.", 
            "No default. Multiple WATCH settings may be provided to watch multiple files.", 
            std::vector<std::pair<std::string_view, std::string_view>>{
            std::pair<std::string_view, std::string_view>{"REFRESH", "The given monitor is refreshed as though F5 was pressed." },
            std::pair<std::string_view, std::string_view>{"RESET", "The given monitor is closed and reopened." }});

        printSetting("EVERY [SECONDS] [REFRESH|RESET] [INT] [INT]... - Adds a timed watcher to the given path that performs the specified action to the given monitors every SECONDS seconds.",
            "No default. Multiple EVERY settings may be provided to occur at different times.",
            std::vector<std::pair<std::string_view, std::string_view>>{
            std::pair<std::string_view, std::string_view>{"REFRESH", "The given monitor is refreshed as though F5 was pressed." },
                std::pair<std::string_view, std::string_view>{"RESET", "The given monitor is closed and reopened." }});

        cleanUp();

        return 0;
    }
    try
    {
        std::vector<fileWatch> watches;
        std::vector<refreshTimer> refreshes;
        std::vector<process> processes;

        //Monitors before and after checking
        std::vector<rect> prevMonitors;
        std::vector<rect> monitors;


        //Read settings file
        std::chrono::system_clock::time_point lastSettingsCheck = std::chrono::system_clock::now();
        parseSettings(watches, refreshes);

        std::cout << osm::feat(osm::col, "green") << "Parsed settings file correctly.\n";

        if (settings.closeAllOnStart)
            closeAllExisting();

        //Read urls file
        std::chrono::system_clock::time_point lastUrlCheck = std::chrono::system_clock::now();
        std::vector<urlWithWatch> urls = readUrls();
        printUrls(urls);


        //Skip the watchdog sleep on the first loop
        bool skipDelay = true;

        //Used for emergency resets
        reset:
        while (true)
        {
            if (!skipDelay)
                std::this_thread::sleep_for(std::chrono::seconds(settings.refreshTime));
            skipDelay = false;

            //Get the current state of the monitors
            prevMonitors = std::move(monitors);
            monitors = getMonitors();
            //Check if the number of active monitors changed
            bool displayStateChanged = !std::ranges::equal(monitors, prevMonitors);


            //Check if the settings file has updated
            if (hasFileUpdatedSince(settingsSource, lastSettingsCheck))
            {
                std::cout << osm::feat(osm::col, "lt cyan") << "Settings file has updated, reloading settings and urls...\n";
                lastSettingsCheck = std::chrono::system_clock::now();
                parseSettings(watches, refreshes);
                //Read the urls again just in case the path to the url file changed
                readUrls();
                printUrls(urls);
            }

            //Check if the urls file has updated
            if (hasFileUpdatedSince(settings.urlsFile, lastUrlCheck))
            {
                std::cout << osm::feat(osm::col, "lt cyan") << "Urls file has updated, reloading urls...\n";
                lastUrlCheck = std::chrono::system_clock::now();
                size_t urlCount = urls.size();
                urls = readUrls();

                //Reset the watch monitors to ensure they're still correct
                for (auto& i : urls)
                {
                    if (!i.watch)
                        continue;
                    auto& watch = i.watch.value();
                    auto it = std::find_if(processes.cbegin(), processes.cend(), [&](const process& proc) { return proc.url == i.url; });
                    if (it == processes.cend())
                    {
                        std::cout << osm::feat(osm::col, "orange") << "Unable to bind event for \"" << i.url << "\" as it did not have an associated process.\n";
                        continue;
                    }
                    auto idx = std::distance(processes.cbegin(), it);
                    watch.relatedMonitors = { static_cast<int>(idx) };
                }

                printUrls(urls);
                //Check if we've got a different number of urls now
                displayStateChanged = urlCount != urls.size();
            }

            //If the number of monitors doesn't line up with what we're expecting
            if (monitors.size() != settings.monitors || monitors.size() != urls.size())
            {
                if (settings.monitorMode == appSettings::invalidMonitorMode::FAIL)
                {
                    throw std::exception(
                        "Non-matching monitor configuration detected, stopping.\nEnsure the number of monitors matches the MONITORS setting and you have the correct number of urls, "
                        "or change the INVALIDMONITORS setting from FAIL.\n");
                }
                else if (settings.monitorMode == appSettings::invalidMonitorMode::NONE)
                {
                    //Only show the message if the monitors change
                    if (displayStateChanged)
                    {
                        std::cout << osm::feat(osm::col, "orange") << "Monitors or urls didn't match, nothing will be displayed until the monitors and urls match.\n"
                                  << "Consider changing the INVALIDMONITORS setting to PASS.\n";
                    }
                    continue;
                }
                //Fit as many as possible on screen, only do any work if the monitor/url setup actually changed
                else if (displayStateChanged)
                {
                    std::cout << osm::feat(osm::col, "orange") << "Number of monitors or urls didn't match expected number.\n";
                    std::cout << "Showing " << urls.size() << " urls on " << monitors.size() << " monitors.\n";

                    //Close any processes that were on monitors we no longer have
                    for (size_t i = monitors.size(); i < processes.size(); i++)
                        processes[i].close();
                
                    //Clear them from the process list
                    processes.erase(processes.begin() + std::min(processes.size(), monitors.size()), processes.end());
                }
            }

            //Ensure all urls are currently correct
            //Only display up to as many monitors or urls as we have
            for (size_t i = 0; i < std::min(monitors.size(), urls.size()); i++)
            {
                //If the process doesn't exist, but we have a monitor and a url for it, start it
                if (i >= processes.size())
                {
                    std::cout << osm::feat(osm::col, "lt cyan") << "Starting \"" << urls[i].url << "\"...\n";
                    if (addProcess(processes, urls[i].url, settings.loadTime, i))
                    {
                        processes[i].moveToMonitor(monitors[i]);
                        if (urls[i].watch)
                        {
                            //Reset the action time
                            urls[i].watch.value().lastTime = std::chrono::system_clock::now() + std::chrono::seconds(urls[i].watch.value().delaySeconds);
                            urls[i].watch.value().relatedMonitors = { static_cast<int>(i) };
                        }
                    }
                    else
                    {
                        resetWindows(monitors);
                        goto reset;
                    }
                }

                //If the process url has changed, restart it with the new url
                if (urls[i].url != processes[i].url)
                {
                    std::cout << osm::feat(osm::col, "lt cyan") << "\"" << processes[i].url << "\" has been changed to \"" << urls[i].url << "\", restarting page...\n";
                    processes[i].close();
                    processes.erase(processes.begin() + i);
                    
                    if (addProcess(processes, urls[i].url, settings.loadTime, i))
                    {
                        processes[i].moveToMonitor(monitors[i]);
                        if (urls[i].watch)
                        {
                            //Reset the action time
                            urls[i].watch.value().lastTime = std::chrono::system_clock::now() + std::chrono::seconds(urls[i].watch.value().delaySeconds);
                            urls[i].watch.value().relatedMonitors = { static_cast<int>(i) };
                        }
                    }
                    else
                    {
                        resetWindows(monitors);
                        goto reset;
                    }
                }
            }

            //If any windows are closed, reopen them
            for (size_t i = 0; i < std::min(monitors.size(), urls.size()); i++)
            {
                if (!processes[i].valid())
                {
                    std::cout << osm::feat(osm::col, "orange") << "Monitor " << i << " appears to have closed! Reopening...\n";
                    processes[i].close();
                    processes.erase(processes.begin() + i);

                    if (addProcess(processes, urls[i].url, settings.loadTime, i))
                    {
                        processes[i].moveToMonitor(monitors[i]);
                        if (urls[i].watch)
                        {
                            //Reset the action time
                            urls[i].watch.value().lastTime = std::chrono::system_clock::now() + std::chrono::seconds(urls[i].watch.value().delaySeconds);
                            urls[i].watch.value().relatedMonitors = { static_cast<int>(i) };
                        }
                    }
                    else
                    {
                        resetWindows(monitors);
                        goto reset;
                    }
                }
            }

            //If any monitors are wrong, try resetting their position
            for (size_t i = 0; i < std::min(monitors.size(), urls.size()); i++)
            {
                if (!processes[i].getBounds().approximately(monitors[i]))
                {
                    std::cout << osm::feat(osm::col, "orange") << "Monitor " << i << " isn't displaying properly! Attempting to fix...\n";
                    processes[i].moveToMonitor(monitors[i]);
                }
            }

            //Check if any watched files have updated
            for (auto& i : watches)
            {
                if (hasFileUpdatedSince(i.filePath, i.lastTime))
                {
                    std::cout << osm::feat(osm::col, "lt cyan") << "Watched file \"" << i.filePath << "\" has been updated, applying changes...\n";
                    //Reset the watched file time
                    i.lastTime = std::chrono::system_clock::now();
                    if (!applyWatch(i, processes, urls, monitors))
                        goto reset;
                }
            }

            //Check if any refresh timers have expired
            const auto now = std::chrono::system_clock::now();
            for (auto& i : refreshes)
            {
                if (now > i.lastTime)
                {
                    std::cout << osm::feat(osm::col, "lt cyan") << "Refresh timer for [";
                    for (size_t m = 0; m < i.relatedMonitors.size() - 1; m++)
                    {
                        std::cout << m << ", ";
                    }
                    std::cout << i.relatedMonitors.back();
                    std::cout << "] has expired, applying changes...\n";
                    //Reset the watched file time
                    i.lastTime = now + std::chrono::seconds(i.delaySeconds);
                    if (!applyWatch(i, processes, urls, monitors))
                        goto reset;
                }
            }
            for (auto& i : urls)
            {
                if (!i.watch)
                    continue;
                auto& watch = i.watch.value();
                if (now > watch.lastTime)
                {
                    std::cout << osm::feat(osm::col, "lt cyan") << "Url refresh timer for [";
                    for (size_t m = 0; m < watch.relatedMonitors.size() - 1; m++)
                    {
                        std::cout << m << ", ";
                    }
                    std::cout << watch.relatedMonitors.back();
                    std::cout << "] has expired, applying changes...\n";
                    //Reset the watched file time
                    watch.lastTime = now + std::chrono::seconds(watch.delaySeconds);
                    if (!applyWatch(watch, processes, urls, monitors))
                        goto reset;
                }
            }
        }
    }
    catch (std::exception& ex)
    {
        std::cout << osm::feat(osm::col, "red") << ex.what() << "\n";
    }
    catch (...)
    {
        std::cout << osm::feat(osm::col, "red") << "An unhandled exception occurred.\n";
    }
    std::cout << osm::feat(osm::rst, "all") << "Press return to close.\n";
    cleanUp();
    //Waits for a newline
    std::cin.ignore();
}
