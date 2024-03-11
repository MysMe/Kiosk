Monitors = 3
Configuration = 'Example'

Example = {
    OnTick = function ()
        print('generic!')
    end,
    [0] = {
        Url = "www.google.com",
        Monitor = 2,
        OnTick = function (t, s)
            print("Tick!" .. t)
            Configuration = 'Test'
            if (t == 3) then
                print("Change!")
                stateHasChanged()
            end
            return t == 5
        end,
        Watches = {
            [0] = {
                File = "test.txt",
                OnUpdate = function ()
                    print("Update!")
                end
            }
        }
    },
    [1] = 
    {
        Enabled = false,
    }
}
Test = {
    [0] = {
        Url = "www.google.com",
        Monitor = 0,
        OnTick = function (t)
            print("Tick!" .. t)
            return t == 5
        end,
        Watches = {
            [0] = {
                File = "test.txt",
                OnUpdate = function ()
                    print("Update!")
                end
            }
        }
    },
}