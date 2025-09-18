add_rules("mode.debug", "mode.release")

add_requires("luajit", "sol2", "osmanip")
if is_plat("linux") then
    add_requires("libx11", "libxinerama", "libxtst")
end

set_languages("c++20")

target("Kiosk")
    set_default(true)
    set_exceptions("cxx")
    set_kind("binary")
    add_includedirs("include")
    add_headerfiles("include/**.h")
    add_files("src/**.cpp")
    add_packages("luajit", "sol2", "osmanip")
    if is_plat("linux") then
        add_packages("libx11", "libxinerama", "libxtst")
    end
    set_warnings("allextra", "error")
    if is_plat("windows") then
        add_links("User32", "Shell32")
    end