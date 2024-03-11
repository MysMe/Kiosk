#pragma once
#include <string_view>
#include <chrono>
#include <filesystem>
#include <array>
#include "Settings.h"
#include <fstream>


//Checks if the file update time has been reset since the given time
bool hasFileUpdatedSince(const std::string_view& filePath, const std::chrono::system_clock::time_point specificTime)
{
    if (!std::filesystem::exists(filePath))
        return false;
    auto updateTime = std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(filePath));
    return updateTime > specificTime;
}

struct basicWatch
{
    enum class action
    {
        RESET,
        REFRESH
    };

    static constexpr std::array<std::pair<std::string_view, basicWatch::action>, 2> actionConversions
    {
        std::pair<std::string_view, basicWatch::action>{ "RESET", basicWatch::action::RESET},
        std::pair<std::string_view, basicWatch::action>{ "REFRESH", basicWatch::action::REFRESH },
    };

    //What to do when the file changes
    action onUpdate = action::RESET;
    //Last detected file change time
    std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();

    //Which monitors to update
    std::vector<int> relatedMonitors;
};

//Parameters used for a file watch
struct fileWatch : basicWatch
{
    //Path to the watched file
    std::string filePath;
};

//Parameters used for a refresh timer
struct refreshTimer : basicWatch
{
    //How many seconds to wait per update
    unsigned int delaySeconds = 2;
};

//Converts a string to uppercase
std::string toUpper(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::toupper(c); });
    return str;
}

//Removes leading/trailing whitespace
std::string trim(const std::string& src)
{
    size_t start = std::find_if_not(src.begin(), src.end(), [](char v) { return std::isspace(static_cast<unsigned char>(v)); }) - src.begin();
    size_t end = src.size() - (std::find_if_not(src.rbegin(), src.rend(), [](char v) { return std::isspace(static_cast<unsigned char>(v)); }) - src.rbegin());
    if (start >= end)
    {
        //Input contains only whitespace
        return "";
    }
    return src.substr(start, end - start);
}


struct urlWithWatch
{
    std::string url;
    std::optional<refreshTimer> watch;
};

//Reads in the urls file
std::vector<urlWithWatch> readUrls()
{
    std::ifstream file{ appSettings::get().urlsFile };
    if (!file.is_open())
        throw std::exception("Unable to read urls file.");

    std::vector<urlWithWatch> result;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line.front() == '#')
            continue;
        std::stringstream stream{ line };
        urlWithWatch current;
        stream >> current.url;

        std::string arg;
        stream >> std::ws >> arg;
        if (!stream)
        {
            result.emplace_back(std::move(current));
            continue;
        }
        arg = toUpper(arg);

        auto convIt = std::ranges::find_if(fileWatch::actionConversions, [&](auto kv) {return kv.first == arg; });
        if (convIt == fileWatch::actionConversions.end())
        {
            throw std::exception("Invalid URL action found in URLs file, acceptable options are RESET or REFRESH.");
        }

        unsigned int refreshTime = 0;
        stream >> refreshTime;
        if (!stream)
        {
            throw std::exception("Unable to parse delay from URL action command.");
        }


        refreshTimer timer;
        timer.delaySeconds = refreshTime;
        timer.onUpdate = convIt->second;
        //Note that this constructs to the current time
        timer.lastTime += std::chrono::seconds(refreshTime);
        current.watch = std::move(timer);
        result.emplace_back(std::move(current));
    }
    return result;
}
