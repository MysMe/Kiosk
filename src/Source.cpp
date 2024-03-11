//Make sure to include this BEFORE including the windows header, as they have conflicting names
#include <osmanip/manipulators/colsty.hpp>

#include "Rect.h"
#include "Settings.h"
#include "Monitor.h"
#include "Process.h"
#include "ProcessManagement.h"
#include "FileWatch.h"

#include <iostream>
#include <exception>
#include <utility>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <filesystem>



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
            appSettings::get().executableName = trim(path);
        }
        else if (settingName == "PROCESS")
        {
            std::string name;
            stream >> std::quoted(name);
            appSettings::get().processName = trim(name);
        }
        else if (settingName == "ARGS")
        {
            appSettings::get().startArgs = trim(line.substr(settingName.size()));
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
            appSettings::get().monitorMode = convIt->second;
        }
        else if (settingName == "MONITORS")
        {
            int mons;
            stream >> mons;
            if (!stream)
                throw std::exception("Invalid option for MONITORS, expected a number.");
            appSettings::get().monitors = mons;
        }
        else if (settingName == "REFRESHSECONDS")
        {
            int secs;
            stream >> secs;
            if (!stream)
                throw std::exception("Invalid option for REFRESHSECONDS, expected a number in seconds.");
            appSettings::get().refreshTime = secs;
        }
        else if (settingName == "URLS")
        {
            std::string path;
            stream >> std::quoted(path);
            appSettings::get().urlsFile = trim(path);
        }
        else if (settingName == "CLOSEONSTART")
        {
            bool val;
            stream >> val;
            if (!stream)
                throw std::exception("Invalid option for CLOSEONSTART, expected a bool (TRUE, FALSE, 1 or 0).");
            appSettings::get().closeAllOnStart = val;
        }
        else if (settingName == "LOADTIME")
        {
            int secs;
            stream >> secs;
            if (!stream)
                throw std::exception("Invalid option for LOADTIME, expected a number in seconds.");
            appSettings::get().loadTime = secs;
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
        std::cout << osm::feat(osm::col, "red") << "No urls found in file, are you sure the path is set correctly? Tried to read from: " << appSettings::get().urlsFile << ".\n";
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

            if (addProcess(processes, urls[p].url, appSettings::get().loadTime, p))
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

int main(int argc, char**)
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
            "If a setting isn't provided, the default will be used. If a setting is provided multiple times, only the last value will be read with the exception of WATCH appSettings::get().\n"
            "The URLs file can postfix any URL with [REFRESH|RESET] [INT], indicating an action that should be taken every [INT] seconds. URLs prefixed with a # are ignored.\n\n" << 
            "SETTINGS:\n";

        printSetting("EXECUTABLE [PATH] - The path to the browser executable.", appSettings::get().executableName);

        printSetting("PROCESS [NAME] - The name of the process spawned by the executable.", appSettings::get().processName);

        printSetting("ARGS [ARG] [ARG]... - Arguments passed when starting the executable.", appSettings::get().startArgs);

        printSetting("MONITORS [INT] - How many monitors the application should expect.", std::to_string(appSettings::get().monitors));

        auto invalidMonitorDefaultString = std::ranges::find_if(appSettings::monitorModeConversions, [&](const auto& kvp) {return kvp.second == appSettings::get().monitorMode; })->first;

        printSetting("INVALIDMONITORS [FAIL|PASS|NONE] - How the application should react if the number of monitors doesn't match.", invalidMonitorDefaultString, std::vector<std::pair<std::string_view, std::string_view>> {
            std::pair<std::string_view, std::string_view>{"FAIL", "Stop the application if the monitors don't match." },
            std::pair<std::string_view, std::string_view>{"PASS", "Show as many urls on the monitors that are present (e.g. if there are three urls and 2 monitors, shows the first two). The MONITORS setting is not considered, and will show as many urls as possible." },
            std::pair<std::string_view, std::string_view>{"NONE", "No urls are shown, but the application will continue to poll the monitors in case they change." }});

        printSetting("REFRESHSECONDS [INT] - How many seconds to wait between file change or monitor change checks.", std::to_string(appSettings::get().refreshTime));

        printSetting("URLS [PATH] - Path to the urls file.", appSettings::get().urlsFile);

        printSetting("CLOSEONSTART [1|0|TRUE|FALSE] - Whether the application should close all existing instances of the process on start.", std::to_string(appSettings::get().closeAllOnStart));

        printSetting("LOADTIME [INT] - Delay in seconds after starting the process for it to become visible, higher numbers may be required on slower systems.", std::to_string(appSettings::get().loadTime));

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

    //Used in case of critical errors
    restart:
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

        if (appSettings::get().closeAllOnStart)
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
                std::this_thread::sleep_for(std::chrono::seconds(appSettings::get().refreshTime));
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
            if (hasFileUpdatedSince(appSettings::get().urlsFile, lastUrlCheck))
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
            if (monitors.size() != appSettings::get().monitors || monitors.size() != urls.size())
            {
                if (appSettings::get().monitorMode == appSettings::invalidMonitorMode::FAIL)
                {
                    throw std::exception(
                        "Non-matching monitor configuration detected, stopping.\nEnsure the number of monitors matches the MONITORS setting and you have the correct number of urls, "
                        "or change the INVALIDMONITORS setting from FAIL.\n");
                }
                else if (appSettings::get().monitorMode == appSettings::invalidMonitorMode::NONE)
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
                    if (addProcess(processes, urls[i].url, appSettings::get().loadTime, i))
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
                    
                    if (addProcess(processes, urls[i].url, appSettings::get().loadTime, i))
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

                    if (addProcess(processes, urls[i].url, appSettings::get().loadTime, i))
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
                if (now > i.lastTime && !i.relatedMonitors.empty())
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
                if (now > watch.lastTime && !watch.relatedMonitors.empty())
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
        std::cout << "Restarting...\n";
        //Add a delay so we don't mash the system if this error is continuous
        std::this_thread::sleep_for(std::chrono::seconds(5));
        goto restart;
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
