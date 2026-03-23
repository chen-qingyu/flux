set_project("flux")
set_version("0.1.0")
set_license("MIT")
set_languages("cxx20")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_requires("entt", "pugixml", "spdlog", "magic_enum")

target("flux")
    set_kind("binary")
    set_rundir(".")
    add_packages("entt", "pugixml", "spdlog", "magic_enum")
    add_files("src/*.cpp")
