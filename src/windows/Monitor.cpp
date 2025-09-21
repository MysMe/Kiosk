#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#undef RGB //Windows leaks this macro and it conflicts with osmanip
#include <vector>
#include <algorithm>
#include "Rect.h"
#include <Psapi.h>
#include "Monitor.h"

//Called once for each monitor, records monitor details to the user data, which must be a std::vector<rect>
BOOL CALLBACK MonitorEnumProc(HMONITOR, HDC, LPRECT lprcMonitor, LPARAM dwData)
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
#endif