#ifdef _WIN32
#include "StartupChecks.h"
#include <windows.h>
#include <iostream>

bool runStartupChecks() {
    // Example: Check if running on Windows
    #ifdef _WIN32
    // Add more Windows-specific checks here
    return true;
    #else
    std::cerr << "Error: Not running on Windows.\n";
    return false;
    #endif
}

#endif