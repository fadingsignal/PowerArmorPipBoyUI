-- CommonLibF4 is pinned as a repository submodule for reproducible builds.
includes("extern/commonlibf4")

-- set project constants
set_project("PowerArmorPipBoyUI")
	set_version("0.3.3")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- define targets
target("PowerArmorPipBoyUI")
    add_rules("commonlibf4.plugin", {
        name = "PowerArmorPipBoyUI",
        author = "",
        description = "Show Fallout 4's Pip-Boy menu as a fullscreen power-armor-style interface.",
        plugin_template = path.join(os.projectdir(), "res/commonlibf4-plugin.cpp.in"),
    })

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
    add_installfiles("res/PowerArmorPipBoyUI.ini", { prefixdir = "F4SE/Plugins" })
