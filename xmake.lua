add_rules("mode.debug", "mode.release")

add_requires("luajit", "sol2", "osmanip")

set_languages("c++20")

target("Kiosk")
    set_default(true)
    set_exceptions("cxx")
    set_kind("binary")
    add_includedirs("include")
    add_headerfiles("include/**.h")
    add_files("src/**.cpp")
    add_packages("luajit", "sol2", "osmanip")
    set_warnings("allextra", "error")
    add_links("User32", "Shell32")