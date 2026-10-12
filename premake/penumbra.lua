-- Penumbra game executable project (this fork; see docs/penumbra-levels.md).
-- Declared only with --product=penumbra (premake5.lua), so the default
-- Amnesia and AMFP generation is unchanged. It reads Penumbra's data from the
-- player's install at run time; penumbra/resources holds this product's own
-- files and is staged next to the executable after every build.
local PENUMBRA = ROOT .. "/penumbra"
local RESOURCES = PENUMBRA .. "/resources"
local PRODUCT = product_context("penumbra")

local function winpath(p) return (p:gsub("/", "\\")) end

product_activate(PRODUCT)

project(PRODUCT.project)
    language "C++"
    set_output("runtime", PRODUCT.product)
    targetname "Penumbra"
    filter "system:windows"
        kind "WindowedApp"
    filter "system:not windows"
        kind "ConsoleApp"
    filter {}

    -- src/common is shared with this product's level editor (premake/tools.lua).
    files { PENUMBRA .. "/src/game/*.cpp", PENUMBRA .. "/src/game/*.h",
            PENUMBRA .. "/src/common/*.cpp", PENUMBRA .. "/src/common/*.h" }

    includedirs {
        ROOT .. "/HPL2/core/include",
        PENUMBRA .. "/src/game",
        PENUMBRA .. "/src/common",
        ROOT .. "/amnesia/slang",
        ROOT .. "/amnesia/glsl",
        ANGELSCRIPT_AMFP_INCLUDE,
        DEPS_EXTERN .. "/tinyxml2",
    }
    config_stamp(PRODUCT.project, { "PENUMBRA", ANGELSCRIPT_AMFP_INCLUDE })
    generated_includes()
    deps_public_includes()
    vulkan_includes()
    mathlib_use()

    defines { "USE_SDL2" }

    link_engine("game", PRODUCT.product)

    -- The shared shader sources, compiled into build-premake/penumbra/<config>/.
    slang_prebuild(PRODUCT)
    if os.target() == "windows" and _OPTIONS["with-d3d12"] == "yes" then
        defines { "DEVICE_SUPPORT_D3D12" }
        slang_dxil_production_prebuild(PRODUCT)
    end

    -- This product's own files (configs, generated textures, font, GUI skin)
    -- go to the runtime directory, the engine's working directory: the engine
    -- registers core/textures there itself while it starts.
    local dest = runtime_dir(PRODUCT.product, "")
    filter "system:not windows"
        postbuildcommands {
            string.format('{MKDIR} "%s"', dest),
            string.format('cp -R "%s/." "%s/"', RESOURCES, dest),
        }
    filter "system:windows"
        postbuildcommands {
            string.format('xcopy /E /I /Y /Q "%s" "%s\\" >nul', winpath(RESOURCES), winpath(dest)),
        }
    filter {}

    filter "system:linux"
        defines { "LINUX" }
        buildoptions { "-Wno-switch", "-Wno-undefined-var-template", "-Wno-extern-c-compat" }
        linkoptions { "-Wl,-rpath,'$$ORIGIN/libs'", "-Wl,-rpath,'$$ORIGIN'" }
        linkgroups "On"
    filter {}

product_deactivate()
