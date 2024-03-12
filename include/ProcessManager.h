#pragma once
#include "Monitor.h"
#include "process.h"
#include "Settings.h"
#include <map>

class processManager
{
    std::vector<process> processes;
    sol::protected_function onTick;
    size_t tickCount = 0;

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
        tickCount = value.value_or(0);
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
        {
            auto result = onTick(tickCount++);
            if (result.valid())
            {
                bool val = result;
                if (val)
                    tickCount = 0;
            }
            else
            {
                sol::error error = result;
                std::cout << osm::feat(osm::col, "orange") << "Failed to run global tick function: " << error.what() << ".\n" << osm::feat(osm::rst, "all");
            }
        }
    }

    void loadFromTable(sol::state& table, std::string_view config)
    {
        sol::table data = table["Configurations"][config].get_or(sol::table{});
        if (!data.valid())
            throw std::runtime_error("Configurations table not found.");

        auto oldProcesses = std::move(processes);
        processes.clear();

        std::vector<std::pair<int, process>> indexedProcesses;

        onTick = data["OnTick"];

        for (auto& [k, v] : data)
        {
            //If this isn't a table, skip it
            if (v.get_type() != sol::type::table)
                continue;
            //If this isn't enabled, skip it
            if (v.as<sol::table>()["Enabled"].get_or(true))
            {
                if (!k.is<int>())
                {
                    std::cout << osm::feat(osm::col, "orange") << "Warning: Process key \"" << v.as<std::string>() << "\" is not an integer. It will not be considered.\n" << osm::feat(osm::rst, "all");
                }
                int key = k.as<int>();

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
                            auto temp = std::move(*it);
                            temp.updateFromTable(v);
                            oldProcesses.erase(it);
                            forceLoad = false;
                            indexedProcesses.emplace_back(key, std::move(temp));
							break;
						}
					}
                }
                if (forceLoad)
                    indexedProcesses.emplace_back(key, process::loadFromTable(v));
            }
        }

        //We don't want these to interfere with the new processes, so close them now
        oldProcesses.clear();

        //Order by insertion
        std::sort(indexedProcesses.begin(), indexedProcesses.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

        //For every process with an unspecified monitor, give it the first unused monitor
        for (auto& [k, p] : indexedProcesses)
        {
            if (p.monitor == -1)
            {
                //Find the lowest unused monitor index
                bool found = true;
                int index = 0;
                while (found)
                {
                    found = false;
                    for (auto& [u, m] : indexedProcesses)
                    {
                        if (m.monitor == index)
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

        //Remove any with a monitor index greater than the monitor count
        indexedProcesses.erase(std::remove_if(indexedProcesses.begin(), indexedProcesses.end(), 
            [](const auto& p) { return p.second.monitor >= appSettings::get().monitors || p.second.monitor < 0; }), indexedProcesses.end());

        //Sort by monitor
        std::sort(indexedProcesses.begin(), indexedProcesses.end(), [](const auto& a, const auto& b) { return a.second.monitor < b.second.monitor; });

        processes.reserve(indexedProcesses.size());

        for (auto& [k, p] : indexedProcesses)
        {
			processes.emplace_back(std::move(p));
		}

        tick();
    }
};
