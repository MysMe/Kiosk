#pragma once
#include "Monitor.h"
#include "process.h"
#include "Settings.h"

class processManager
{
    std::vector<process> processes;
    sol::function onTick;

    const std::vector<HWND>& getExistingHandles() const
    {
        //Cheeky little static variable to avoid reallocating every tick
        static std::vector<HWND> handles;
        handles.clear();
        for (const auto& p : processes)
            handles.push_back(p.getHandle());
        return handles;
    }

public:
    //If set, the system will reload the lua state on the next update
    bool needsRefresh = false;

    //Sets the tick of all processes to the given value
    void synchroniseTicks(sol::optional<size_t> value)
    {
        for (auto& p : processes)
        {
            p.setTick(value.value_or(0));
        }
    }

    void tick()
    {
        auto monitors = getMonitors();
        if (monitors.size() != appSettings::get().monitors)
        {
            switch (appSettings::get().monitorMode)
            {
            case appSettings::invalidMonitorMode::NONE:
            {
                processes.clear();
                return;
            }
            case appSettings::invalidMonitorMode::FAIL:
            {
                throw std::runtime_error("Monitor count mismatch.");
            }
            default:
                break;
            }
        }

        for (auto& p : processes)
        {
            //The handles can change during a tick, so we need to get them every time
            auto handles = getExistingHandles();
            p.tick(handles);
        }

        if (onTick.valid())
            onTick();
    }

    void loadFromTable(sol::state& table, std::string_view config)
    {
        sol::table data = table[config].get_or(sol::table{});
        if (!data.valid())
            return;

        auto oldProcesses = std::move(processes);
        processes.clear();

        onTick = data["OnTick"];

        for (auto& [k, v] : data)
        {
            //If this isn't a table, skip it
            if (v.get_type() != sol::type::table)
                continue;
            //If this isn't enabled, skip it
            if (v.as<sol::table>()["Enabled"].get_or(true))
            {
                bool forceLoad = false;
                //If this has been set to always reload, set it to do so
                if (v.as<sol::table>()["ForceLoad"].valid())
					forceLoad = v.as<sol::table>()["ForceLoad"];

                //Otherwise, if a process already has this url, swap it back in and do not reload but still update it
                if (!forceLoad && v.as<sol::table>()["Url"].valid())
                {
                    auto url = v.as<sol::table>()["Url"].get<std::string>();
                    forceLoad = true;
                    for (auto it = oldProcesses.begin(); it != oldProcesses.end(); ++it)
					{
						if (it->getUrl() == url)
						{
                            processes.emplace_back(std::move(*it));
                            processes.back().updateFromTable(v);
                            oldProcesses.erase(it);
                            forceLoad = false;
							break;
						}
					}
                }
                if (forceLoad)
                    processes.emplace_back(process::loadFromTable(v));
            }
        }

        //We don't want these to intefere with the new processes, so close them now
        oldProcesses.clear();

        //For every process with an unspecified monitor, give it the first unused monitor
        for (auto& p : processes)
        {
            if (p.monitor == -1)
            {
                //Find the lowest unused monitor index
                bool found = false;
                int index = 0;
                while (found)
                {
                    for (auto& u : processes)
                    {
                        if (u.monitor == index)
                        {
                            found = true;
                            index++;
                            break;
                        }
                    }
                }
                p.monitor = index;
            }
        }

        tick();
    }
};
