#ifdef __linux__
#include "StartupChecks.h"
#include <iostream>
#include <osmanip/manipulators/colsty.hpp>
#include "Settings.h"
#include <ranges>

bool isX11()
{
    // Check XDG_SESSION_TYPE for X11 session
    const char* sessionType = getenv("XDG_SESSION_TYPE");
    if (!sessionType || std::string(sessionType) != "x11") {
        std::cerr << osm::feat(osm::col, "red") << "Error: Not running in an X11 session (found: " << (sessionType ? sessionType : "unset") << ").\n" << osm::feat(osm::rst, "all");
        return false;
    }
    return true;
}

bool isChromium()
{
    // Check if the executable is a Chromium-based browser
    const auto& exe = appSettings::get().executableName;

    static constexpr auto chromiumBrowsers = {
        "chrome",
        "chromium",
        "edge",
        "brave"
    };

    bool failed = false;
    
    if (std::find(chromiumBrowsers.begin(), chromiumBrowsers.end(), exe) != chromiumBrowsers.end()) 
    {
        std::cout << osm::feat(osm::col, "red") << "Error: The specified executable does not appear to be a Chromium-based browser. If in doubt, use 'chromium'.\n" << osm::feat(osm::rst, "all");
        failed = true;
    }

    const auto& process = appSettings::get().processName;
    if (process != "chromium")
    {
        std::cout << osm::feat(osm::col, "red") << "Error: The specified process name must be 'chromium' on Linux.\n" << osm::feat(osm::rst, "all");
        failed = true;
    }

    return !failed;
}

bool runStartupChecks() 
{
    bool valid = true;
    if (!isX11()) valid = false;
    if (!isChromium()) valid = false;
    return valid;
}

#endif