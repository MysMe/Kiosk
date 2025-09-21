#ifdef __linux__
#include "Process.h"
#include "Monitor.h"
#include "Settings.h"
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <span>
#include <sol/sol.hpp>
#include "PlatformTypes.h"
#include <X11/extensions/XTest.h>
#include <X11/Xatom.h>

//Returns true if the window is fullscreen (_NET_WM_STATE_FULLSCREEN)
bool isFullscreen(windowHandle handle) 
{
    if (handle == 0) return false;
    auto oldHandler = XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });
    Display* display = XOpenDisplay(nullptr);
    if (!display) return false;
    Atom netWmState = XInternAtom(display, "_NET_WM_STATE", True);
    Atom netWmStateFullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", True);
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    unsigned char* prop = nullptr;
    int status = XGetWindowProperty(
        display,
        handle,
        netWmState,
        0, 1024,
        False,
        XA_ATOM,
        &actualType,
        &actualFormat,
        &nItems,
        &bytesAfter,
        &prop
    );
    bool isFullscreen = false;
    if (status == Success && prop) 
    {
        Atom* atoms = (Atom*)prop;
        for (unsigned long i = 0; i < nItems; ++i) 
        {
            if (atoms[i] == netWmStateFullscreen) 
            {
                isFullscreen = true;
                break;
            }
        }
        XFree(prop);
    }
    XCloseDisplay(display);
    XSetErrorHandler(oldHandler);
    return isFullscreen;
}

//Move the window to the specified area and resize it
void setWindowPos(rect area, windowHandle handle)
{
    if (handle == 0) return;
    auto oldHandler = XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });
    Display* display = XOpenDisplay(nullptr);
    if (!display) return;
    XMoveResizeWindow(display, handle, area.left, area.top, area.width, area.height);
    XFlush(display);
    XCloseDisplay(display);
    XSetErrorHandler(oldHandler);
}

bool process::isInPosition(rect area) const
{
    auto bounds = getBounds();
    return bounds.approximately(area) && isFullscreen(wHandle);
}

//Returns true if the process is a valid window
bool process::valid() const
{
    if (wHandle == 0) return false;
    Display* display = XOpenDisplay(nullptr);
    if (!display) return false;
    //Suppress X11 BadWindow errors
    auto oldHandler = XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });
    XWindowAttributes attr;
    bool ok = XGetWindowAttributes(display, wHandle, &attr);
	XSetErrorHandler(oldHandler);
    XCloseDisplay(display);
    return ok;
}

void process::sendMessage(keycode vkCode, bool shiftPress, bool controlPress, bool altPress) const
{
    //Send key event to window using XTest
    auto oldHandler = XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });
    Display* display = XOpenDisplay(nullptr);
    if (!display || wHandle == 0) 
        return;

    //Set input focus and raise window
    XRaiseWindow(display, wHandle);
    XSetInputFocus(display, wHandle, RevertToParent, CurrentTime);
    KeyCode keycode = XKeysymToKeycode(display, vkCode);
    if (shiftPress) 
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Shift_L), True, 0);
    if (controlPress) 
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Control_L), True, 0);
    if (altPress) 
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_L), True, 0);
    XTestFakeKeyEvent(display, keycode, True, 0);
    XTestFakeKeyEvent(display, keycode, False, 0);
    if (shiftPress) 
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Shift_L), False, 0);
    if (controlPress) 
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Control_L), False, 0);
    if (altPress) 
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_L), False, 0);
    XFlush(display);
    XCloseDisplay(display);
    XSetErrorHandler(oldHandler);
}

void process::sendClick(int x, int y, sol::optional<int> buttonType) const
{
    //Send mouse click event using XTest
    auto oldHandler = XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });
    Display* display = XOpenDisplay(nullptr);
    if (!display || wHandle == 0) 
        return;

    //Set input focus and raise window
    XRaiseWindow(display, wHandle);
    XSetInputFocus(display, wHandle, RevertToParent, CurrentTime);
    int button = buttonType.value_or(1);
    int x11Button = (button == 1) ? Button1 : (button == 2) ? Button3 : Button2;

    XTestFakeMotionEvent(display, -1, x, y, 0);
    XTestFakeButtonEvent(display, x11Button, True, 0);
    XTestFakeButtonEvent(display, x11Button, False, 0);
    XFlush(display);
    XCloseDisplay(display);
    XSetErrorHandler(oldHandler);
}

rect process::getBounds() const 
{
    //Get window geometry using XGetWindowAttributes
    auto oldHandler = XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });
    Display* display = XOpenDisplay(nullptr);
    if (!display || wHandle == 0) 
        return rect{0,0,0,0};

    XWindowAttributes attr;
    if (XGetWindowAttributes(display, wHandle, &attr)) 
    {
        XCloseDisplay(display);
        return rect{attr.x, attr.y, attr.width, attr.height};
    }
    XCloseDisplay(display);
    XSetErrorHandler(oldHandler);
    return rect{0,0,0,0};
}

//Attempts to move the window to the given area and full screen it
void process::moveToMonitor(rect area) const
{
    if (valid())
    {
        //Only try up to 5 times to sort the window, otherwise ignore it and move on
        int fail = 5;
        while (!isInPosition(area) && fail-- > 0)
        {
            //Move to the given monitor
            setWindowPos(area, wHandle);
            //Give the window a moment to relocate
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            //If the window was already full screened, don't undo it
            //Moving it should already have returned it to a window
            if (!isFullscreen(wHandle))
            {
                static const auto f11 = getKeycode("F11");
                //Otherwise make it full screen
                sendMessage(f11);
            }
        }
    }
}

//Sends a close request to the window
void process::close() const
{
    if (valid())
    {
        //Send SIGKILL to the process
        if (pId > 0)
        {
            kill(pId, SIGKILL);
        }
    }
}
#endif