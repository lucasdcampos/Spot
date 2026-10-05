add_library(spot STATIC
    spot.h
    version.h
    version.cpp
    assets/asset_database.h
    assets/asset_database.cpp
    assets/asset_id.h
    assets/asset_id.cpp
    assets/asset_meta.h
    assets/asset_meta.cpp
    assets/asset_type.h
    assets/asset_type.cpp
    audio/audio_device.h
    audio/audio_device.cpp
    audio/audio_output.h
    audio/mixer.h
    audio/mixer.cpp
    audio/playback.h
    audio/sound.h
    audio/sound.cpp
    core/application.h
    core/application.cpp
    core/assert.h
    core/assert.cpp
    core/log.h
    core/log.cpp
    core/log_sinks.h
    core/log_sinks.cpp
    core/paths.h
    core/paths.cpp
    core/time.h
    core/time.cpp
    core/utf8.h
    core/utf8.cpp
    events/event.h
    graphics/buffer.h
    graphics/camera_2d.h
    graphics/camera_2d.cpp
    graphics/camera_3d.h
    graphics/camera_3d.cpp
    graphics/color.h
    graphics/font.h
    graphics/font.cpp
    graphics/font_atlas.h
    graphics/font_atlas.cpp
    graphics/graphics_device.h
    graphics/image.h
    graphics/image.cpp
    graphics/material.h
    graphics/mesh.h
    graphics/mesh.cpp
    graphics/mesh_data.h
    graphics/mesh_data.cpp
    graphics/model_data.h
    graphics/model.h
    graphics/model.cpp
    graphics/model_data.cpp
    graphics/pipeline.h
    graphics/pipeline.cpp
    graphics/quad_batch.h
    graphics/quad_batch.cpp
    graphics/render_context.h
    graphics/renderer_2d.h
    graphics/renderer_2d.cpp
    graphics/renderer_3d.h
    graphics/renderer_3d.cpp
    graphics/shader.h
    graphics/text_layout.h
    graphics/text_layout.cpp
    graphics/texture.h
    input/input.h
    input/input.cpp
    input/input_state.h
    input/input_state.cpp
    input/keys.h
    input/keys.cpp
    math/angles.h
    math/glm_conversions.h
    math/matrix.h
    math/matrix.cpp
    math/quaternion.h
    math/quaternion.cpp
    math/vector.h
    platform/platform.h
    scene/archive.h
    scene/archive.cpp
    scene/archive_state.h
    scene/component.h
    scene/component.cpp
    scene/component_registry.h
    scene/component_registry.cpp
    scene/entity.h
    scene/entity.cpp
    scene/mesh_renderer.h
    scene/scene.h
    scene/scene.cpp
    scene/scene_serialization.cpp
    scene/transform.h
    scene/transform.cpp
    window/window.h
)
add_library(spot::spot ALIAS spot)

spot_add_shaders(spot NAMESPACE Spot::Detail::Shaders
    graphics/shaders/mesh.frag.hlsl
    graphics/shaders/mesh.vert.hlsl
    graphics/shaders/sprite.frag.hlsl
    graphics/shaders/sprite.vert.hlsl
)

include(FetchContent)

# glm implements the heavier math behind the engine's own math types; no public header includes it.
set(GLM_BUILD_LIBRARY OFF)
set(GLM_BUILD_TESTS OFF)
set(GLM_BUILD_INSTALL OFF)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(glm)
target_link_libraries(spot PRIVATE glm::glm-header-only)

# nlohmann/json reads and writes saved scenes; like glm, no public header includes it.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    SYSTEM
)
FetchContent_MakeAvailable(nlohmann_json)
target_link_libraries(spot PRIVATE nlohmann_json::nlohmann_json)

# stb_image decodes images and stb_vorbis Ogg Vorbis audio; they are single files without a CMake project.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    SYSTEM
)
FetchContent_MakeAvailable(stb)
target_include_directories(spot SYSTEM PRIVATE ${stb_SOURCE_DIR})

# cgltf reads glTF models; like stb, it is a single header.
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.15
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(cgltf)
target_include_directories(spot SYSTEM PRIVATE ${cgltf_SOURCE_DIR})

# dr_wav decodes WAV audio; like stb, it is a single header.
FetchContent_Declare(dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG dfe8377631000664666519fdb83da193fd8037f4
    SYSTEM
)
FetchContent_MakeAvailable(dr_libs)
target_include_directories(spot SYSTEM PRIVATE ${dr_libs_SOURCE_DIR})

# The backend is chosen at build time. Its types never reach the public headers, so swapping it does not
# change the engine API.
set(SPOT_PLATFORM "SDL3" CACHE STRING "Windowing and input backend")
set_property(CACHE SPOT_PLATFORM PROPERTY STRINGS SDL3)

if(SPOT_PLATFORM STREQUAL "SDL3")
    set(SDL_SHARED OFF)
    set(SDL_STATIC ON)
    set(SDL_TEST_LIBRARY OFF)

    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.18
        GIT_SHALLOW TRUE
        SYSTEM
    )
    FetchContent_MakeAvailable(SDL3)

    target_sources(spot PRIVATE
        platform/sdl3/sdl3_events.cpp
        platform/sdl3/sdl3_paths.cpp
        platform/sdl3/sdl3_platform.cpp
        platform/sdl3/sdl3_window.cpp
    )
    target_link_libraries(spot PRIVATE SDL3::SDL3-static)
else()
    message(FATAL_ERROR "Unknown SPOT_PLATFORM '${SPOT_PLATFORM}'")
endif()

# Like the platform backend, the graphics backend never reaches the public headers.
set(SPOT_GRAPHICS "SDL_GPU" CACHE STRING "Graphics backend")
set_property(CACHE SPOT_GRAPHICS PROPERTY STRINGS SDL_GPU)

if(SPOT_GRAPHICS STREQUAL "SDL_GPU")
    if(NOT SPOT_PLATFORM STREQUAL "SDL3")
        message(FATAL_ERROR "The SDL_GPU graphics backend requires the SDL3 platform")
    endif()

    target_sources(spot PRIVATE
        graphics/sdl_gpu/sdl_gpu_internal.h
        graphics/sdl_gpu/sdl_gpu_device.cpp
        graphics/sdl_gpu/sdl_gpu_render_context.cpp
        graphics/sdl_gpu/sdl_gpu_resources.cpp
    )
    if(SPOT_DXC)
        target_compile_definitions(spot PRIVATE SPOT_SHADERS_DXIL)
    endif()
else()
    message(FATAL_ERROR "Unknown SPOT_GRAPHICS '${SPOT_GRAPHICS}'")
endif()

# Like the graphics backend, the audio backend never reaches the public headers. It only plays what the engine's mixer
# produces.
set(SPOT_AUDIO "SDL3" CACHE STRING "Audio backend")
set_property(CACHE SPOT_AUDIO PROPERTY STRINGS SDL3)

if(SPOT_AUDIO STREQUAL "SDL3")
    if(NOT SPOT_PLATFORM STREQUAL "SDL3")
        message(FATAL_ERROR "The SDL3 audio backend requires the SDL3 platform")
    endif()

    target_sources(spot PRIVATE
        audio/sdl3/sdl3_audio_output.cpp
    )
else()
    message(FATAL_ERROR "Unknown SPOT_AUDIO '${SPOT_AUDIO}'")
endif()

target_include_directories(spot PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_features(spot PUBLIC cxx_std_20)

target_compile_definitions(spot PRIVATE
    SPOT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SPOT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SPOT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    SPOT_VERSION_STRING="${PROJECT_VERSION}"
)

spot_set_compile_options(spot)
