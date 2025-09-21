#ifdef __linux__
//Linux monitor enumeration using X11/Xinerama
#include <X11/Xlib.h>
#include <X11/extensions/Xinerama.h>
#include <vector>
#include <algorithm>
#include "Rect.h"
#include "Monitor.h"
#include <stdexcept>

std::vector<rect> getMonitors()
{
    std::vector<rect> result;
    Display* display = XOpenDisplay(nullptr);
    if (!display)
        throw std::runtime_error("Failed to open X display");

    int event_base, error_base;
    if (!XineramaQueryExtension(display, &event_base, &error_base) || !XineramaIsActive(display))
    {
        XCloseDisplay(display);
        throw std::runtime_error("Xinerama extension not available or not active");
    }

    int num_monitors = 0;
    XineramaScreenInfo* screens = XineramaQueryScreens(display, &num_monitors);
    if (!screens)
    {
        XCloseDisplay(display);
        throw std::runtime_error("Failed to query Xinerama screens");
    }

    for (int i = 0; i < num_monitors; ++i)
    {
        result.push_back({
            screens[i].x_org,
            screens[i].y_org,
            screens[i].width,
            screens[i].height
        });
    }
    XFree(screens);
    XCloseDisplay(display);

    std::sort(result.begin(), result.end(), [](const rect& a, const rect& b)
    {
        return a.left == b.left ? a.top < b.top : a.left < b.left;
    });
    return result;
}
#endif