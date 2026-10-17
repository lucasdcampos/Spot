# Shader build: HLSL sources compiled to every format the graphics backend may ask for.
#   SPIR-V (Vulkan)   glslang, built from source, twice: for SDL_GPU, with each texture combined with its sampler, and
#                     for the NVRHI backend, with textures and samplers apart and bindings offset by resource type
#                     as NVRHI numbers them
#   MSL (Metal)       SPIRV-Cross, translated from the SPIR-V
#   DXIL (Direct3D)   the DXC that ships with the Windows SDK, when it is installed
# The results are embedded in a generated header, so shaders need no files at runtime.

include(FetchContent)

# SPIRV-Cross targets an old CMake policy where option() ignores the variables set below.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(ENABLE_OPT OFF)
set(ENABLE_GLSLANG_BINARIES ON)
set(GLSLANG_TESTS OFF)
set(GLSLANG_ENABLE_INSTALL OFF)
set(BUILD_EXTERNAL OFF)
FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG 16.6.0
    GIT_SHALLOW TRUE
    SYSTEM
)

set(SPIRV_CROSS_CLI ON)
set(SPIRV_CROSS_ENABLE_TESTS OFF)
set(SPIRV_CROSS_SHARED OFF)
set(SPIRV_CROSS_STATIC ON)
set(SPIRV_CROSS_SKIP_INSTALL ON)
FetchContent_Declare(spirv_cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG vulkan-sdk-1.4.363.0
    GIT_SHALLOW TRUE
    SYSTEM
)

# glslang warns at configure time that its HLSL front end will be removed in a future major release. The tag above is
# pinned, and the HLSL sources would move to another compiler unchanged, so the warning needs no action for now.
FetchContent_MakeAvailable(glslang spirv_cross)

# DXIL is only needed for Direct3D 12, so it is only built on Windows hosts.
if(CMAKE_HOST_WIN32 AND NOT DEFINED SPOT_DXC)
    file(GLOB windows_sdk_dxc "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x64/dxc.exe")
    if(windows_sdk_dxc)
        list(SORT windows_sdk_dxc COMPARE NATURAL ORDER DESCENDING)
        list(GET windows_sdk_dxc 0 latest_dxc)
        set(SPOT_DXC "${latest_dxc}" CACHE FILEPATH "DXC used to compile shaders to DXIL; empty to skip DXIL")
    endif()
endif()

if(SPOT_DXC)
    message(STATUS "Spot: compiling shaders to DXIL with ${SPOT_DXC}")
else()
    message(STATUS "Spot: DXC not found, shaders will not include DXIL (Direct3D 12 unavailable)")
endif()

set(SPOT_EMBED_SHADER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/spot_embed_shader.cmake")
set(SPOT_EMBED_COMMON_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/spot_embed_common.cmake")

# spot_add_shaders(<target> [NAMESPACE <namespace>] <file.vert.hlsl | file.frag.hlsl>...)
# Compiles each shader and generates "<file>.h" (e.g. "sprite.vert.h"), which defines a Spot::ShaderDesc named after
# the file in PascalCase (e.g. Shaders::SpriteVert), ready for GraphicsDevice::CreateShader().
function(spot_add_shaders target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "NAMESPACE" "")
    if(NOT ARG_NAMESPACE)
        set(ARG_NAMESPACE Shaders)
    endif()

    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/spot_shaders/${target}")
    set(headers)

    foreach(source IN LISTS ARG_UNPARSED_ARGUMENTS)
        get_filename_component(source "${source}" ABSOLUTE)
        get_filename_component(file_name "${source}" NAME)

        if(file_name MATCHES "^(.+)\\.vert\\.hlsl$")
            set(stage Vertex)
            set(glslang_stage vert)
            set(dxc_profile vs_6_0)
        elseif(file_name MATCHES "^(.+)\\.frag\\.hlsl$")
            set(stage Fragment)
            set(glslang_stage frag)
            set(dxc_profile ps_6_0)
        else()
            message(FATAL_ERROR "spot_add_shaders: '${file_name}' must end in .vert.hlsl or .frag.hlsl")
        endif()

        string(REGEX REPLACE "\\.hlsl$" "" base_name "${file_name}")
        set(spirv "${output_dir}/${base_name}.spv")
        set(vulkan_spirv "${output_dir}/${base_name}.vk.spv")
        set(msl "${output_dir}/${base_name}.msl")
        set(reflection "${output_dir}/${base_name}.json")
        set(header "${output_dir}/${base_name}.h")

        set(commands
            # SDL_GPU binds each texture together with its sampler, so separate HLSL textures and samplers are
            # combined.
            COMMAND $<TARGET_FILE:glslang-standalone> -D -V -S ${glslang_stage} -e main --auto-sampled-textures
                    --quiet "${source}" -o "${spirv}"
            COMMAND $<TARGET_FILE:spirv-cross> "${spirv}" --msl --msl-version 20100 --msl-decoration-binding
                    --output "${msl}"
            COMMAND $<TARGET_FILE:spirv-cross> "${spirv}" --reflect --output "${reflection}"
            # NVRHI binds textures and samplers apart, each register space being a descriptor set, and numbers
            # bindings as its default VulkanBindingOffsets do: shader resources from 0, samplers from 128, constant
            # buffers from 256 and unordered access views from 384.
            COMMAND $<TARGET_FILE:glslang-standalone> -D -V -S ${glslang_stage} -e main --hlsl-iomap
                    --shift-texture-binding 0 --shift-ssbo-binding 0 --shift-sampler-binding 128
                    --shift-ubo-binding 256 --shift-uav-binding 384 --quiet "${source}" -o "${vulkan_spirv}"
        )
        set(dxil_argument)
        if(SPOT_DXC)
            set(dxil "${output_dir}/${base_name}.dxil")
            list(APPEND commands COMMAND "${SPOT_DXC}" -nologo -T ${dxc_profile} -E main "${source}" -Fo "${dxil}")
            set(dxil_argument "-DDXIL=${dxil}")
        endif()

        add_custom_command(
            OUTPUT "${header}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
            ${commands}
            COMMAND ${CMAKE_COMMAND} "-DNAME=${base_name}" "-DNAMESPACE=${ARG_NAMESPACE}" "-DSTAGE=${stage}"
                    "-DSOURCE_NAME=${file_name}" "-DSPIRV=${spirv}" "-DVULKAN_SPIRV=${vulkan_spirv}" "-DMSL=${msl}"
                    "-DREFLECTION=${reflection}"
                    ${dxil_argument} "-DOUTPUT=${header}" -P "${SPOT_EMBED_SHADER_SCRIPT}"
            DEPENDS "${source}" "${SPOT_EMBED_SHADER_SCRIPT}" "${SPOT_EMBED_COMMON_SCRIPT}" glslang-standalone
                    spirv-cross
            COMMENT "Compiling shader ${file_name}"
            VERBATIM
        )
        list(APPEND headers "${header}")
    endforeach()

    target_sources(${target} PRIVATE ${headers})
    target_include_directories(${target} PRIVATE "${output_dir}")
endfunction()
