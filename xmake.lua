set_project("etch")

add_rules("mode.debug", "mode.release", "mode.releasedbg")
set_allowedplats("windows", "linux", "macosx")

set_languages("c++23")

option("dev", {default = true})
option("test", {default = true})

if has_config("test") then
    add_requires("boost_ut")
end

if has_config("dev") then
    -- Don't fetch system package
    set_policy("package.install_only", true)
    set_policy("build.ccache", true)
    if is_mode("debug") then
        set_policy("build.sanitizer.address", true)
    end

    add_rules("plugin.compile_commands.autoupdate", {outputdir = "build", lsp = "clangd"})

    if is_plat("windows") then
        set_runtimes("MD")

        local toolchain = get_config("toolchain")
        if toolchain == "clang" then
            add_ldflags("-fuse-ld=lld-link")
            add_shflags("-fuse-ld=lld-link")
        elseif toolchain == "clang-cl" then
            set_toolset("ld", "lld-link")
            set_toolset("sh", "lld-link")
        end
    end
end

target("lib")
    set_kind("headeronly")
    add_includedirs("lib/", {public = true})

for _, filepath in ipairs(os.files("demo/*.cc")) do
    local name = path.basename(filepath)
    target(name)
        set_kind("binary")
        add_files(filepath)
        add_deps("lib")
end

if has_config("test") then
    target("tnfa_test")
        set_kind("binary")
        add_files("test/*.cc")
        add_deps("lib")
        add_packages("boost_ut")
end
