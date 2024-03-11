#pragma once
#include "Process.h"
#include <vector>
#include <iostream>

//Starts a new instance of the process and adds it to the process list at the given location
[[nodiscard]]
bool addProcess(std::vector<process>& processes, const std::string& url, int sleepSecs, size_t where)
{
    //Start the process, give it a moment to load and then capture its ids
    createProcess(appSettings::get().executableName, url);

    std::this_thread::sleep_for(std::chrono::seconds(sleepSecs));
    auto instances = getMostRecentProcessesWithName(appSettings::get().processName);

    //Record the urls
    for (auto& i : instances)
        i.url = url;

    //A single process can have many windows, filter out the ones we're already using as we know we don't want them
    std::erase_if(instances, [&](process l) {return std::ranges::any_of(processes, [&](process r) { return r.windowHandle == l.windowHandle; }); });

    //Ensure we only find one window, if we find more then it's likely some were already running 
    if (instances.size() == 0)
    {
        //If we haven't found any windows, it's likely they haven't loaded yet and we should reset and try again
        std::cout << osm::feat(osm::col, "orange") << "Failed to register process and will reset. Consider increasing LOADTIME.\n";
        return false;
    }

    //If we find too many processes, something's gone wrong and we can't continue (we don't know which process to hook onto)
    if (instances.size() > 1)
    {
        std::cout << osm::feat(osm::col, "orange") << "Failed to register process and will reset. Too many processes were found.\n";
        return false;
    }

    processes.insert(processes.begin() + where, instances[0]);
    return true;
}

//Closes all instances of the process
void closeAllExisting()
{
    std::cout << osm::feat(osm::col, "orange") << "Closing all instances of " << appSettings::get().processName << ".\n";
    auto processes = getMostRecentProcessesWithName(appSettings::get().processName);
    for (auto& i : processes)
        i.close();
}
