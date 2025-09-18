#pragma once
#include "PlatformTypes.h"
#include <vector>
#include <optional>
#include <span>
#include <string>

//Starts the given process with the provided arguments
void createProcess(const std::string& path, const std::string& args);

//Returns the PIDs of all active processes
std::vector<processId> getActiveProcesses(std::string_view processName);

//Finds all the visible windows for a given process
std::vector<windowHandle> FindVisibleWindowsByProcessId(processId processId);

//Finds the most recent process with the given name and pulls all its visible windows
std::vector<std::pair<processId, windowHandle>> getMostRecentProcessesWithName(const std::string& name);

void closeAllExisting();

//Starts a new instance of the process and adds it to the process list at the given location
[[nodiscard]]
std::optional<std::pair<processId, windowHandle>> startProcess(const std::string& url, std::span<const windowHandle> existing, windowHandle self);
