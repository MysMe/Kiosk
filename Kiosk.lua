Monitors = 5

--Gang, Regular, BigRat, VdoNinja
Configuration = 'Gang'
ShowRace = true

Configurations = {
    Gang = {
        [0] = {
            Url = "http://localhost:8080/gang1.jpg"
        },
        [1] = {
            Url = "http://localhost:8080/gang2.jpg"
        },
        [2] = {
            Url = "http://localhost:8080/gang3.jpg"
        },
        [3] = {
            Url = "http://localhost:8080/gang4.jpg"
        },
        [4] = {
            Url = "http://localhost:8080/gang5.jpg"
        },
    },
    Regular = {
        [0] = {
            Url = "http://localhost:8080/timetable.html?file=support_timetable",
            CacheBuster = true,
            Watches = {
                [0] = {
                    File = "support/support_timetable.txt",
                    OnUpdate = function (s)
                        s:refresh()
                    end
                }
            }
        },
        [1] = {
            Url = "http://localhost:8080/timetable.html?file=dev_timetable",
            CacheBuster = true,
            Watches = {
                [0] = {
                    File = "support/dev_timetable.txt",
                    OnUpdate = function (s)
                        s:refresh()
                    end
                }
            }
        },
        [2] = {
            Monitor = 4,
            Enabled = ShowRace,
            Url = "http://localhost:8080/sport/race.html",
            CacheBuster = true,
            Watches = {
                [0] = {
                    File = "support/sport/race.json",
                    OnUpdate = function (s)
                        s:refresh()
                    end
                },
                [1] = {
                    File = "support/sport/race.html",
                    OnUpdate = function (s)
                        s:refresh()
                    end
                },
            }
        },
        [3] = {
            Url = "http://opsyte.fas.local/?Screen=2&Live=&FromHours=6&TimeGrain=PT15M",
        },
        [4] = {
            Url = "http://opsyte.fas.local/?Screen=1&Live=&FromDate=&TimeGrain=",
        },
        [5] = {
            Url = "http://opsyte.fas.local/?Screen=1&Live=true&FromDate=&TimeGrain",
        },
    }, 
    BigRat = {
        [0] = {
            Url = "http://localhost:8080/rat1.jpg"
        },
        [1] = {
            Url = "http://localhost:8080/rat2.jpg"
        },
        [2] = {
            Url = "http://localhost:8080/rat3.jpg"
        },
        [3] = {
            Url = "http://localhost:8080/rat4.jpg"
        },
        [4] = {
            Url = "http://localhost:8080/rat5.jpg"
        },
    },
    VdoNinja = {
        [0] = {
            Url = "https://vdo.ninja/?scene=1&room=srd6r6"
        },
        [1] = {
            Url = "https://vdo.ninja/?scene=2&room=srd6r6"
        },
        [2] = {
            Url = "https://vdo.ninja/?scene=3&room=srd6r6"
        },
        [3] = {
            Url = "https://vdo.ninja/?scene=4&room=srd6r6"
        },
        [4] = {
            Url = "https://vdo.ninja/?scene=5&room=srd6r6"
        },
    },
}