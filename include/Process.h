#pragma once
#include "PlatformTypes.h"
#include <span>
#include "FileWatch.h"
#include "ProcessManagement.h"
#include "Keymap.h"
#include "Rect.h"
#include "Settings.h"
#include "Monitor.h"

class process
{
    std::vector<luaWatch> watches;
    size_t tickCount = 0;
    sol::protected_function onTick;
    sol::protected_function onOpen;
    processId pId = 0;
    windowHandle wHandle = 0;
    std::string url;
    bool cacheBuster = false;
    int nudges = 0;

    bool valid() const;

    auto getCacheBuster() const
    {
        if (watches.empty())
            return std::filesystem::file_time_type::min().time_since_epoch().count();
        return std::max_element(watches.begin(), watches.end(), [](const luaWatch& a, const luaWatch& b) { return a.getLastFileWrite() < b.getLastFileWrite(); })->getLastFileWrite().time_since_epoch().count();
    }

    //Attempts to start the process and assign window handle
    void start(std::span<const windowHandle> existing, const windowHandle self) 
    {
        if (valid())
            close();

        //Add the cache buster if required
        auto toOpen = url;
        if (cacheBuster)
        {
            toOpen += (url.find('?') == std::string::npos ? "?" : "&") + std::to_string(getCacheBuster());
        }

        auto process = startProcess(toOpen, existing, self);
        if (process)
		{
			pId = process->first;
			wHandle = process->second;
            if (onOpen.valid())
			{
				auto result = onOpen(std::ref(*this));
                if (!result.valid())
                {
                    sol::error error = result;
                    std::cout << osm::feat(osm::col, "orange") << "Failed to run OnOpen function: " << error.what() << ".\n" << osm::feat(osm::rst, "all");
                }
			}
		}
    }

    bool isInPosition(rect area) const;

    //Checks if the window is on the correct monitor
    bool checkMonitor() const 
    {
        if (monitor == -1) return true;
        auto monitors = getMonitors();
        if (monitor >= static_cast<int>(monitors.size())) return true;
        if (!isInPosition(monitors[monitor])) 
        {
            moveToMonitor(monitors[monitor]);
            return false;
        }
        return true;
    }

    void moveToMonitor(rect monitorRect) const;

    void close() const;

    void luaPress(sol::variadic_args keys, bool shift, bool control, bool alt)
    {
        for (auto v : keys)
        {
            std::string k = v.get<std::string>();
            std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return std::toupper(c); });
            if (auto code = getKeycode(k))
            {
                sendMessage(code, shift, control, alt);
            }
        }
    }

public:
    int monitor = -1;
    process(processId pid = 0, windowHandle handle = 0) : pId(pid), wHandle(handle) {}
    process(const process&) = delete;
    process(process&& other) noexcept
    {
        watches = std::move(other.watches);
		tickCount = other.tickCount;
		onTick = std::move(other.onTick);
		onOpen = std::move(other.onOpen);
		pId = std::exchange(other.pId, {});
		wHandle = std::exchange(other.wHandle, {});
		url = std::move(other.url);
        monitor = other.monitor;
        cacheBuster = other.cacheBuster;
    }

    process& operator=(const process&) = delete;
        process& operator=(process&& other) noexcept
    {
        watches = std::move(other.watches);
        tickCount = other.tickCount;
        onTick = std::move(other.onTick);
        onOpen = std::move(other.onOpen);
        pId = std::exchange(other.pId, {});
        wHandle = std::exchange(other.wHandle, {});
        url = std::move(other.url);
        monitor = other.monitor;
        cacheBuster = other.cacheBuster;
        return *this;
    }
    ~process() 
    {
        close();
    }

    void setTick(size_t value) { tickCount = value; }
    std::string_view getUrl() const { return url; }
    windowHandle getHandle() const { return wHandle; }

    void sendMessage(keycode vkCode, bool shiftPress = false, bool controlPress = false, bool altPress = false) const;
    void sendClick(int x, int y, sol::optional<int> buttonType) const;
    rect getBounds() const;

    void tick(std::span<const windowHandle> existing) 
    {
        if (!valid())
        {
            start(existing, wHandle);
        }
        if (!checkMonitor())
        {
            //Reset the nudge count if we had to reset the window
            nudges = appSettings::get().nudges;
        }
        if (onTick.valid())
		{
            auto result = onTick(tickCount++, std::ref(*this));
            if (result.valid())
            {
                //If the function didn't return a bool, assume we continue as normal
                if (result.get_type() == sol::type::boolean)
                {
                    bool val = result;
                    if (val)
                        tickCount = 0;
                }
            }
            else
            {
                sol::error error = result;
                std::cout << osm::feat(osm::col, "orange") << "Failed to run tick function: " << error.what() << ".\n" << osm::feat(osm::rst, "all");
            }
		}
        for (auto& watch : watches)
        {
			watch.check(*this);
		}

        if (nudges > 0)
        {
            nudges--;
            static const auto shift = getKeycode("SHIFT");
            //We send a "nudge" to the window to convince it to stop showing the F11 popup
            for (int i = 0; i < 10; i++)
                sendMessage(shift);
        }
    }

    void updateFromTable(sol::table table) 
    {
        onTick = table.get_or("OnTick", sol::protected_function{});
        onOpen = table.get_or("OnOpen", sol::protected_function{});
        monitor = table.get_or("Monitor", -1);
        cacheBuster = table.get_or("CacheBuster", false);
        watches.clear();
        if (auto toWatch = table["Watches"].get_or<sol::table>({}); toWatch.valid())
        {
            for (auto& w : toWatch)
            {
                watches.push_back(luaWatch::loadFromTable(w.second));
            }
        }
    }
    static process loadFromTable(sol::table table) 
    {
		process p({}, {});
        p.url = table.get<std::string>("Url");
        p.updateFromTable(table);
		return p;
    }

    static void initialiseLUAState(sol::state& lua) 
        {
		lua.new_usertype<process>("process",
			"Press", [](process& p, sol::variadic_args keys) 
            {
                p.luaPress(keys, false, false, false);
            },
            "ShiftPress", [](process& p, sol::variadic_args keys) 
            {
				p.luaPress(keys, true, false, false);
			},
            "ControlPress", [](process& p, sol::variadic_args keys) 
            {
                p.luaPress(keys, false, true, false);
                },
            "AltPress", [](process& p, sol::variadic_args keys) 
            {
                p.luaPress(keys, false, false, true);
			},
            "MixedPress", [](process& p, bool shift, bool control, bool alt, sol::variadic_args keys) 
            {
				p.luaPress(keys, shift, control, alt);
			},
            "Click", &process::sendClick,
            "Tick", sol::property(&process::tickCount, &process::tickCount),
            "Monitor", sol::readonly(&process::monitor),
            "Refresh", [](process& p) 
            {
                static const auto refresh = getKeycode("F5");
                p.sendMessage(refresh);
			}
		);
    }
};