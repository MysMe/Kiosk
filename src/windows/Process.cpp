#ifdef _WIN32

    // Returns true if the process is a valid window
    bool process::valid() const
    {
        return windowHandle && IsWindow(windowHandle);
    }
    
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

    //Puts the focus on the window
    void bringToForeground() const
    {
        //Restore and give focus
        if (IsIconic(windowHandle))
        {
            ShowWindow(windowHandle, SW_RESTORE);
        }

        SetForegroundWindow(windowHandle);
        SetFocus(windowHandle);
    }

        //Sends a keypress event to the window
    void simulateKey(WORD vkCode, bool press) const
    {
        if (!valid())
            return;

        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vkCode;
        input.ki.dwFlags = press ? 0 : KEYEVENTF_KEYUP;

        SendInput(1, &input, sizeof(INPUT));

        std::this_thread::sleep_for(std::chrono::milliseconds(appSettings::get().keyTimeMs));
    }

    //Attempts to move the window to the given area and full screen it
    void moveToMonitor(rect area) const
    {
        if (valid())
        {
            //Only try up to 5 times to sort the window, otherwise ignore it and move on
            int fail = 5;
            while (!getBounds().approximately(area) && fail-- > 0)
            {
                //Move to the given monitor
                SetWindowPos(windowHandle, NULL, area.left, area.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
                //Give the window a moment to relocate
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                //If the window was already full screened, don't undo it
                //Moving it should already have returned it to a window
                if (!getBounds().approximately(area))
                {
                    //Otherwise make it full screen
                    sendMessage(VK_F11);
                }
            }
        }
    }

    void process::sendMessage(int vkCode, bool shiftPress, bool controlPress, bool altPress) const 
    {
        if (!valid())
            return;

        //Try and get window focus before sending keycodes
        waitForProcessIdle();
        bringToForeground();

        // If Shift, Control, or Alt is pressed, send their down events
        if (shiftPress)
            simulateKey(VK_SHIFT, true);
        if (controlPress)
            simulateKey(VK_CONTROL, true);
        if (altPress)
            simulateKey(VK_MENU, true);

        //Send a down, then an up (otherwise the window will think we're holding the key)
        simulateKey(vkCode, true);
        simulateKey(vkCode, false);

        // If Shift, Control, or Alt was pressed, send their up events
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
        waitForProcessIdle();
        bringToForeground();

        //Send a down, then an up (otherwise the window will think we're holding the key)
        switch (buttonType.value_or(1)) {
        case 1: // Left click
            SendMessage(windowHandle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessage(windowHandle, WM_LBUTTONUP, MK_LBUTTON, MAKELPARAM(x, y));
            break;
        case 2: // Right click
            SendMessage(windowHandle, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(x, y));
            SendMessage(windowHandle, WM_RBUTTONUP, MK_RBUTTON, MAKELPARAM(x, y));
            break;
        case 3: // Middle click
            SendMessage(windowHandle, WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(x, y));
            SendMessage(windowHandle, WM_MBUTTONUP, MK_MBUTTON, MAKELPARAM(x, y));
            break;
        default:
            // Invalid button type
            break;
        }
    }

    //Gets the window area
    rect process::getBounds() const
    {
        RECT windowBounds;
        GetWindowRect(windowHandle, &windowBounds);
        return { windowBounds.left, windowBounds.top, windowBounds.right - windowBounds.left, windowBounds.bottom - windowBounds.top };
    }

    //Sends a close request to the window
    void process::close() const
    {
        if (valid())
        {
            PostMessage(windowHandle, WM_CLOSE, 0, 0);
        }
    }

#endif