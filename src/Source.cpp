#include <osmanip/manipulators/colsty.hpp>
#include "ProcessManager.h"
#include <thread>
#include <chrono>

bool ansiEnabledPriorToExecution = false;

void enableAnsiSequences()
{
	#ifdef _WIN32
	std::cout << "Enabling ANSI escape sequences for this console session...\n";
	//Get the handle to the console output
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	//Get the current output mode
	DWORD consoleMode;
	GetConsoleMode(hConsole, &consoleMode);

	ansiEnabledPriorToExecution = (consoleMode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;

	if (!ansiEnabledPriorToExecution)
	{
		//Enable the ENABLE_VIRTUAL_TERMINAL_PROCESSING flag
		consoleMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		//Set the updated mode
		SetConsoleMode(hConsole, consoleMode);
	}
	else
	{
		std::cout << "Ansi sequences already enabled!\n";
	}
	#endif
}

void disableAnsiSequences()
{
	#ifdef _WIN32
	std::cout << "Disabling ANSI escape sequences for this console session...\n";
	//Get the handle to the console output
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	//Get the current output mode
	DWORD consoleMode;
	GetConsoleMode(hConsole, &consoleMode);
	//Disable the ENABLE_VIRTUAL_TERMINAL_PROCESSING flag
	consoleMode &= ~ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	//Set the updated mode
	SetConsoleMode(hConsole, consoleMode);
	#endif
}

//Clears any ansi state, existing processes and resets the terminal ansi status
void cleanUp()
{
	std::cout << "Cleaning up.\n";
	closeAllExisting();
	//Reset ansi sequence
	std::cout << osm::feat(osm::rst, "all");
	//Only disable ansi sequences if they were off to begin with
	if (!ansiEnabledPriorToExecution)
		disableAnsiSequences();
}

#ifdef _WIN32
//Intercepts CTRL+C or close events and cleans up first
BOOL WINAPI closeHandler(DWORD signal)
{
	if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT)
	{
		cleanUp();
		std::cout << "Goodbye!\n";
		std::exit(0);
	}
	return TRUE;
}
#endif

//This function is used to handle errors on the lua side, we only want to print the error and don't need to take special action
inline void luaPanic(sol::optional<std::string> msg) 
{
	std::cout << osm::feat(osm::col, "red") << "A lua error occurred.";
	if (msg)
		std::cout << msg.value() << '\n';
	std::cout << osm::feat(osm::rst, "all");
}

int main()
{
	#ifdef _WIN32
	if (!SetConsoleCtrlHandler(closeHandler, TRUE))
	{
		std::cout << "Error: Could not set control handler.\n";
	}
	#endif

	enableAnsiSequences();
	std::cout << osm::feat(osm::rst, "all");

	while (true)
	{
		try
		{
			if (!std::filesystem::exists("Kiosk.lua"))
			{
				throw std::runtime_error("Unable to find Kiosk.lua");
			}
			std::filesystem::file_time_type lastLoadTime = std::filesystem::last_write_time("Kiosk.lua");

			processManager manager;

			sol::state lua(sol::c_call<decltype(&luaPanic), &luaPanic>);
			lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string, sol::lib::table);
			process::initialiseLUAState(lua);

			lua.set_function("SynchroniseTicks", &processManager::synchroniseTicks, std::ref(manager));
			lua.set_function("StateHasChanged", [&]() { manager.needsRefresh = true; });
			lua.set_function("Sleep", [](unsigned int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); });

			auto luaOutput = lua.safe_script_file("Kiosk.lua");

			if (!luaOutput.valid())
			{
				sol::error err = luaOutput;
				std::string what = err.what();
				std::cout << osm::feat(osm::col, "red") << "Loading lua failed: " << what << '\n' << osm::feat(osm::rst, "all");
			}

			appSettings::get().loadFromTable(lua);

			if (appSettings::get().closeAllOnStart)
				closeAllExisting();

			manager.loadFromTable(lua, appSettings::get().configuration);


			while (true)
			{
				std::this_thread::sleep_for(std::chrono::seconds(appSettings::get().refreshTime));

				manager.tick();

				if (manager.needsRefresh)
				{
					//Refresh the state without reloading the file
					appSettings::get().loadFromTable(lua);
					manager.loadFromTable(lua, appSettings::get().configuration);
					manager.needsRefresh = false;
				}

				//Check whether the kiosk.lua file has been modified
				auto lastWriteTime = std::filesystem::last_write_time("Kiosk.lua");
				if (lastWriteTime > lastLoadTime)
				{
					lastLoadTime = lastWriteTime;
					std::cout << osm::feat(osm::col, "orange") << "Reloading...\n" << osm::feat(osm::rst, "all");
					lua.script_file("Kiosk.lua");
					appSettings::get().loadFromTable(lua);
					manager.loadFromTable(lua, appSettings::get().configuration);
				}
			}
		}
		catch (std::exception& ex)
		{
			std::cout << osm::feat(osm::col, "red") << ex.what() << "\n" << osm::feat(osm::rst, "all");
			std::cout << "Restarting...\n";
			//Add a delay so we don't mash the system if this error is continuous
			std::this_thread::sleep_for(std::chrono::seconds(5));
			continue;
		}
		catch (...)
		{
			std::cout << osm::feat(osm::col, "red") << "An unhandled exception occurred.\n" << osm::feat(osm::rst, "all");
			break;
		}
	}
	std::cout << "Press return to close.\n";
	cleanUp();
	//Waits for a newline
	std::cin.ignore();
}
