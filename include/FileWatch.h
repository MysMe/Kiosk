#pragma once
#include <string_view>
#include <filesystem>
#include <sol/sol.hpp>

//Binds a function to a file change
class luaWatch
{
    std::string filePath;
    sol::protected_function onUpdate;
    std::filesystem::file_time_type lastTime = std::filesystem::file_time_type::min();
public:
    void check()
    {
        if (!onUpdate.valid() || !std::filesystem::exists(filePath))
        {
            return;
        }
        auto newTime = std::filesystem::last_write_time(filePath);
        if (newTime > lastTime)
		{
			lastTime = newTime;
			auto result = onUpdate();
            if (!result.valid())
            {
                sol::error error = result;
                std::cout << osm::feat(osm::col, "orange") << "Failed to run watch function: " << error.what() << ".\n" << osm::feat(osm::rst, "all");
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
		result.filePath = table.get<std::string>("File");
		result.onUpdate = table.get<sol::protected_function>("OnUpdate");
        if (std::filesystem::exists(result.filePath))
            result.lastTime = std::filesystem::last_write_time(result.filePath);
		return result;
	}
};