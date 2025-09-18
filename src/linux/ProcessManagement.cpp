#ifdef __linux__
#include "ProcessManagement.h"
#include <optional>
#include <vector>
#include <string>
#include <span>
#include <stdexcept>
#include <sstream>
#include <unistd.h>
#include <iomanip>
#include <dirent.h>
#include <algorithm>
#include <X11/Xatom.h>
#include "PlatformTypes.h"
#include <fstream>
#include <signal.h>
#include "Settings.h"
#include <chrono>
#include <thread>

void createProcess(const std::string& path, const std::string& args) 
{
    // Launch a process using fork and execvp
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        // Set DISPLAY environment variable to :0 if not already set
        if (!getenv("DISPLAY")) {
            setenv("DISPLAY", ":0", 1);
        }
        // Build argument list
        std::vector<std::string> argList;
        argList.push_back(path);
        // Split args by spaces
        std::istringstream iss(args);
        std::string token;
        while (iss >> std::quoted(token)) 
        {
            argList.push_back(token);
        }

        iss = std::istringstream(appSettings::get().startArgs);
        while (iss >> std::quoted(token)) 
        {
            argList.push_back(token);
        }
        argList.push_back("--new-window");
        // Convert to char* array
        std::vector<char*> argv;
        for (auto& s : argList) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);
        execvp(path.c_str(), argv.data());
        // If execvp fails
        _exit(127);
    } else if (pid < 0) {
        // Fork failed
        throw std::runtime_error("Failed to fork process");
    }
    // Parent process: do nothing, child runs browser
}

std::vector<processId> getActiveProcesses(std::string_view nameFilter = "") {
    std::vector<processId> result;
    DIR* proc = opendir("/proc");
    if (!proc) return result;
    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        // Only directories with all digits are PIDs
        if (entry->d_type == DT_DIR) {
            std::string pidStr(entry->d_name);
            if (!pidStr.empty() && std::all_of(pidStr.begin(), pidStr.end(), ::isdigit)) {
                processId pid = static_cast<processId>(std::stoi(pidStr));
                std::string commPath = "/proc/" + pidStr + "/comm";
                std::ifstream commFile(commPath);
                std::string procName;
                if (commFile && std::getline(commFile, procName)) {
                    if (!procName.empty() && procName.back() == '\n') procName.pop_back();
                } else {
                    procName = "";
                }
                if (nameFilter.empty() || procName == nameFilter) {
                    result.push_back(pid);
                }
            }
        }
    }
    closedir(proc);
    return result;
}
void findWindowsByPID(Display* display, Window window, Atom atomPID, processId pId, std::vector<windowHandle>& result) {
    // Check this window
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    unsigned char* propPID = nullptr;
    int status = XGetWindowProperty(
        display,
        window,
        atomPID,
        0, 1,
        False,
        XA_CARDINAL,
        &actualType,
        &actualFormat,
        &nItems,
        &bytesAfter,
        &propPID
    );
    bool windowAdded = false;
    if (status == Success && propPID && nItems == 1) {
        processId winPID = *(processId*)propPID;
        if (winPID == pId) {
            // Only add if this window's parent is the root window (top-level)
            Window root, parent;
            Window* children = nullptr;
            unsigned int nchildren = 0;
            if (XQueryTree(display, window, &root, &parent, &children, &nchildren)) {
                if (parent == root) {
                    // Check if window is mapped and viewable
                    XWindowAttributes attr;
                    if (XGetWindowAttributes(display, window, &attr) && attr.map_state == IsViewable) {
                        result.push_back(window);
                        windowAdded = true;
                    }
                }
                if (children) XFree(children);
            }
        }
    }
    if (propPID) XFree(propPID);

    // Only recurse if we haven't already added a window for this PID
    if (!windowAdded) {
        Window root, parent;
        Window* children;
        unsigned int nchildren;
        if (XQueryTree(display, window, &root, &parent, &children, &nchildren)) {
            for (unsigned int i = 0; i < nchildren; ++i) {
                findWindowsByPID(display, children[i], atomPID, pId, result);
            }
            if (children) XFree(children);
        }
    }
}

std::vector<windowHandle> FindVisibleWindowsByProcessId(processId pId) {
    //Get the window handle for a given process id
    std::vector<windowHandle> result;
    Display* display = XOpenDisplay(nullptr);
    if (!display) return result;
    Window root = DefaultRootWindow(display);
    Atom atomPID = XInternAtom(display, "_NET_WM_PID", True);
    if (atomPID != None) {
        findWindowsByPID(display, root, atomPID, pId, result);
    }
    XCloseDisplay(display);
    return result;
}

std::vector<std::pair<processId, windowHandle>> getMostRecentProcessesWithName(const std::string& name) {
    // Find all active processes
    std::vector<std::pair<processId, windowHandle>> result;
    auto pids = getActiveProcesses(name);
    
    //For every id, find the window handles
    for (const auto& pid : pids) 
    {
        auto windows = FindVisibleWindowsByProcessId(pid);
        for (const auto& win : windows) 
        {
            result.emplace_back(pid, win);
        }
    }

    return result;
}

void closeAllExisting() {
    // Find all browser processes by name and send SIGTERM
    auto processes = getMostRecentProcessesWithName(appSettings::get().processName);
    for (const auto& [pid, win] : processes) {
        kill(pid, SIGTERM); // or SIGKILL for force
    }
}

std::optional<std::pair<processId, windowHandle>> startProcess(const std::string& url, std::span<const windowHandle> existing, windowHandle self) 
{
    // Launch process
    createProcess(appSettings::get().executableName, url);
    // Wait for process to start and window to appear
    std::this_thread::sleep_for(std::chrono::seconds(appSettings::get().loadTime));
    auto instances = getMostRecentProcessesWithName(appSettings::get().processName);
    // Filter out windows already in 'existing'
    std::erase_if(instances, [&](const auto& l) {
        return l.second != self && std::find(existing.begin(), existing.end(), l.second) != existing.end();
    });
    if (instances.empty()) {
        closeAllExisting();
        return std::nullopt;
    }
    if (instances.size() > 1) {
        closeAllExisting();
        return std::nullopt;
    }
    return instances.front();
}
#endif