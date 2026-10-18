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
    core/asset_id.h
    core/asset_id.cpp
    core/file_io.h
    core/file_io.cpp
    core/job_system.h
    core/job_system.cpp
    core/memory.h
    core/memory.cpp
    assets/asset_manifest.h
    assets/asset_pak.h
    assets/asset_manifest.cpp
    assets/asset_pak.cpp
    assets/asset_manager.h
    assets/asset_manager.cpp
    assets/asset_ref.h
    assets/asset_archive.h
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
    scene/audio/audio_source.h
    scene/audio/audio_source.cpp
    scene/audio/audio_system.h
    scene/audio/audio_system.cpp
    audio/sound_decoder.h
    audio/sound_stream.h
    audio/sound_stream.cpp
    runtime/application.h
    runtime/application.cpp
    core/console.cpp
    runtime/console_input.cpp
    runtime/runtime_stats.cpp
    core/profile_stream.cpp
    runtime/engine_console.cpp
    core/assert.h
    core/assert.cpp
    core/crash_handler.h
    core/crash_handler.cpp
    core/crash_handler_internal.h
    core/diagnostics.h
    core/diagnostics.cpp
    runtime/game_module.h
    runtime/game_module.cpp
    runtime/game_settings.h
    runtime/game_settings.cpp
    core/log.h
    core/spsc_queue.h
    core/log.cpp
    core/log_sinks.h
    core/log_sinks.cpp
    core/paths.h
    core/paths.cpp
    core/process.h
    core/save_data.h
    core/save_data.cpp
    core/profiler.h
    core/profiler.cpp
    core/shared_library.h
    core/signal.h
    core/task.h
    core/task.cpp
    core/time.h
    core/time.cpp
    core/timer.h
    core/timer.cpp
    core/tween.h
    core/tween.cpp
    core/utf8.h
    core/utf8.cpp
    events/event.h
    graphics/atmosphere.h
    graphics/atmosphere.cpp
    graphics/buffer.h
    graphics/compute.h
    graphics/camera_2d.h
    graphics/bloom.cpp
    graphics/bloom.h
    graphics/camera_2d.cpp
    graphics/pixel_perfect.cpp
    graphics/camera_3d.h
    graphics/camera_3d.cpp
    math/color.h
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
    graphics/graphics_backend.h
    graphics/graphics_device.h
    graphics/graphics_device.cpp
    graphics/image.h
    graphics/image.cpp
    graphics/light_clusters.cpp
    graphics/light_clusters.h
    graphics/light_data.h
    graphics/line_renderer.h
    graphics/line_renderer.cpp
    graphics/material.h
    graphics/material.cpp
    graphics/mesh.h
    graphics/mesh.cpp
    graphics/mesh_data.h
    graphics/mikktspace.cpp
    graphics/mesh_data.cpp
    scene/animation/skinned_mesh_renderer.h
    scene/animation/skinned_mesh_renderer.cpp
    animation/animator_controller.h
    animation/animator_controller.cpp
    animation/blend_tree.h
    animation/blend_tree.cpp
    scene/animation/animator.h
    scene/animation/animator.cpp
    animation/animation_motion.h
    animation/animation_motion.cpp
    scene/animation/animator_motion.cpp
    animation/animation_layer.h
    animation/animation_layer.cpp
    scene/animation/animator_layers.cpp
    scene/animation/bone_attachment.h
    scene/animation/bone_attachment.cpp
    scene/animation/ragdoll.h
    scene/animation/ragdoll.cpp
    animation/pose.h
    animation/pose.cpp
    animation/animation_data.h
    animation/animation_data.cpp
    animation/animation_formats.cpp
    graphics/model_data.h
    graphics/model.h
    graphics/model.cpp
    graphics/model_data.cpp
    graphics/pipeline.h
    graphics/pipeline.cpp
    graphics/quad_batch.h
    graphics/quad_batch.cpp
    graphics/sprite_atlas.cpp
    scene/rendering/sprite_animator.cpp
    scene/rendering/tilemap.cpp
    scene/physics2d/tilemap_collider_2d.cpp
    graphics/render_context.h
    graphics/render_stage.h
    graphics/renderer_2d.h
    graphics/renderer_2d.cpp
    runtime/console_overlay.cpp
    graphics/render_graph.h
    graphics/render_world.h
    graphics/renderer_3d.h
    scene/rendering/rendering_settings.cpp
    scene/rendering/rendering_settings.h
    graphics/render_graph.cpp
    graphics/render_world.cpp
    graphics/renderer_3d.cpp
    graphics/renderer_3d_depth.cpp
    graphics/particle_batch.h
    graphics/particle_batch.cpp
    graphics/particle_renderer.h
    graphics/particle_renderer.cpp
    graphics/screenshot.cpp
    graphics/shader.h
    graphics/shadow_views.h
    graphics/shadow_views.cpp
    graphics/sky_data.h
    graphics/text_layout.h
    graphics/text_layout.cpp
    graphics/texture.h
    graphics/texture_table.h
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
    particles/particle_settings.h
    particles/particle_settings.cpp
    particles/particle_simulation.h
    particles/particle_simulation.cpp
    scene/particles/particle_system.h
    scene/particles/particle_system.cpp
    math/curve.h
    math/curve.cpp
    math/gradient.h
    math/gradient.cpp
    math/angles.h
    math/glm_conversions.h
    math/matrix.h
    math/matrix.cpp
    math/quaternion.h
    math/quaternion.cpp
    math/vector.h
    scene/physics/character_controller.h
    scene/physics/character_controller.cpp
    scene/physics/collider.h
    scene/physics/collider.cpp
    scene/physics/collision.h
    scene/physics/joint.h
    scene/physics/joint.cpp
    scene/physics/physics_debug.h
    scene/physics/physics_debug.cpp
    scene/physics/physics_query.h
    physics/physics_simulation.h
    scene/physics/physics_settings.h
    scene/physics/physics_settings.cpp
    scene/physics/physics_world.h
    scene/physics/physics_world.cpp
    scene/physics/physics_world_contacts.cpp
    scene/physics/physics_world_joints.cpp
    scene/physics/physics_world_queries.cpp
    scene/physics/rigidbody.h
    scene/physics/rigidbody.cpp
    physics2d/physics_2d.h
    scene/physics2d/physics_world_2d.cpp
    scene/physics2d/collider_2d.cpp
    scene/physics2d/rigidbody_2d.cpp
    platform/platform.h
    core/archive.h
    core/archive.cpp
    core/archive_state.h
    scene/rendering/camera.h
    scene/rendering/camera.cpp
    scene/component.h
    scene/component_ref.h
    scene/component.cpp
    scene/component_type.h
    scene/component_requirements.h
    scene/component_type.cpp
    scene/component_registry.h
    scene/component_serialization.h
    scene/component_registry.cpp
    scene/entity.h
    scene/entity.cpp
    scene/rendering/light.h
    scene/rendering/fog.cpp
    scene/rendering/fog.h
    scene/rendering/light.cpp
    scene/rendering/post_processing.cpp
    scene/rendering/post_processing.h
    scene/prefab.h
    scene/prefab.cpp
    scene/prefab_serialization.cpp
    scene/rendering/mesh_renderer.h
    scene/rendering/render_world_system.h
    scene/missing_component.h
    scene/rendering/mesh_renderer.cpp
    scene/rendering/render_world_system.cpp
    scene/scene.h
    scene/scene.cpp
    scene/scene_model.cpp
    scene/scene_prefab.cpp
    scene/scene_assets.h
    scene/rendering/scene_drawing.h
    scene/rendering/scene_drawing.cpp
    scene/rendering/scene_renderer.h
    scene/rendering/scene_renderer.cpp
    scene/scene_assets.cpp
    scene/scene_serialization.cpp
    scene/scene_serializer.h
    scene/scene_system.h
    scene/scene_system.cpp
    scene/rendering/sky.h
    scene/rendering/sky.cpp
    scene/rendering/sprite_renderer.h
    scene/rendering/sprite_renderer.cpp
    scene/ui/ui_text_layout.cpp
    scene/ui/ui_widgets.cpp
    scene/ui/ui_layout_group.cpp
    scene/ui/ui_input_system.cpp
    scene/ui/ui_layout.cpp
    scene/ui/ui_system.cpp
    scene/rendering/text_renderer.cpp
    scene/rendering/text_renderer.h
    scene/transform.h
    scene/transform.cpp
    scene/tween_system.h
    scene/tween_system.cpp
    window/clipboard.h
    window/file_dialog.h
    window/shell.cpp
    window/shell.h
    window/window.h
)

