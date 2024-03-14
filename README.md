# Kiosk
Multi-Monitor Browser Kiosk System 

**Windows only.**

Lua-driven application for running multiple browser instances over multiple monitors in full screen. Can be configured using a local Kiosk.lua file.
Checks for file updates at regular intervals, so content can be adjusted without the need to restart the process.  

## Settings
Settings are placed at global scope in the lua file, the following settings are available

- **ExecutableName**: The path to the executable that will be run. By default, this is set to the path of the Microsoft Edge browser.
- **ProcessName**: The name of the process that the executable will run as. This is used to find and manage the process after it's started. By default, this is set to *"msedge.exe"*.
- **StartArgs**: Arguments that will be passed to the process on start. By default, this is empty. Note that *"--new-window"* is always used, regardless of this setting. 
- **Monitors**: The maximum number of monitors and by extension maximum number of windows opened. By default, this is set to *1*. Note that the program can still run even if this doesn't line up with the real number of monitors (see *MonitorMode* below).
- **MonitorMode**: Determines how the program should behave when the correct number of monitors are not available. Options are "FAIL" (stop the program), "PASS" (show as many windows as possible), and "NONE" (don't show any windows). By default, this is set to *"PASS"*.
- **RefreshTime**: The number of seconds to wait between ticking. By default, this is set to *2*.
- **CloseAllOnStart**: Whether to close all instances of the process on start up. By default, this is set to *true*.
- **LoadTime**: The number of seconds to wait after starting a process before trying to interact with it. By default, this is set to *1*.
- **Configuration**: The name of the configuration to use ([see: *Configurations*](#configurations)). By default, this is set to *"Default"*. 

## Configurations
Aside from settings, the system will look for a global table called *"Configurations"*, this table should contain sub-tables representing a valid screen layout. The name of the sub-table is the name that should be used with the *Configuration* setting. For shown windows, the sub-table must use numeric keys and the order of the keys determines which order the windows are considered in. Non-table entries are not considered.

In addition, an *OnTick* function can be set, which is called every tick. This function accepts an unsigned integer representing the number of ticks elapsed. Returning true will reset the tick counter to 0, otherwise it will be incremented by 1 for the next tick.

## Windows
Each sub-table of a [*configuration*](#configurations) describes multiple windows, these windows have multiple properties that can be set:
- **Enabled**: Whether or not this table should be considered. This value can be changed at runtime. Defaults to *true*.
- **Url**: The url of the webpage that should be displayed.
- **ForceLoad**: Whether this window should always be reopened when the window layout changes. Otherwise an existing window will be reused when possible. Defaults to *false*.
- **OnTick(tickCount, window)**: A function that runs every tick. The function accepts an unsigned integer argument that represents the ticks elapsed. If this function returns true, the tick counter resets. The tick counter is unique for each window and the general tick function in the *configuration*. The window parameter can be used to modify this window, but not other windows on the kiosk ([see: *Window Functions And Members*](#window-functions-and-members)).
- **OnOpen(window)**: A function called when the window is opened for the first time. Note that there are no guarantees the window has loaded by the time this function runs. If *ForceLoad* is set, then this function will be run every time the window reopens. The window parameter can be used to modify this window, but not other windows on the kiosk ([see: *Window Functions And Members*](#window-functions-and-members)).
- **Monitor**: Which monitor this window should show on, from left to right. If unset, the first unassigned monitor will be used.
- **Watches**: An array of watch objects ([see: *Watches*](#watches)).
- **CacheBuster**: If set, the url will be appended with a cache busting string. This string is determined by the *watched* files, and will not update if the watches haven't updated.

## Watches
Watches can be used to respond to file changes. When the file change is detected, the watch can trigger a function. Watches have two assignable values:
- **File**: The *relative* (to the executable) path of the file to watch.
- **OnUpdate**: A function that runs when a file change is detected.

## Functions And Members
There are a number of functions and members that can be called/accessed from LUA to interact with the kiosk.

### Global Functions
*These functions can be called anywhere in the lua file. There are no global members.*
- **SynchroniseTicks(unsigned integer [Default 0])**: The function resets the ticks of all windows and the generic tick function to the same value. If no parameter is provided, resets them all to 0.
- **StateHasChanged()**: Used to indicate to the executable that the lua state has changed (e.g. the [*configuration*](#configurations) has been changed). If this isn't called the changes will not be reflected. Once this has been called, the changes will be *queued* until the end of the tick, ensuring all tick functions are run regardless of when *StateHasChanged* is called. This function can be called multiple times without issue, and should ideally be called every time a change is made.
- **Sleep(unsigned integer)**: Sleeps the kiosk for the given number of *milliseconds*. Note that the kiosk is single-threaded and will not run ticks/watches while sleeping. This means the kiosk is effectively suspended for the duration of the sleep.

### Window Functions And Members
*These functions and members can only be used on a window, which must be obtained through a relevant parameter.*
- **Press(strings, ...)**: Simulates a set of keypresses in the browser window one at a time in order. Each key is represented by a string and is case insensitive. Keypresses are always *lower case*. Valid keys are
    -- Numeric keys: "0" to "9".
    -- Alphabetic keys: "A" to "Z".
    -- Function keys: "F1" to "F24".
    -- Numpad keys: "NUMPAD0" to "NUMPAD9", "NUMPADMULTIPLY", "NUMPADADD", "NUMPADSUBTRACT", "NUMPADDECIMAL", and "NUMPADDIVIDE".
    -- Control keys: "BACKSPACE", "TAB", "CLEAR", "ENTER", "SHIFT", "CONTROL", "ALT", "PAUSE", "CAPSLOCK", "ESCAPE", "SPACE", "PAGEUP", "PAGEDOWN", "END", "HOME", "LEFT", "UP", "RIGHT", "DOWN", "SELECT", "PRINT", "EXECUTE", "PRINTSCREEN", "INSERT", "DELETE".
- **ShiftPress(strings, ...), ControlPress(strings, ...), AltPress(strings, ...)**: Simulates a set of keypresses in the browser window, but as though the associated key was also being held.
- **MultiPress(shift, control, alt, strings, ...)**: Simulates a set of keypresses in the browser window. For each flag set to true, simulates that key being held.
- **Click(x, y, type [Default: 1])**: Simulates a click at the given *local screen* (not global desktop) coordinates, where 0,0 is the top left corner. Type can be set to 1 (left click), 2 (right click) or 3 (middle click).
- **Tick**: Read/Write member access to the windows tick counter.
- **Monitor**: Read-only member access to the windows monitor id.
- **Refresh**: Sends a refresh keypress to the window (shortcut for Press("F5")).