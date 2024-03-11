#pragma once
#include "Rect.h"
#include "Settings.h"
#include <thread>
#include <chrono>
#include <Psapi.h>
#include "FileWatch.h"
#include "Keymap.h"
#include "ProcessManagement.h"

//Represents a running instance of the browser
class process
{
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
    }


    std::vector<luaWatch> watches;
    size_t tickCount = 0;
    sol::function onTick;
    sol::function onOpen;

    DWORD processID = 0;
    HWND windowHandle = nullptr;
    std::string url;

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

            //When edge full screens, it likes to put a pop-up telling you how to undo it, but it doesn't dissappear until the process gets some key inputs
            //So after fullscreening, blast it with some inputs on the shift key (this shouldn't affect page content)
            for (int i = 0; i < 10; i++)
                sendMessage(VK_SHIFT);
        }
    }

    void checkMonitor() const
	{
        //If we don't have a monitor set, don't bother checking
		if (monitor == -1)
			return;

        auto bounds = getBounds();
        auto monitors = getMonitors();
        //If the monitor is out of range, ignore it
        if (monitor >= monitors.size())
			return;
        if (!bounds.approximately(monitors[monitor]))
        {
            //If the window is not on the correct monitor, move it
			moveToMonitor(monitors[monitor]);
        }
	}

    //Sends a close request to the window
    void close() const
    {
        if (valid())
        {
            PostMessage(windowHandle, WM_CLOSE, 0, 0);
        }
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

    //Returns true if the process is a valid window
    bool valid() const { return windowHandle && IsWindow(windowHandle); }

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

    void start(std::span<HWND> existing)
    {
        if (valid())
            close();
        auto process = startProcess(url, existing);
        if (process)
		{
			processID = process->first;
			windowHandle = process->second;
            if (onOpen.valid())
			{
				onOpen(std::ref(*this));
			}
		}
    }

    auto cacheBuster() const
    {
        if (watches.empty())
            return std::filesystem::file_time_type::min().time_since_epoch().count();
        return std::max_element(watches.begin(), watches.end(), [](const luaWatch& a, const luaWatch& b) { return a.getLastFileWrite() < b.getLastFileWrite(); })->getLastFileWrite().time_since_epoch().count();
    }

public:

    process(DWORD pid, HWND handle) : processID(pid), windowHandle(handle) {}
    process(const process&) = delete;
    process(process&& other) noexcept
    {
        watches = std::move(other.watches);
		tickCount = other.tickCount;
		onTick = std::move(other.onTick);
		onOpen = std::move(other.onOpen);
		processID = std::exchange(other.processID, 0);
		windowHandle = std::exchange(other.windowHandle, nullptr);
		url = std::move(other.url);
        monitor = other.monitor;
    }
    process& operator=(const process&) = delete;
    process& operator=(process&& other) noexcept
    {
        watches = std::move(other.watches);
        tickCount = other.tickCount;
        onTick = std::move(other.onTick);
        onOpen = std::move(other.onOpen);
        processID = std::exchange(other.processID, 0);
        windowHandle = std::exchange(other.windowHandle, nullptr);
        url = std::move(other.url);
        monitor = other.monitor;
        return *this;
    }
    ~process()
    {
        close();
    }

    int monitor = -1;

    void setTick(size_t value)
    {
        tickCount = value;
    }

    std::string_view getUrl() const { return url; }

    HWND getHandle() const { return windowHandle; }

    //Sends a keypress to the window
    void sendMessage(WORD vkCode) const
    {
        if (!valid())
            return;

        //Try and get window focus before sending keycodes
        waitForProcessIdle();
        bringToForeground();

        //Send a down, then an up (otherwise the window will think we're holding the key)
        simulateKey(vkCode, true);
        simulateKey(vkCode, false);
    }

    void sendClick(int x, int y, sol::optional<int> buttonType) const
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
    rect getBounds() const
    {
        RECT windowBounds;
        GetWindowRect(windowHandle, &windowBounds);
        return { windowBounds.left, windowBounds.top, windowBounds.right - windowBounds.left, windowBounds.bottom - windowBounds.top };
    }

    void tick(std::span<HWND> existing)
    {
        if (!valid())
        {
            start(existing);
        }
        checkMonitor();
        if (onTick.valid())
		{
            if (onTick(tickCount++, std::ref(*this)))
            {
                tickCount = 0;
            }
		}
        for (auto& watch : watches)
        {
			watch.check();
		}
    }

    //Updates the non-url fields
    void updateFromTable(sol::table table)
    {
        onTick = table.get_or("OnTick", sol::function{});
        onOpen = table.get_or("OnOpen", sol::function{});
        monitor = table.get_or("Monitor", -1);
        watches.clear();
        if (auto toWatch = table["Watches"].get<sol::table>(); toWatch.valid())
        {
            for (auto& w : toWatch)
            {
                watches.push_back(luaWatch::loadFromTable(w.second));
            }
        }
        if (table.get_or("CacheBuster", false))
        {
            url += (url.find('?') == std::string::npos ? "?" : "&") + std::to_string(cacheBuster());
        }
    }

    static process loadFromTable(sol::table table)
	{
		process p(0, nullptr);
        p.url = table.get<std::string>("Url");
        p.updateFromTable(table);
		return p;
	}

    //Declare the sol usertype functions
    static void initialiseLUAState(sol::state& lua)
	{
		lua.new_usertype<process>("process",
			"press", [](process& p, sol::variadic_args keys) {
                for (auto v : keys)
                {
                    std::string k = v.get<std::string>();
                    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return std::toupper(c); });
                    auto it = keyToCode.find(k);
                    if (it != keyToCode.end())
                    {
                        p.sendMessage(it->second);
                    }
                }
            },
            "click", &process::sendClick,
            "tick", sol::property(&process::tickCount, &process::tickCount),
            "monitor", sol::readonly(&process::monitor),
            "refresh", [](process& p) {
                p.sendMessage(VK_F5);
			}
		);
	}
};