spot_add_shaders(spot_objects NAMESPACE Spot::Detail::Shaders
    graphics/shaders/particle.vert.hlsl
    graphics/shaders/particle.frag.hlsl
    graphics/shaders/bloom_down.frag.hlsl
    graphics/shaders/bloom_up.frag.hlsl
    graphics/shaders/fullscreen.vert.hlsl
    graphics/shaders/mip_downsample.frag.hlsl
    graphics/shaders/mip_downsample_layer.frag.hlsl
    graphics/shaders/fxaa.frag.hlsl
    graphics/shaders/hiz_reduce.frag.hlsl
    graphics/shaders/world_cull.comp.hlsl
    graphics/shaders/line.frag.hlsl
    graphics/shaders/line.vert.hlsl
    graphics/shaders/mesh.frag.hlsl
    graphics/shaders/skinned_mesh.vert.hlsl
    graphics/shaders/skinned_shadow.vert.hlsl
    graphics/shaders/mesh.vert.hlsl
    graphics/shaders/shadow.frag.hlsl
    graphics/shaders/shadow.vert.hlsl
    graphics/shaders/shadow_cutout.frag.hlsl
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
target_include_directories(spot_objects SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/renderdoc)

# LZ4 compresses the assets of paks; only its one source file is built, without its CMake project.
FetchContent_Declare(lz4
    URL https://github.com/lz4/lz4/releases/download/v1.10.0/lz4-1.10.0.tar.gz
    SOURCE_SUBDIR no_cmake_project
    SYSTEM
)
FetchContent_MakeAvailable(lz4)
add_library(spot_lz4 STATIC ${lz4_SOURCE_DIR}/lib/lz4.c)
# Built as C++, which it supports, as the project enables no other language.
set_source_files_properties(${lz4_SOURCE_DIR}/lib/lz4.c PROPERTIES LANGUAGE CXX)
target_include_directories(spot_lz4 SYSTEM PUBLIC ${lz4_SOURCE_DIR}/lib)
set_target_properties(spot_lz4 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(spot_objects PRIVATE spot_lz4)

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
        platform/sdl3/sdl3_memory.cpp
        platform/sdl3/sdl3_frame_capture.cpp
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

# NVRHI runs Vulkan, and later Direct3D 12, for the graphics backend that takes over from SDL_GPU on Windows and Linux;
# macOS keeps SDL_GPU, whose Metal NVRHI lacks. Both are built in, and the device picks one when it is made.
if(NOT APPLE)
    set(SPOT_GRAPHICS_NVRHI ON)
    set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
    set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
    set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
    set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
    set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
    set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nvrhi
        GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
        GIT_TAG 6b96fb03e07539f08327aea76c56d55f1de9d906
        SYSTEM
    )
    FetchContent_MakeAvailable(nvrhi)
    # nvrhi_vk uses nvrhi without linking it, so it comes first for linkers that read libraries in order.
    target_link_libraries(spot_objects PRIVATE nvrhi_vk nvrhi)
    # The backend makes the Vulkan instance and device NVRHI runs on, so it needs Vulkan's headers too.
    if(TARGET Vulkan::Headers)
        target_link_libraries(spot_objects PRIVATE Vulkan::Headers)
    else()
        target_link_libraries(spot_objects PRIVATE Vulkan-Headers)
    endif()
    target_compile_definitions(spot_objects PRIVATE SPOT_GRAPHICS_NVRHI)
    # Vulkan's Windows header brings in windows.h, whose min and max macros would break std::min and std::max.
    if(WIN32)
        target_compile_definitions(spot_objects PRIVATE NOMINMAX)
    endif()
    target_sources(spot_objects PRIVATE
        graphics/nvrhi/nvrhi_device.cpp
        graphics/nvrhi/nvrhi_internal.h
        graphics/nvrhi/nvrhi_render_context.cpp
        graphics/nvrhi/nvrhi_resources.cpp
        graphics/nvrhi/nvrhi_vulkan.cpp
        graphics/nvrhi/nvrhi_vulkan.h
    )
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
        physics/jolt/jolt_job_system.cpp
        physics/jolt/jolt_job_system.h
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

set(SPOT_PHYSICS_2D "BOX2D" CACHE STRING "2D physics backend")
set_property(CACHE SPOT_PHYSICS_2D PROPERTY STRINGS BOX2D)
if(SPOT_PHYSICS_2D STREQUAL "BOX2D")
    set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
    set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(BOX2D_VALIDATE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(box2d
        GIT_REPOSITORY https://github.com/erincatto/box2d.git
        GIT_TAG v3.1.1
        GIT_SHALLOW TRUE
        SYSTEM
    )
    FetchContent_MakeAvailable(box2d)
    target_sources(spot_objects PRIVATE physics/box2d/box2d_simulation.cpp)
    target_link_libraries(spot_objects PRIVATE box2d)
else()
    message(FATAL_ERROR "Unknown SPOT_PHYSICS_2D '${SPOT_PHYSICS_2D}'")
endif()

target_include_directories(spot_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Crashes are reported with the system's own tools: DbgHelp on Windows, signals and dladdr() elsewhere.
if(WIN32)
    target_sources(spot_objects PRIVATE core/crash_handler_windows.cpp)
    target_link_libraries(spot_objects PUBLIC dbghelp psapi)
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
if(APPLE)
    set_target_properties(spot_shared PROPERTIES MACOSX_RPATH ON BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(spot_shared PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
add_library(spot::shared ALIAS spot_shared)

# A CMake package for game modules, which link the shared library of this build: find_package(Spot CONFIG) with
# Spot_DIR set to package/<configuration> in the build directory.
set(SPOT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
include("${PROJECT_SOURCE_DIR}/cmake/spot_runtime_libraries.cmake")
spot_get_runtime_libraries(SPOT_GAME_RUNTIME_LIBRARIES)
configure_file("${PROJECT_SOURCE_DIR}/cmake/SpotConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/SpotConfig.cmake.in" @ONLY)
file(GENERATE
    OUTPUT "${PROJECT_BINARY_DIR}/package/$<CONFIG>/SpotConfig.cmake"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/SpotConfig.cmake.in"
)

# Importing assets is for the tools, never for games, which load what was imported.
add_subdirectory(import)
