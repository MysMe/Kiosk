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

    const std::vector<HWND>& getExistingHandles(std::span<const HWND> otherHandles) const
    {
        //Cheeky little static variable to avoid reallocating every tick
        static std::vector<HWND> handles;
        handles.clear();
        for (const auto& p : processes)
            handles.push_back(p.getHandle());

        for (auto h : otherHandles)
        {
			handles.push_back(h);
		}
        return handles;
    }

    //Takes a list of other windows that may be closed soon, but not yet
    void tickImpl(std::span<const HWND> dyingWindows)
    {
        auto monitors = getMonitors();
        if (monitors.size() != appSettings::get().monitors)
        {
            switch (appSettings::get().monitorMode)
            {
            case appSettings::invalidMonitorMode::NONE:
            {
                //Close all windows but keep running
                processes.clear();
                return;
            }
            case appSettings::invalidMonitorMode::FAIL:
            {
                //Let the exception handler do its thing
                throw std::runtime_error("Monitor count mismatch.");
            }
            default:
                //Continue with the windows we have regardless
                break;
            }
        }

        //Use a reference wrapper as we may want to reassign the handles
        auto handles = std::ref(getExistingHandles(dyingWindows));

        for (auto& p : processes)
        {
            auto originalHandle = p.getHandle();
            p.tick(handles.get());
            if (p.getHandle() != originalHandle)
			{
				//If the handle has changed, we need to update the list
				handles = std::ref(getExistingHandles(dyingWindows));
			}
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
        tickImpl({});
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

        //Store the old handles so we don't double capture them
        static std::vector<HWND> dyingHandles;
        dyingHandles.clear();
        for (auto& p : oldProcesses)
		{
			dyingHandles.push_back(p.getHandle());
		}

        tickImpl(dyingHandles);
    }
};
