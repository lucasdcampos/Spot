# The sandbox's asset generator: writes the models, textures and sounds of sandbox/assets/generated/ from code, with
# fixed seeds, so they are the same on every run. The build runs it whenever it changes, and its output is imported
# like any artist's assets; only their .meta files, which hold their ids, are committed. It can also be run by hand:
#
#     sandbox_generator [<output folder>]

# Painting is pure arithmetic, and unoptimized it takes minutes of every Debug build of the sandbox and of its tests,
# so the generator is optimized in Debug builds too; MSVC's run-time checks, which forbid that, are left out of this
# folder's flags.
if(MSVC)
    string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Od" "/O2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Ob0" "/Ob2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
else()
    string(APPEND CMAKE_CXX_FLAGS_DEBUG " -O2")
endif()

add_library(sandbox_generator_core STATIC
    rigs.h
    rigs.cpp
    effect_textures.cpp
    effect_textures.h
    foliage_textures.cpp
    foliage_textures.h
    generator.cpp
    generator.h
    gltf_writer.cpp
    gltf_writer.h
    icons.cpp
    icons.h
    level_models.cpp
    level_models.h
    level_textures.cpp
    level_textures.h
    output.cpp
    output.h
    shapes.cpp
    shapes.h
    sounds.cpp
    sounds.h
    prop_textures.cpp
    prop_textures.h
    texture_painting.h
    textures.cpp
    textures.h
    trees.cpp
    trees.h
    weapons.cpp
    weapons.h
)
target_link_libraries(sandbox_generator_core PUBLIC spot::spot PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(sandbox_generator_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# cgltf's writer, from the engine's copy of cgltf.
FetchContent_GetProperties(cgltf)
target_include_directories(sandbox_generator_core SYSTEM PRIVATE ${cgltf_SOURCE_DIR})
spot_set_compile_options(sandbox_generator_core)
spot_precompile_engine_header(sandbox_generator_core)

add_executable(sandbox_generator main.cpp)
target_link_libraries(sandbox_generator PRIVATE sandbox_generator_core)
target_compile_definitions(sandbox_generator PRIVATE
    SANDBOX_GENERATED_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/../assets/generated")
spot_set_compile_options(sandbox_generator)

# Runs again only when the generator is rebuilt; delete the stamp to write the assets again without changing it.
set(sandbox_generated_stamp "${CMAKE_CURRENT_BINARY_DIR}/sandbox_generated.stamp")
add_custom_command(
    OUTPUT "${sandbox_generated_stamp}"
    COMMAND sandbox_generator
    COMMAND "${CMAKE_COMMAND}" -E touch "${sandbox_generated_stamp}"
    DEPENDS sandbox_generator
    COMMENT "Generating the sandbox's models, textures and sounds"
    VERBATIM
)
add_custom_target(sandbox_generated_assets DEPENDS "${sandbox_generated_stamp}")
