# The engine's code is compiled once, then linked both as a static library, for games made only in code, and as a
# shared one, for the editor and the game modules it loads, so they share one copy of the engine's state: its
# registered components, logs, input and assets.
add_library(spot_objects OBJECT
    spot.h
    spot/core.h
    spot/platform.h
    spot/assets.h
    spot/graphics.h
    spot/render.h
    spot/audio.h
    spot/physics.h
    spot/animation.h
    spot/particles.h
    spot/scene.h
    spot/runtime.h
    spot/modules.h
    spot/scripting.h
    spot/assets/asset_loader.h
    spot/assets/alpha_mode.h
    spot/graphics/texture_asset_format.h
    spot/graphics/texture_asset_format.cpp
    spot/animation/animation_asset_format.h
    spot/graphics/graphics_assets.h
    spot/graphics/graphics_assets.cpp
    spot/render/render_assets.h
    spot/render/render_assets.cpp
    spot/audio/audio_assets.h
    spot/audio/audio_assets.cpp
    spot/animation/animation_assets.h
    spot/animation/animation_assets.cpp
    spot/assets/script_assets.h
    spot/assets/script_assets.cpp

    version.h
    version.cpp
    spot/assets/asset_file.h
    spot/assets/asset_file.cpp
    spot/assets/primitive_meshes.h
    spot/assets/primitive_meshes.cpp
    spot/assets/asset_formats.h
    spot/assets/asset_formats.cpp
    spot/core/ids/asset_id.h
    spot/core/ids/asset_id.cpp
    spot/core/io/file_io.h
    spot/core/io/file_io.cpp
    spot/core/jobs/job_system.h
    spot/core/jobs/job_system.cpp
    spot/core/memory/memory.h
    spot/core/memory/memory.cpp
    spot/assets/asset_manifest.h
    spot/assets/asset_pak.h
    spot/assets/asset_manifest.cpp
    spot/assets/asset_pak.cpp
    spot/assets/asset_manager.h
    spot/assets/asset_manager.cpp
    spot/core/ids/asset_ref.h
    spot/core/ids/asset_type.h
    spot/core/ids/entity_id.h
    spot/assets/asset_archive.h
    spot/assets/asset_type.h
    spot/assets/asset_type.cpp
    spot/assets/script_source.h
    spot/assets/built_in_assets.h
    spot/assets/assets.h
    spot/assets/assets.cpp
    spot/assets/material_source.h
    spot/assets/material_source.cpp
    spot/audio/audio_device.h
    spot/audio/audio_device.cpp
    spot/audio/audio_output.h
    spot/audio/mixer.h
    spot/audio/mixer.cpp
    spot/audio/playback.h
    spot/audio/sound.h
    spot/audio/sound.cpp
    spot/audio/audio_settings.h
    spot/audio/audio_settings.cpp
    spot/scene/audio/audio_source.h
    spot/scene/audio/audio_source.cpp
    spot/scene/audio/audio_system.h
    spot/scene/audio/audio_system.cpp
    spot/audio/sound_decoder.h
    spot/audio/sound_stream.h
    spot/audio/sound_stream.cpp
    spot/runtime/application.h
    spot/runtime/application.cpp
    spot/runtime/console.cpp
    spot/runtime/console_input.cpp
    spot/runtime/runtime_stats.cpp
    spot/runtime/profile_stream.cpp
    spot/runtime/engine_console.cpp
    spot/runtime/dev_tools_module.h
    spot/runtime/dev_tools_module.cpp
    spot/core/debug/assert.h
    spot/core/debug/assert.cpp
    spot/platform/crash_handler.h
    spot/platform/crash_handler.cpp
    spot/platform/crash_handler_internal.h
    spot/core/debug/diagnostics.h
    spot/core/debug/diagnostics.cpp
    spot/runtime/game_module.h
    spot/runtime/game_module.cpp
    spot/runtime/game_library.h
    spot/runtime/game_library.cpp
    spot/runtime/game_settings.h
    spot/runtime/module.h
    spot/runtime/game_settings.cpp
    spot/core/debug/log.h
    spot/core/memory/spsc_queue.h
    spot/core/debug/log.cpp
    spot/core/debug/log_sinks.h
    spot/core/debug/log_sinks.cpp
    spot/core/io/paths.h
    spot/core/io/paths.cpp
    spot/platform/process.h
    spot/runtime/save_data.h
    spot/runtime/save_data.cpp
    spot/core/debug/profiler.h
    spot/core/debug/profiler.cpp
    spot/platform/shared_library.h
    spot/scene/signal.h
    spot/scene/task.h
    spot/scene/task.cpp
    spot/core/time/time.h
    spot/core/time/time.cpp
    spot/scene/timer.h
    spot/scene/timer.cpp
    spot/scene/tween.h
    spot/scene/tween.cpp
    spot/core/io/utf8.h
    spot/core/io/utf8.cpp
    spot/platform/event.h
    spot/render/atmosphere.h
    spot/render/atmosphere.cpp
    spot/graphics/buffer.h
    spot/graphics/compute.h
    spot/render/custom_surface.h
    spot/render/camera_2d.h
    spot/render/bloom.cpp
    spot/render/bloom.h
    spot/render/camera_2d.cpp
    spot/render/pixel_perfect.cpp
    spot/render/camera_3d.h
    spot/render/camera_3d.cpp
    spot/core/math/color.h
    spot/render/debug_draw.h
    spot/render/debug_draw.cpp
    spot/render/font.h
    spot/render/fog_data.h
    spot/render/font.cpp
    spot/render/font_atlas.h
    spot/render/fullscreen_pass.cpp
    spot/render/fxaa.cpp
    spot/render/fxaa.h
    spot/render/fullscreen_pass.h
    spot/render/font_atlas.cpp
    spot/graphics/graphics_backend.h
    spot/graphics/graphics_device.h
    spot/graphics/graphics_device.cpp
    spot/graphics/image.h
    spot/graphics/image.cpp
    spot/render/light_clusters.cpp
    spot/render/light_clusters.h
    spot/render/light_data.h
    spot/render/line_renderer.h
    spot/render/line_renderer.cpp
    spot/render/material.h
    spot/render/material.cpp
    spot/render/mesh.h
    spot/render/mesh.cpp
    spot/core/math/bounds.h
    spot/graphics/mesh_data.h
    spot/graphics/mikktspace.cpp
    spot/graphics/mesh_data.cpp
    spot/scene/animation/skinned_mesh_renderer.h
    spot/scene/animation/skinned_mesh_renderer.cpp
    spot/animation/animator_controller.h
    spot/animation/animator_controller.cpp
    spot/animation/blend_tree.h
    spot/animation/blend_tree.cpp
    spot/scene/animation/animator.h
    spot/scene/animation/animator.cpp
    spot/animation/animation_motion.h
    spot/animation/animation_motion.cpp
    spot/scene/animation/animator_motion.cpp
    spot/animation/animation_layer.h
    spot/animation/animation_layer.cpp
    spot/scene/animation/animator_layers.cpp
    spot/scene/animation/bone_attachment.h
    spot/scene/animation/bone_attachment.cpp
    spot/scene/animation/ragdoll.h
    spot/scene/animation/ragdoll.cpp
    spot/animation/pose.h
    spot/animation/pose.cpp
    spot/animation/animation_data.h
    spot/animation/animation_data.cpp
    spot/animation/animation_formats.cpp
    spot/render/model_data.h
    spot/render/model.h
    spot/render/model.cpp
    spot/render/model_data.cpp
    spot/graphics/pipeline.h
    spot/graphics/pipeline.cpp
    spot/render/quad_batch.h
    spot/render/quad_batch.cpp
    spot/render/sprite_atlas.cpp
    spot/scene/rendering/sprite_animator.cpp
    spot/scene/rendering/tilemap.cpp
    spot/scene/physics2d/tilemap_collider_2d.cpp
    spot/graphics/render_context.h
    spot/render/render_stage.h
    spot/render/renderer_2d.h
    spot/render/renderer_2d.cpp
    spot/runtime/console_overlay.cpp
    spot/graphics/render_graph.h
    spot/render/render_world.h
    spot/render/renderer_3d.h
    spot/scene/rendering/rendering_settings.cpp
    spot/scene/rendering/rendering_settings.h
    spot/graphics/render_graph.cpp
    spot/render/render_world.cpp
    spot/render/renderer_3d.cpp
    spot/render/renderer_3d_internal.h
    spot/render/renderer_3d_meshes.h
    spot/render/renderer_3d_meshes.cpp
    spot/render/renderer_3d_shadows.h
    spot/render/renderer_3d_shadows.cpp
    spot/render/renderer_3d_sky.h
    spot/render/renderer_3d_sky.cpp
    spot/render/renderer_3d_worlds.h
    spot/render/renderer_3d_worlds.cpp
    spot/render/particle_batch.h
    spot/render/particle_batch.cpp
    spot/render/particle_renderer.h
    spot/render/particle_renderer.cpp
    spot/graphics/screenshot.cpp
    spot/graphics/shader.h
    spot/render/shadow_views.h
    spot/render/shadow_views.cpp
    spot/render/sky_data.h
    spot/render/text_layout.h
    spot/render/text_layout.cpp
    spot/graphics/texture.h
    spot/graphics/texture_table.h
    spot/render/tonemapper.cpp
    spot/render/tonemapper.h
    spot/platform/input.h
    spot/platform/input.cpp
    spot/platform/input_action.h
    spot/platform/input_action.cpp
    spot/platform/input_settings.h
    spot/platform/input_settings.cpp
    spot/platform/input_state.h
    spot/platform/input_state.cpp
    spot/platform/keys.h
    spot/platform/keys.cpp
    spot/particles/particle_settings.h
    spot/particles/particle_settings.cpp
    spot/particles/particle_simulation.h
    spot/particles/particle_simulation.cpp
    spot/scene/particles/particle_system.h
    spot/scene/particles/particle_system.cpp
    spot/core/math/curve.h
    spot/core/math/curve.cpp
    spot/core/math/gradient.h
    spot/core/math/gradient.cpp
    spot/core/math/angles.h
    spot/core/math/glm_conversions.h
    spot/core/math/matrix.h
    spot/core/math/matrix.cpp
    spot/core/math/quaternion.h
    spot/core/math/quaternion.cpp
    spot/core/math/vector.h
    spot/scene/physics/character_controller.h
    spot/scene/physics/character_controller.cpp
    spot/scene/physics/collider.h
    spot/scene/physics/collider.cpp
    spot/scene/physics/collision.h
    spot/scene/physics/joint.h
    spot/scene/physics/joint.cpp
    spot/scene/physics/physics_debug.h
    spot/scene/physics/physics_debug.cpp
    spot/scene/physics/physics_query.h
    spot/physics/physics_3d.h
    spot/scene/physics/physics_settings.h
    spot/scene/physics/physics_settings.cpp
    spot/scene/physics/physics_world.h
    spot/scene/physics/physics_world.cpp
    spot/scene/physics/physics_world_contacts.cpp
    spot/scene/physics/physics_world_joints.cpp
    spot/scene/physics/physics_world_queries.cpp
    spot/scene/physics/rigidbody.h
    spot/scene/physics/rigidbody.cpp
    spot/physics/physics_2d.h
    spot/scene/physics2d/physics_world_2d.cpp
    spot/scene/physics2d/collider_2d.cpp
    spot/scene/physics2d/rigidbody_2d.cpp
    spot/platform/platform.h
    spot/core/io/archive.h
    spot/core/io/archive.cpp
    spot/core/io/archive_state.h
    spot/scene/rendering/camera.h
    spot/scene/rendering/camera.cpp
    spot/scene/component.h
    spot/scene/component_ref.h
    spot/scene/component.cpp
    spot/scene/component_type.h
    spot/scene/component_requirements.h
    spot/scene/component_type.cpp
    spot/scene/component_registry.h
    spot/scene/component_serialization.h
    spot/scene/animation/animation_components.h
    spot/scene/animation/animation_components.cpp
    spot/scene/audio/audio_components.h
    spot/scene/audio/audio_components.cpp
    spot/scene/particles/particles_components.h
    spot/scene/particles/particles_components.cpp
    spot/scene/physics/physics_components.h
    spot/scene/physics/physics_components.cpp
    spot/scene/physics2d/physics2d_components.h
    spot/scene/physics2d/physics2d_components.cpp
    spot/scene/rendering/rendering_components.h
    spot/scene/rendering/rendering_components.cpp
    spot/scene/ui/ui_components.h
    spot/scene/ui/ui_components.cpp
    spot/scene/animation/animation_module.h
    spot/scene/animation/animation_module.cpp
    spot/scene/audio/audio_module.h
    spot/scene/audio/audio_module.cpp
    spot/scene/particles/particles_module.h
    spot/scene/particles/particles_module.cpp
    spot/scene/physics/physics_module.h
    spot/scene/physics/physics_module.cpp
    spot/scene/physics2d/physics2d_module.h
    spot/scene/physics2d/physics2d_module.cpp
    spot/scene/rendering/render_module.h
    spot/scene/rendering/render_module.cpp
    spot/scene/ui/ui_module.h
    spot/scene/ui/ui_module.cpp
    spot/scene/component_registry.cpp
    spot/scene/entity.h
    spot/scene/entity.cpp
    spot/scene/rendering/light.h
    spot/scene/rendering/fog.cpp
    spot/scene/rendering/fog.h
    spot/scene/rendering/light.cpp
    spot/scene/rendering/post_processing.cpp
    spot/scene/rendering/post_processing.h
    spot/scene/prefab.h
    spot/scene/prefab_document.h
    spot/scene/prefab.cpp
    spot/scene/prefab_serialization.cpp
    spot/scene/rendering/mesh_renderer.h
    spot/scene/rendering/surface_renderer.h
    spot/scene/rendering/render_world_system.h
    spot/scene/missing_component.h
    spot/scene/rendering/mesh_renderer.cpp
    spot/scene/rendering/render_world_system.cpp
    spot/scene/scene.h
    spot/scene/scene.cpp
    spot/scene/rendering/scene_model.cpp
    spot/scene/scene_prefab.cpp
    spot/scene/scene_assets.h
    spot/scene/rendering/scene_drawing.h
    spot/scene/rendering/scene_drawing.cpp
    spot/scene/rendering/scene_renderer.h
    spot/scene/rendering/render_hook_system.cpp
    spot/scene/rendering/scene_renderer.cpp
    spot/scene/scene_assets.cpp
    spot/scene/scene_serialization.cpp
    spot/scene/scene_serializer.h
    spot/scene/scene_system.h
    spot/scene/scene_system.cpp
    spot/scene/rendering/sky.h
    spot/scene/rendering/sky.cpp
    spot/scene/rendering/clouds.h
    spot/scene/rendering/clouds.cpp
    spot/scene/rendering/sprite_renderer.h
    spot/scene/rendering/sprite_renderer.cpp
    spot/scene/ui/ui_text_layout.cpp
    spot/scene/ui/ui_widgets.cpp
    spot/scene/ui/ui_layout_group.cpp
    spot/scene/ui/ui_input_system.cpp
    spot/scene/ui/ui_layout.cpp
    spot/scene/ui/ui_system.cpp
    spot/scene/rendering/text_renderer.cpp
    spot/scene/rendering/text_renderer.h
    spot/scene/transform.h
    spot/scene/transform.cpp
    spot/scene/tween_system.h
    spot/scene/tween_system.cpp
    spot/platform/clipboard.h
    spot/platform/file_dialog.h
    spot/platform/shell.cpp
    spot/platform/shell.h
    spot/platform/window.h
)

