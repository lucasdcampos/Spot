# The engine's code is compiled once, then linked both as a static library, for games made only in code, and as a
# shared one, for the editor and the game modules it loads, so they share one copy of the engine's state: its
# registered components, logs, input and assets.
add_library(spot_objects OBJECT
    spot.h
    version.h
    version.cpp
    assets/asset_file.h
    assets/asset_file.cpp
    assets/asset_formats.h
    assets/asset_formats.cpp
    assets/asset_id.h
    assets/asset_id.cpp
    assets/asset_manifest.h
    assets/asset_manifest.cpp
    assets/asset_manager.h
    assets/asset_manager.cpp
    assets/asset_ref.h
    assets/asset_type.h
    assets/asset_type.cpp
    assets/built_in_assets.h
    assets/assets.h
    assets/assets.cpp
    assets/material_source.h
    assets/material_source.cpp
    audio/audio_device.h
    audio/audio_device.cpp
    audio/audio_output.h
    audio/mixer.h
    audio/mixer.cpp
    audio/playback.h
    audio/sound.h
    audio/sound.cpp
    audio/audio_settings.h
    audio/audio_settings.cpp
    audio/audio_source.h
    audio/audio_source.cpp
    audio/audio_system.h
    audio/audio_system.cpp
    audio/sound_decoder.h
    audio/sound_stream.h
    audio/sound_stream.cpp
    core/application.h
    core/application.cpp
    core/assert.h
    core/assert.cpp
    core/crash_handler.h
    core/crash_handler.cpp
    core/crash_handler_internal.h
    core/diagnostics.h
    core/diagnostics.cpp
    core/game_module.h
    core/game_module.cpp
    core/game_settings.h
    core/game_settings.cpp
    core/log.h
    core/log.cpp
    core/log_sinks.h
    core/log_sinks.cpp
    core/paths.h
    core/paths.cpp
    core/process.h
    core/profiler.h
    core/profiler.cpp
    core/shared_library.h
    core/time.h
    core/time.cpp
    core/utf8.h
    core/utf8.cpp
    events/event.h
    graphics/atmosphere.h
    graphics/atmosphere.cpp
    graphics/buffer.h
    graphics/camera_2d.h
    graphics/bloom.cpp
    graphics/bloom.h
    graphics/camera_2d.cpp
    graphics/camera_3d.h
    graphics/camera_3d.cpp
    graphics/color.h
    graphics/debug_draw.h
    graphics/debug_draw.cpp
    graphics/font.h
    graphics/fog_data.h
    graphics/font.cpp
    graphics/font_atlas.h
    graphics/fullscreen_pass.cpp
    graphics/fxaa.cpp
    graphics/fxaa.h
    graphics/fullscreen_pass.h
    graphics/font_atlas.cpp
    graphics/graphics_device.h
    graphics/image.h
    graphics/image.cpp
    graphics/light_data.h
    graphics/line_renderer.h
    graphics/line_renderer.cpp
    graphics/material.h
    graphics/mesh.h
    graphics/mesh.cpp
    graphics/mesh_data.h
    graphics/mikktspace.cpp
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
    graphics/render_stage.h
    graphics/renderer_2d.h
    graphics/renderer_2d.cpp
    graphics/renderer_3d.h
    graphics/rendering_settings.cpp
    graphics/rendering_settings.h
    graphics/renderer_3d.cpp
    graphics/scene_renderer.h
    graphics/scene_renderer.cpp
    graphics/shader.h
    graphics/shadow_views.h
    graphics/shadow_views.cpp
    graphics/sky_data.h
    graphics/text_layout.h
    graphics/text_layout.cpp
    graphics/texture.h
    graphics/tonemapper.cpp
    graphics/tonemapper.h
    input/input.h
    input/input.cpp
    input/input_action.h
    input/input_action.cpp
    input/input_settings.h
    input/input_settings.cpp
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
    physics/character_controller.h
    physics/character_controller.cpp
    physics/collider.h
    physics/collider.cpp
    physics/collision.h
    physics/joint.h
    physics/joint.cpp
    physics/physics_debug.h
    physics/physics_debug.cpp
    physics/physics_query.h
    physics/physics_simulation.h
    physics/physics_settings.h
    physics/physics_settings.cpp
    physics/physics_world.h
    physics/physics_world.cpp
    physics/physics_world_contacts.cpp
    physics/physics_world_joints.cpp
    physics/physics_world_queries.cpp
    physics/rigidbody.h
    physics/rigidbody.cpp
    platform/platform.h
    scene/archive.h
    scene/archive.cpp
    scene/archive_state.h
    scene/camera.h
    scene/camera.cpp
    scene/component.h
    scene/component.cpp
    scene/component_registry.h
    scene/component_serialization.h
    scene/component_registry.cpp
    scene/entity.h
    scene/entity.cpp
    scene/light.h
    scene/fog.cpp
    scene/fog.h
    scene/light.cpp
    scene/post_processing.cpp
    scene/post_processing.h
    scene/mesh_renderer.h
    scene/missing_component.h
    scene/mesh_renderer.cpp
    scene/scene.h
    scene/scene.cpp
    scene/scene_serialization.cpp
    scene/scene_system.h
    scene/scene_system.cpp
    scene/sky.h
    scene/sky.cpp
    scene/sprite_renderer.h
    scene/sprite_renderer.cpp
    scene/transform.h
    scene/transform.cpp
    window/clipboard.h
    window/file_dialog.h
    window/shell.cpp
    window/shell.h
    window/window.h
)

