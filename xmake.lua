set_xmakever("3.0.0")

-- CommonLibF4 is pinned as a repository submodule for reproducible builds.
includes("extern/commonlibf4")

-- Enable typed INI parsing support from the pinned CommonLib dependency.
set_config("commonlib_ini", true)

set_project("PowerArmorPipBoyUI")
	set_version("0.3.7")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")
set_policy("package.requires_lock", true)

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

target("PowerArmorPipBoyUI")
    add_rules("commonlibf4.plugin", {
        name = "PowerArmorPipBoyUI",
        author = "",
        description = "Show Fallout 4's Pip-Boy menu as a fullscreen power-armor-style interface.",
        plugin_template = path.join(os.projectdir(), "res/commonlibf4-plugin.cpp.in"),
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
    add_installfiles("res/PowerArmorPipBoyUI.ini", { prefixdir = "F4SE/Plugins" })
    add_extrafiles(".clang-format")
