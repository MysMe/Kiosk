#pragma once
#include <string>
#include <array>
#include <sol/sol.hpp>

struct appSettings
{
    //The executable location to be run
    std::string executableName = "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";
    //The process name of the executable
    std::string processName = "msedge.exe";
    //Args passed to process on start
    std::string startArgs = "";

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

    //Whether to close all instances of the process on start up
    bool closeAllOnStart = true;
    //How long to wait after starting a process before trying to pull its PID/HWND
    int loadTime = 1;

    //Which configuration to use
    std::string configuration = "Default";

    //How long we wait for keypresses
    int keyTimeMs = 50;

    static appSettings& get()
	{
		static appSettings settings;
		return settings;
	}

    void loadFromTable(sol::state& table)
    {
        executableName = table.get_or("ExecutableName", executableName);
		processName = table.get_or("ProcessName", processName);
		startArgs = table.get_or("StartArgs", startArgs);

        std::string monitorModeStr = table.get_or("MonitorMode", std::string());
        if (!monitorModeStr.empty())
		{
            std::transform(monitorModeStr.begin(), monitorModeStr.end(), monitorModeStr.begin(), [](unsigned char c) { return std::toupper(c); });
			auto it = std::find_if(monitorModeConversions.begin(), monitorModeConversions.end(), [&](const auto& pair) { return pair.first == monitorModeStr; });
			if (it != monitorModeConversions.end())
			{
				monitorMode = it->second;
			}
		}

		monitors = table.get_or("Monitors", monitors);
		refreshTime = table.get_or("RefreshTime", refreshTime);
		closeAllOnStart = table.get_or("CloseAllOnStart", closeAllOnStart);
		loadTime = table.get_or("LoadTime", loadTime);
        configuration = table.get_or("Configuration", configuration);
        keyTimeMs = table.get_or("KeyTimeMs", keyTimeMs);
    }
};
