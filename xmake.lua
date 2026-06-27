set_project("flux")
set_version("1.0.0")
set_license("MIT")
set_languages("cxx20")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_requires("entt 3.16", "pugixml 1.15", "spdlog 1.17", "magic_enum 0.9", "catch2 3.14", "argparse 3.2", "pybind11 3.0", "csvparser 3.1")
if is_plat("linux") then
    add_requireconfs("pybind11.python", {override = true, configs = {headeronly = true}})
end

target("core")
    set_kind("static")
    add_packages("entt", "pugixml", "spdlog", "magic_enum", "csvparser", {public = true})
    add_files("src/core/*.cpp")
    on_load(function(target)
        local csvparser = target:pkg("csvparser")
        target:add("files", path.join(csvparser:installdir(), "include", "internal", "*.cpp"))
    end)
    if is_plat("linux") then
        add_cxflags("-fPIC")
    end

target("cli")
    set_kind("binary")
    set_rundir(".")
    add_packages("argparse")
    add_deps("core")
    add_files("src/main.cpp")

target("lib")
    add_rules("python.module")
    add_deps("core")
    add_packages("pybind11")
    add_files("src/python_module.cpp")

target("test")
    set_kind("binary")
    set_rundir(".")
    add_deps("core")
    add_packages("catch2")
    add_files("tests/*.cpp")
