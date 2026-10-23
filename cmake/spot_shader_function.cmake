# spot_add_shaders(), apart from fetching and building the tools it runs, so the engine's CMake package can give it to
# games with the tools the engine's build made. Whoever includes this sets:
#   SPOT_SHADER_GLSLANG, SPOT_SHADER_SPIRV_CROSS   the tools' executables (generator expressions allowed)
#   SPOT_SHADER_TOOL_TARGETS                       their targets, which the commands depend on, when in this build
#   SPOT_DXC                                       DXC, or empty to compile no DXIL
#   SPOT_SHADER_INCLUDE_DIR                        the engine's shader headers, such as surface_lighting.hlsl

set(SPOT_EMBED_SHADER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/spot_embed_shader.cmake")
set(SPOT_EMBED_COMMON_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/spot_embed_common.cmake")

# spot_add_shaders(<target> [NAMESPACE <namespace>] [INCLUDE_DIRECTORIES <dir>...]
#                  <file.vert.hlsl | file.frag.hlsl | file.comp.hlsl>...)
# Compiles each shader and generates "<file>.h" (e.g. "sprite.vert.h"), which defines a Spot::ShaderDesc named after
# the file in PascalCase (e.g. Shaders::SpriteVert), ready for GraphicsDevice::CreateShader(). Shaders #include headers
# from their own folder, from the engine's shader headers (SPOT_SHADER_INCLUDE_DIR) and from INCLUDE_DIRECTORIES; a
# change to any .hlsl file in those folders compiles them again.
function(spot_add_shaders target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "NAMESPACE" "INCLUDE_DIRECTORIES")
    if(NOT ARG_NAMESPACE)
        set(ARG_NAMESPACE Shaders)
    endif()

    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/spot_shaders/${target}")
    set(headers)

    set(include_directories "${SPOT_SHADER_INCLUDE_DIR}")
    foreach(directory IN LISTS ARG_INCLUDE_DIRECTORIES)
        get_filename_component(directory "${directory}" ABSOLUTE)
        list(APPEND include_directories "${directory}")
    endforeach()
    set(include_arguments)
    set(dxc_include_arguments)
    set(include_files)
    foreach(directory IN LISTS include_directories)
        list(APPEND include_arguments "-I${directory}")
        list(APPEND dxc_include_arguments -I "${directory}")
        file(GLOB directory_files CONFIGURE_DEPENDS "${directory}/*.hlsl")
        list(APPEND include_files ${directory_files})
    endforeach()

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
        elseif(file_name MATCHES "^(.+)\\.comp\\.hlsl$")
            set(stage Compute)
            set(glslang_stage comp)
            set(dxc_profile cs_6_0)
        else()
            message(FATAL_ERROR "spot_add_shaders: '${file_name}' must end in .vert.hlsl, .frag.hlsl or .comp.hlsl")
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
            COMMAND "${SPOT_SHADER_GLSLANG}" -D -V -S ${glslang_stage} -e main --auto-sampled-textures
                    ${include_arguments} --quiet "${source}" -o "${spirv}"
            COMMAND "${SPOT_SHADER_SPIRV_CROSS}" "${spirv}" --msl --msl-version 20100 --msl-decoration-binding
                    --output "${msl}"
            COMMAND "${SPOT_SHADER_SPIRV_CROSS}" "${spirv}" --reflect --output "${reflection}"
            # NVRHI binds textures and samplers apart, each register space being a descriptor set, and numbers
            # bindings as its default VulkanBindingOffsets do: shader resources from 0, samplers from 128, constant
            # buffers from 256 and unordered access views from 384.
            COMMAND "${SPOT_SHADER_GLSLANG}" -D -V -S ${glslang_stage} -e main --hlsl-iomap
                    --shift-texture-binding 0 --shift-ssbo-binding 0 --shift-sampler-binding 128
                    --shift-ubo-binding 256 --shift-uav-binding 384 ${include_arguments} --quiet "${source}"
                    -o "${vulkan_spirv}"
        )
        set(dxil_argument)
        if(SPOT_DXC)
            set(dxil "${output_dir}/${base_name}.dxil")
            # Shaders tell glslang the format of the storage textures they write with [[spv::format_*]], which
            # Direct3D needs not.
            list(APPEND commands COMMAND "${SPOT_DXC}" -nologo -Wno-unknown-attributes -T ${dxc_profile} -E main
                                 ${dxc_include_arguments} "${source}" -Fo "${dxil}")
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
            DEPENDS "${source}" ${include_files} "${SPOT_EMBED_SHADER_SCRIPT}" "${SPOT_EMBED_COMMON_SCRIPT}"
                    ${SPOT_SHADER_TOOL_TARGETS}
            COMMENT "Compiling shader ${file_name}"
            VERBATIM
        )
        list(APPEND headers "${header}")
    endforeach()

    target_sources(${target} PRIVATE ${headers})
    target_include_directories(${target} PRIVATE "${output_dir}")
endfunction()
