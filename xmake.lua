set_project("flux")
set_version("0.1.0")
set_license("MIT")
set_languages("cxx20")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_requires("entt", "pugixml", "spdlog", "magic_enum", "catch2", "argparse", "pybind11", "csvparser")
if is_plat("linux") then
    add_requireconfs("pybind11.python", {override = true, configs = {headeronly = true}})
end

target("flux-lib")
    set_kind("static")
    add_packages("entt", "pugixml", "spdlog", "magic_enum", "csvparser", {public = true})
    add_files("src/core/*.cpp")
    if is_plat("linux") then
        add_cxflags("-fPIC")
    end

target("flux")
    set_kind("binary")
    set_rundir(".")
    add_packages("argparse")
    add_deps("flux-lib")
    add_files("src/main.cpp")

target("_native")
    add_rules("python.module")
    add_deps("flux-lib")
    add_packages("pybind11")
    add_files("src/python_module.cpp")

target("test")
    set_kind("binary")
    set_rundir(".")
    add_deps("flux-lib")
    add_packages("catch2")
    add_files("tests/*.cpp")