spot_add_shaders(spot_objects NAMESPACE Spot::Detail::Shaders
    spot/render/shaders/particle.vert.hlsl
    spot/render/shaders/particle.frag.hlsl
    spot/render/shaders/bloom_down.frag.hlsl
    spot/render/shaders/bloom_up.frag.hlsl
    spot/render/shaders/fullscreen.vert.hlsl
    spot/render/shaders/mip_downsample.frag.hlsl
    spot/render/shaders/mip_downsample_layer.frag.hlsl
    spot/render/shaders/fxaa.frag.hlsl
    spot/render/shaders/hiz_reduce.frag.hlsl
    spot/render/shaders/world_cull.comp.hlsl
    spot/render/shaders/line.frag.hlsl
    spot/render/shaders/line.vert.hlsl
    spot/render/shaders/mesh.frag.hlsl
    spot/render/shaders/skinned_mesh.vert.hlsl
    spot/render/shaders/skinned_shadow.vert.hlsl
    spot/render/shaders/mesh.vert.hlsl
    spot/render/shaders/shadow.frag.hlsl
    spot/render/shaders/shadow.vert.hlsl
    spot/render/shaders/shadow_cutout.frag.hlsl
    spot/render/shaders/sky.frag.hlsl
    spot/render/shaders/sky.vert.hlsl
    spot/render/shaders/sky_irradiance.frag.hlsl
    spot/render/shaders/sky_prefilter.frag.hlsl
    spot/render/shaders/sprite.frag.hlsl
    spot/render/shaders/sprite.vert.hlsl
    spot/render/shaders/tonemap.frag.hlsl
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
# MikkTSpace, the tangent space glTF's normal maps are baked in; spot/graphics/mikktspace.cpp compiles it.
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
        spot/platform/sdl3/sdl3_clipboard.cpp
        spot/platform/sdl3/sdl3_events.cpp
        spot/platform/sdl3/sdl3_file_dialog.cpp
        spot/platform/sdl3/sdl3_mouse_capture.h
        spot/platform/sdl3/sdl3_paths.cpp
        spot/platform/sdl3/sdl3_platform.cpp
        spot/platform/sdl3/sdl3_process.cpp
        spot/platform/sdl3/sdl3_memory.cpp
        spot/platform/sdl3/sdl3_frame_capture.cpp
        spot/platform/sdl3/sdl3_shared_library.cpp
        spot/platform/sdl3/sdl3_shell.cpp
        spot/platform/sdl3/sdl3_window.cpp
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
        spot/graphics/sdl_gpu/sdl_gpu_internal.h
        spot/graphics/sdl_gpu/sdl_gpu_device.cpp
        spot/graphics/sdl_gpu/sdl_gpu_render_context.cpp
        spot/graphics/sdl_gpu/sdl_gpu_resources.cpp
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
        spot/graphics/nvrhi/nvrhi_device.cpp
        spot/graphics/nvrhi/nvrhi_internal.h
        spot/graphics/nvrhi/nvrhi_render_context.cpp
        spot/graphics/nvrhi/nvrhi_resources.cpp
        spot/graphics/nvrhi/nvrhi_vulkan.cpp
        spot/graphics/nvrhi/nvrhi_vulkan.h
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
        spot/audio/sdl3/sdl3_audio_output.cpp
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
        spot/physics/jolt/jolt_characters.cpp
        spot/physics/jolt/jolt_common.h
        spot/physics/jolt/jolt_contacts.cpp
        spot/physics/jolt/jolt_contacts.h
        spot/physics/jolt/jolt_job_system.cpp
        spot/physics/jolt/jolt_job_system.h
        spot/physics/jolt/jolt_joints.cpp
        spot/physics/jolt/jolt_layers.h
        spot/physics/jolt/jolt_physics_3d.cpp
        spot/physics/jolt/jolt_queries.cpp
        spot/physics/jolt/jolt_runtime.cpp
        spot/physics/jolt/jolt_runtime.h
        spot/physics/jolt/jolt_shapes.cpp
        spot/physics/jolt/jolt_shapes.h
        spot/physics/jolt/jolt_simulation_impl.h
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
    target_sources(spot_objects PRIVATE spot/physics/box2d/box2d_simulation.cpp)
    target_link_libraries(spot_objects PRIVATE box2d)
else()
    message(FATAL_ERROR "Unknown SPOT_PHYSICS_2D '${SPOT_PHYSICS_2D}'")
endif()

target_include_directories(spot_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Crashes are reported with the system's own tools: DbgHelp on Windows, signals and dladdr() elsewhere.
if(WIN32)
    target_sources(spot_objects PRIVATE spot/platform/crash_handler_windows.cpp)
    target_link_libraries(spot_objects PUBLIC dbghelp psapi)
else()
    target_sources(spot_objects PRIVATE spot/platform/crash_handler_posix.cpp)
    target_link_libraries(spot_objects PUBLIC ${CMAKE_DL_LIBS})
endif()

if(SPOT_SCRIPTING)
    target_compile_definitions(spot_objects PUBLIC SPOT_SCRIPTING=1)
    target_sources(spot_objects PRIVATE
        spot/scene/scripting/scripting_components.h
        spot/scene/scripting/scripting_components.cpp
        spot/scene/scripting/scripting_module.h
        spot/scene/scripting/scripting_module.cpp
        spot/scene/scripting/script_bindings.h
        spot/scene/scripting/script_api.h
        spot/scene/scripting/script_bindings.cpp
        spot/scene/scripting/script_runtime.h
        spot/scene/scripting/script_runtime.cpp
        spot/scene/scripting/script_scene.h
        spot/scene/scripting/script_events.h
        spot/scene/scripting/script_scene.cpp
        spot/scene/scripting/script_module_bindings.h
        spot/scene/scripting/script_audio.cpp
        spot/scene/scripting/script_render.cpp
        spot/scene/scripting/script_input.cpp
        spot/scene/scripting/script_time.cpp
        spot/scene/scripting/script_physics.cpp
        spot/scene/scripting/script_component.h
        spot/scene/scripting/script_component.cpp
    )
    if(SPOT_ENABLE_LUAU)
        # Only Luau's parser, compiler and VM are built, as their targets are linked: scripts are compiled as they load,
        # and nothing needs the type checker or native code generation. Luau reports errors with C++ exceptions.
        set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
        set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(luau
            GIT_REPOSITORY https://github.com/luau-lang/luau.git
            GIT_TAG 0.743
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR none
        )
        FetchContent_MakeAvailable(luau)
        add_subdirectory(${luau_SOURCE_DIR} ${luau_BINARY_DIR} EXCLUDE_FROM_ALL SYSTEM)
        foreach(target Luau.Common Luau.Ast Luau.Bytecode Luau.Compiler Luau.VM)
            target_compile_options(${target} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>)
        endforeach()

        target_sources(spot_objects PRIVATE
            spot/scene/scripting/luau_runtime.h
            spot/scene/scripting/luau_runtime.cpp
        )
        target_link_libraries(spot_objects PRIVATE Luau.Compiler Luau.VM)
        target_compile_definitions(spot_objects PUBLIC SPOT_ENABLE_LUAU=1)
    endif()
endif()

target_compile_features(spot_objects PUBLIC cxx_std_20)

target_compile_definitions(spot_objects PRIVATE
    SPOT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SPOT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SPOT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    SPOT_VERSION_STRING="${SPOT_VERSION}"
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

if(SPOT_SCRIPTING)
    target_compile_definitions(spot_shared PUBLIC SPOT_SCRIPTING=1)
    if(SPOT_ENABLE_LUAU)
        target_compile_definitions(spot_shared PUBLIC SPOT_ENABLE_LUAU=1)
    endif()
endif()

# A CMake package for game modules, which link the shared library of this build: find_package(Spot CONFIG) with
# Spot_DIR set to package/<configuration> in the build directory.
set(SPOT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
configure_file("${PROJECT_SOURCE_DIR}/cmake/SpotConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/SpotConfig.cmake.in" @ONLY)
file(GENERATE
    OUTPUT "${PROJECT_BINARY_DIR}/package/$<CONFIG>/SpotConfig.cmake"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/SpotConfig.cmake.in"
)

# Importing assets is for the tools, never for games, which load what was imported.
add_subdirectory(spot/import)
