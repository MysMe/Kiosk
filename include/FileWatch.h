#pragma once
#include <string_view>
#include <filesystem>
#include <sol/sol.hpp>

class process;

//Binds a function to a file change
class luaWatch
{
    std::string filePath;
    sol::protected_function onUpdate;
    std::filesystem::file_time_type lastTime = std::filesystem::file_time_type::min();
public:
    void check(process& proc)
    {
        if (!std::filesystem::exists(filePath))
        {
            return;
        }
        auto newTime = std::filesystem::last_write_time(filePath);
        if (newTime > lastTime)
        {
            //Watches can be used for cachebusting, so even if there's no function set we can assume the watch is here for the cache buster and the time should still be tracked
            lastTime = newTime;
            if (onUpdate.valid())
            {
                auto result = onUpdate(std::ref(proc));
                if (!result.valid())
                {
                    sol::error error = result;
                    std::cout << osm::feat(osm::col, "orange") << "Failed to run watch function: " << error.what() << ".\n" << osm::feat(osm::rst, "all");
                }
            }
		}
    }

    std::filesystem::file_time_type getLastFileWrite() const
    {
		return lastTime;
	}

    static luaWatch loadFromTable(const sol::table& table)
	{
		luaWatch result;
		result.filePath = table.get_or<std::string>("File", "");
        result.onUpdate = table.get_or<sol::protected_function>("OnUpdate", {});
        if (std::filesystem::exists(result.filePath))
            result.lastTime = std::filesystem::last_write_time(result.filePath);
		return result;
	}
};