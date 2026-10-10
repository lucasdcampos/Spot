# The sandbox's asset generator: writes the models, textures and sounds of sandbox/assets/generated/ from code, with
# fixed seeds, so they are the same on every run. Its output is committed and imported like any artist's assets;
# run it again after changing it:
#
#     sandbox_generator [<output folder>]
add_library(sandbox_generator_core STATIC
    generator.cpp
    generator.h
    gltf_writer.cpp
    gltf_writer.h
    output.cpp
    output.h
    shapes.cpp
    shapes.h
    sounds.cpp
    sounds.h
    textures.cpp
    textures.h
)
target_link_libraries(sandbox_generator_core PUBLIC spot::spot)
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
