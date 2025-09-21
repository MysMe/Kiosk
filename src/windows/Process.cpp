#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#undef RGB //Windows leaks this macro and it conflicts with osmanip
#include "Process.h"
#include <thread>

//Returns true if the process is a valid window
bool process::valid() const
{
    return wHandle && IsWindow(wHandle);
}
    
//Waits for the process to be ready to accept input
bool waitForProcessIdle(processId pId, DWORD timeoutMillis = INFINITE)
{
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pId);
    if (hProcess)
    {
        DWORD waitResult = WaitForInputIdle(hProcess, timeoutMillis);
        CloseHandle(hProcess);
        return (waitResult == 0);
    }
    //Failed to open the process
    return false;
}

//Puts the focus on the window
void bringToForeground(windowHandle wHandle)
{
    //Restore and give focus
    if (IsIconic(wHandle))
    {
        ShowWindow(wHandle, SW_RESTORE);
    }

    SetForegroundWindow(wHandle);
    SetFocus(wHandle);
}

//Sends a keypress event to the window
void simulateKey(WORD vkCode, bool press)
{
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vkCode;
    input.ki.dwFlags = press ? 0 : KEYEVENTF_KEYUP;

    SendInput(1, &input, sizeof(INPUT));

    std::this_thread::sleep_for(std::chrono::milliseconds(appSettings::get().keyTimeMs));
}

bool process::isInPosition(rect area) const
{
    auto bounds = getBounds();
    return bounds.approximately(area);
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
            SetWindowPos(wHandle, NULL, area.left, area.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
            //Give the window a moment to relocate
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            //If the window was already full screened, don't undo it
            //Moving it should already have returned it to a window
            if (!isInPosition(area))
            {
                //Otherwise make it full screen
                sendMessage(VK_F11);
            }
        }
    }
}

void process::sendMessage(keycode vkCode, bool shiftPress, bool controlPress, bool altPress) const 
{
    if (!valid())
        return;

    //Try and get window focus before sending keycodes
    waitForProcessIdle(pId);
    bringToForeground(wHandle);

    //If Shift, Control, or Alt is pressed, send their down events
    if (shiftPress)
        simulateKey(VK_SHIFT, true);
    if (controlPress)
        simulateKey(VK_CONTROL, true);
    if (altPress)
        simulateKey(VK_MENU, true);

    //Send a down, then an up (otherwise the window will think we're holding the key)
    simulateKey(vkCode, true);
    simulateKey(vkCode, false);

    //If Shift, Control, or Alt was pressed, send their up events
    if (shiftPress)
        simulateKey(VK_SHIFT, false);
    if (controlPress)
        simulateKey(VK_CONTROL, false);
    if (altPress)
        simulateKey(VK_MENU, false);
}

void process::sendClick(int x, int y, sol::optional<int> buttonType) const
{
    if (!valid())
        return;

    //Try and get window focus before sending keycodes
    waitForProcessIdle(pId);
    bringToForeground(wHandle);

    //Send a down, then an up (otherwise the window will think we're holding the key)
    switch (buttonType.value_or(1)) 
    {
    case 1: //Left click
        SendMessage(wHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessage(wHandle, WM_LBUTTONUP, MK_LBUTTON, MAKELPARAM(x, y));
        break;
    case 2: //Right click
        SendMessage(wHandle, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(x, y));
        SendMessage(wHandle, WM_RBUTTONUP, MK_RBUTTON, MAKELPARAM(x, y));
        break;
    case 3: //Middle click
        SendMessage(wHandle, WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(x, y));
        SendMessage(wHandle, WM_MBUTTONUP, MK_MBUTTON, MAKELPARAM(x, y));
        break;
    default:
        //Invalid button type
        break;
    }
}

//Gets the window area
rect process::getBounds() const
{
    RECT windowBounds;
    GetWindowRect(wHandle, &windowBounds);
    return { windowBounds.left, windowBounds.top, windowBounds.right - windowBounds.left, windowBounds.bottom - windowBounds.top };
}

//Sends a close request to the window
void process::close() const
{
    if (valid())
    {
        PostMessage(wHandle, WM_CLOSE, 0, 0);
    }
}

#endif