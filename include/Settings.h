#pragma once
#include <string>
#include <array>

//Path to settings file
static constexpr auto settingsSource = "Settings.txt";

struct settingDescription
{
	std::string name;
	std::string description;
    std::string defaultValue;
};

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

    static constexpr auto getDefaults()
    {
        return std::array
        {
            settingDescription{ "executableName", "The executable location to be run", "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" },
            settingDescription{ "processName", "The process name of the executable", "msedge.exe" },
            settingDescription{ "startArgs", "Args passed to process on start", "--new-window" },
            settingDescription{ "monitorMode", "How to act when we don't have the correct number of monitors", "PASS" },
            settingDescription{ "monitors", "Expected number of monitors", "1" },
            settingDescription{ "refreshTime", "How many seconds to wait before checking files again", "2" },
            settingDescription{ "urlsFile", "Source urls file", "URLs.txt" },
            settingDescription{ "closeAllOnStart", "Whether to close all instances of the process on start up", "true" },
            settingDescription{ "loadTime", "How long to wait after starting a process before trying to pull its PID/HWND", "1" }
        };
    }

    static appSettings& get()
	{
		static appSettings settings;
		return settings;
	}
};