spot_add_shaders(spot_objects NAMESPACE Spot::Detail::Shaders
    graphics/shaders/bloom_down.frag.hlsl
    graphics/shaders/bloom_up.frag.hlsl
    graphics/shaders/fullscreen.vert.hlsl
    graphics/shaders/fxaa.frag.hlsl
    graphics/shaders/line.frag.hlsl
    graphics/shaders/line.vert.hlsl
    graphics/shaders/mesh.frag.hlsl
    graphics/shaders/mesh.vert.hlsl
    graphics/shaders/shadow.frag.hlsl
    graphics/shaders/shadow.vert.hlsl
    graphics/shaders/sky.frag.hlsl
    graphics/shaders/sky.vert.hlsl
    graphics/shaders/sky_irradiance.frag.hlsl
    graphics/shaders/sky_prefilter.frag.hlsl
    graphics/shaders/sprite.frag.hlsl
    graphics/shaders/sprite.vert.hlsl
    graphics/shaders/tonemap.frag.hlsl
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
target_link_libraries(spot_objects PRIVATE glm::glm-header-only)

# nlohmann/json reads and writes saved scenes; like glm, no public header includes it.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    SYSTEM
)
FetchContent_MakeAvailable(nlohmann_json)
target_link_libraries(spot_objects PRIVATE nlohmann_json::nlohmann_json)

# stb_image decodes images and stb_vorbis Ogg Vorbis audio; they are single files without a CMake project.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    SYSTEM
)
FetchContent_MakeAvailable(stb)
target_include_directories(spot_objects SYSTEM PRIVATE ${stb_SOURCE_DIR})

# cgltf reads glTF models; like stb, it is a single header.
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.15
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(cgltf)
target_include_directories(spot_objects SYSTEM PRIVATE ${cgltf_SOURCE_DIR})

# dr_wav decodes WAV audio; like stb, it is a single header.
# MikkTSpace, the tangent space glTF's normal maps are baked in; graphics/mikktspace.cpp compiles it.
FetchContent_Declare(mikktspace
    GIT_REPOSITORY https://github.com/mmikk/MikkTSpace.git
    GIT_TAG 3e895b49d05ea07e4c2133156cfa94369e19e409
    SYSTEM
)
FetchContent_MakeAvailable(mikktspace)
target_include_directories(spot_objects SYSTEM PRIVATE ${mikktspace_SOURCE_DIR})

FetchContent_Declare(dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG dfe8377631000664666519fdb83da193fd8037f4
    SYSTEM
)
FetchContent_MakeAvailable(dr_libs)
target_include_directories(spot_objects SYSTEM PRIVATE ${dr_libs_SOURCE_DIR})

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

    target_sources(spot_objects PRIVATE
        platform/sdl3/sdl3_clipboard.cpp
        platform/sdl3/sdl3_events.cpp
        platform/sdl3/sdl3_file_dialog.cpp
        platform/sdl3/sdl3_mouse_capture.h
        platform/sdl3/sdl3_paths.cpp
        platform/sdl3/sdl3_platform.cpp
        platform/sdl3/sdl3_process.cpp
        platform/sdl3/sdl3_shared_library.cpp
        platform/sdl3/sdl3_shell.cpp
        platform/sdl3/sdl3_window.cpp
    )
    target_link_libraries(spot_objects PRIVATE SDL3::SDL3-static)
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

    target_sources(spot_objects PRIVATE
        graphics/sdl_gpu/sdl_gpu_internal.h
        graphics/sdl_gpu/sdl_gpu_device.cpp
        graphics/sdl_gpu/sdl_gpu_render_context.cpp
        graphics/sdl_gpu/sdl_gpu_resources.cpp
    )
    if(SPOT_DXC)
        target_compile_definitions(spot_objects PRIVATE SPOT_SHADERS_DXIL)
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

    target_sources(spot_objects PRIVATE
        audio/sdl3/sdl3_audio_output.cpp
    )
