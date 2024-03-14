-- How many monitors to display
Monitors = 1
-- Which configuration to use
Configuration = 'Example'

-- All configurations
Configurations = {
    -- Configuration name
    Example = {
        -- This function runs every time the kiosk ticks, it accepts a tick-count as a parameter
        OnTick = function (t)
            -- Functions that can be called globally

            -- Synchronise the tick-count for all windows and the global tick counter
            SynchroniseTicks(5)
            -- Indicate to the kiosk that something has changed and it needs to reload
            StateHasChanged()
            -- Wait for 100ms
            Sleep(100)
        end,
        {
            -- Url of the page to display
            Url = "www.google.com",
            -- List of file watches
            Watches = {
                {
                    -- File to be watched
                    File = "example.file",
                    -- Function to run when the file is updated, it accepts the window as a parameter
                    OnUpdate = function (w)
                        -- Functions that can be called on a window

                        -- Refresh the window
                        w:refresh()
                        -- Right click at 100, 100
                        w:click(100, 100, 2)
                        -- Add one to the window's tick count
                        w.tick = w.tick + 1
                        -- If the window is on the first monitor, press F5
                        if (w.Monitor == 0) then
                            w:Press("F5")
                        end
                    end
                }
            },
            -- Function to run every time the kiosk ticks, it accepts a tick-count and the window as parameters
            -- This tick-count is separate from the global tick-count
            OnTick = function (t, w)
                print("Tick")
            end,
            -- Function to run when the window is opened, it accepts the window as a parameter
            -- This function is not guaranteed to run if the window is already open, and does not wait for the window to load
            OnOpen = function (w)
                print("Open")
            end,
            -- Which monitor to display this window on, left to right, 0-N
            Monitor = 0,
            -- Whether to append a cache-buster to the URL, based on the watched files
            CacheBuster = true,
        },
        {
            -- Don't show this window
            Enabled = false,
            -- Regardless of the above, this window will never display as there's only one monitor
            Url = "www.google.com",
        }
    },
}