else()
    message(FATAL_ERROR "Unknown SPOT_AUDIO '${SPOT_AUDIO}'")
endif()

# Like the audio backend, the physics backend never reaches the public headers. It only simulates the bodies the
# engine's physics world asks for.
set(SPOT_PHYSICS "JOLT" CACHE STRING "Physics backend")
set_property(CACHE SPOT_PHYSICS PROPERTY STRINGS JOLT)

if(SPOT_PHYSICS STREQUAL "JOLT")
    # Only the library itself: no samples, tools, GPU compute, debug renderer or profiler, and the same runtime
    # library and compiler flags as the engine.
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF)
    set(OVERRIDE_CXX_FLAGS OFF)
    set(ENABLE_ALL_WARNINGS OFF)
    set(INTERPROCEDURAL_OPTIMIZATION OFF)
    set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)
    set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
    set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
    set(ENABLE_OBJECT_STREAM OFF)
    set(ENABLE_INSTALL OFF)
    set(JPH_USE_DX12 OFF)
    set(JPH_USE_VK OFF)
    set(JPH_USE_MTL OFF)
    set(JPH_USE_CPU_COMPUTE OFF)

    FetchContent_Declare(JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG v5.6.0
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR Build
        SYSTEM
    )
    FetchContent_MakeAvailable(JoltPhysics)

    target_sources(spot_objects PRIVATE
        physics/jolt/jolt_characters.cpp
        physics/jolt/jolt_common.h
        physics/jolt/jolt_contacts.cpp
        physics/jolt/jolt_contacts.h
        physics/jolt/jolt_joints.cpp
        physics/jolt/jolt_layers.h
        physics/jolt/jolt_physics_simulation.cpp
        physics/jolt/jolt_queries.cpp
        physics/jolt/jolt_runtime.cpp
        physics/jolt/jolt_runtime.h
        physics/jolt/jolt_shapes.cpp
        physics/jolt/jolt_shapes.h
        physics/jolt/jolt_simulation_impl.h
    )
    target_link_libraries(spot_objects PRIVATE Jolt)
else()
    message(FATAL_ERROR "Unknown SPOT_PHYSICS '${SPOT_PHYSICS}'")
endif()

target_include_directories(spot_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Crashes are reported with the system's own tools: DbgHelp on Windows, signals and dladdr() elsewhere.
if(WIN32)
    target_sources(spot_objects PRIVATE core/crash_handler_windows.cpp)
    target_link_libraries(spot_objects PUBLIC dbghelp)
else()
    target_sources(spot_objects PRIVATE core/crash_handler_posix.cpp)
    target_link_libraries(spot_objects PUBLIC ${CMAKE_DL_LIBS})
endif()

target_compile_features(spot_objects PUBLIC cxx_std_20)

target_compile_definitions(spot_objects PRIVATE
    SPOT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SPOT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SPOT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    SPOT_VERSION_STRING="${PROJECT_VERSION}"
)

spot_set_compile_options(spot_objects)

# Shared libraries need code that works at any address.
set_target_properties(spot_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(spot STATIC)
target_link_libraries(spot PUBLIC spot_objects)
add_library(spot::spot ALIAS spot)

# Exports every function, so whatever links it reaches the whole engine, as with the static library.
add_library(spot_shared SHARED)
target_link_libraries(spot_shared PRIVATE spot_objects)
target_include_directories(spot_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_compile_features(spot_shared PUBLIC cxx_std_20)
set_target_properties(spot_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_library(spot::shared ALIAS spot_shared)

# A CMake package for game modules, which link the shared library of this build: find_package(Spot CONFIG) with
# Spot_DIR set to package/<configuration> in the build directory.
set(SPOT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
configure_file("${PROJECT_SOURCE_DIR}/cmake/SpotConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/SpotConfig.cmake.in" @ONLY)
file(GENERATE
    OUTPUT "${PROJECT_BINARY_DIR}/package/$<CONFIG>/SpotConfig.cmake"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/SpotConfig.cmake.in"
)

# Importing assets is for the tools, never for games, which load what was imported.
add_subdirectory(import)
