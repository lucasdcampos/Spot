include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.0
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(Catch2)

add_executable(spot_tests
    application_tests.cpp
    character_controller_tests.cpp
    asset_tests.cpp
    audio_tests.cpp
    audio_scene_tests.cpp
    cli_tests.cpp
    component_fault_tests.cpp
    debug_draw_tests.cpp
    crash_tests.cpp
    event_tests.cpp
    game_code_tests.cpp
    graphics_tests.cpp
    image_tests.cpp
    input_tests.cpp
    input_action_tests.cpp
    log_capture.h
    light_tests.cpp
    log_tests.cpp
    math_tests.cpp
    physics_debug_tests.cpp
    physics_event_tests.cpp
    physics_joint_tests.cpp
    physics_query_tests.cpp
    physics_tests.cpp
    model_data_tests.cpp
    model_instance_tests.cpp
    platform_tests.cpp
    profiler_tests.cpp
    prefab_tests.cpp
    prefab_override_tests.cpp
    prefab_nesting_tests.cpp
    project_tests.cpp
    renderer_2d_tests.cpp
    renderer_3d_tests.cpp
    scene_serialization_tests.cpp
    scene_system_tests.cpp
    scene_tests.cpp
    shader_tests.cpp
    sky_tests.cpp
    temp_directory.h
    text_tests.cpp
    time_tests.cpp
    version_tests.cpp
    window_tests.cpp
)

target_link_libraries(spot_tests PRIVATE
    spot::tools
    spot::import
    spot::spot
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
)

spot_add_shaders(spot_tests
    shaders/textured.frag.hlsl
    shaders/triangle.vert.hlsl
)
if(SPOT_DXC)
    target_compile_definitions(spot_tests PRIVATE SPOT_TEST_EXPECT_DXIL)
endif()

# Backend tests feed native events in, so they need the backend's headers; the library comes through spot.
if(SPOT_PLATFORM STREQUAL "SDL3")
    target_sources(spot_tests PRIVATE sdl3_event_tests.cpp)
    target_link_libraries(spot_tests PRIVATE SDL3::Headers)
endif()

target_compile_definitions(spot_tests PRIVATE
    SPOT_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    SPOT_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SPOT_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SPOT_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    SPOT_EXPECTED_VERSION_STRING="${PROJECT_VERSION}"
)

spot_set_compile_options(spot_tests)
spot_precompile_engine_header(spot_tests <catch2/catch_test_macros.hpp>)

# Crashes the way the crash tests ask, after installing the crash handler.
add_executable(spot_crash_app crash_app.cpp)
target_link_libraries(spot_crash_app PRIVATE spot::spot)
spot_set_compile_options(spot_crash_app)
target_compile_definitions(spot_tests PRIVATE SPOT_CRASH_APP="$<TARGET_FILE:spot_crash_app>")
add_dependencies(spot_tests spot_crash_app)

# The command line runs as a program of its own.
target_compile_definitions(spot_tests PRIVATE SPOT_CLI="$<TARGET_FILE:spot_cli>")
add_dependencies(spot_tests spot_cli)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
add_executable(spot_animation_tests animation_import_tests.cpp animation_pose_tests.cpp animation_runtime_tests.cpp animation_controller_tests.cpp animation_blend_tests.cpp animation_motion_tests.cpp animation_layer_tests.cpp bone_attachment_tests.cpp ragdoll_tests.cpp)
target_link_libraries(spot_animation_tests PRIVATE spot::import spot::spot Catch2::Catch2WithMain)
target_compile_definitions(spot_animation_tests PRIVATE SPOT_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
spot_set_compile_options(spot_animation_tests)
catch_discover_tests(spot_animation_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 30)

add_executable(spot_ui_tests ui_tests.cpp ui_widget_tests.cpp ui_layout_group_tests.cpp ui_input_tests.cpp ui_application_tests.cpp rendering_settings_tests.cpp ui_signal_tests.cpp)
target_compile_definitions(spot_ui_tests PRIVATE SPOT_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(spot_ui_tests PRIVATE spot::spot Catch2::Catch2WithMain)
spot_set_compile_options(spot_ui_tests)
catch_discover_tests(spot_ui_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 30)

add_executable(spot_particle_tests curve_tests.cpp particle_tests.cpp)
target_link_libraries(spot_particle_tests PRIVATE spot::spot Catch2::Catch2WithMain)
spot_set_compile_options(spot_particle_tests)
catch_discover_tests(spot_particle_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 30)

add_executable(spot_gameplay_tests gameplay_tests.cpp)
target_link_libraries(spot_gameplay_tests PRIVATE spot::spot Catch2::Catch2WithMain)
spot_set_compile_options(spot_gameplay_tests)
catch_discover_tests(spot_gameplay_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 30)
add_executable(spot_console_tests console_tests.cpp console_input_tests.cpp runtime_stats_tests.cpp profile_stream_tests.cpp frame_capture_tests.cpp)
target_link_libraries(spot_console_tests PRIVATE spot::spot Catch2::Catch2WithMain)
spot_set_compile_options(spot_console_tests)
catch_discover_tests(spot_console_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 30)
add_library(spot_capture_fixture SHARED frame_capture_fixture.cpp)
target_include_directories(spot_capture_fixture SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/renderdoc)
spot_set_compile_options(spot_capture_fixture)
target_compile_definitions(spot_console_tests PRIVATE SPOT_CAPTURE_FIXTURE="$<TARGET_FILE:spot_capture_fixture>")
add_dependencies(spot_console_tests spot_capture_fixture)
add_executable(spot_profile_process_tests profile_process_tests.cpp)
target_link_libraries(spot_profile_process_tests PRIVATE spot::tools spot::spot Catch2::Catch2WithMain)
target_compile_definitions(spot_profile_process_tests PRIVATE SPOT_CRASH_APP="$<TARGET_FILE:spot_crash_app>")
add_dependencies(spot_profile_process_tests spot_crash_app)
spot_set_compile_options(spot_profile_process_tests)
catch_discover_tests(spot_profile_process_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 30)
# The dummy video driver lets window tests run on machines without a display, such as CI servers.
# The timeout turns a main loop that never quits into a failure instead of a hang.
catch_discover_tests(spot_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 30)

# GPU tests need a real display and GPU, so they live in their own executable without the dummy video driver.
# They are skipped on machines without a usable GPU.
add_executable(spot_gpu_tests
    golden_tests.cpp
    gpu_tests.cpp
    rendering_application.h
)

target_link_libraries(spot_gpu_tests PRIVATE
    spot::import
    spot::spot
    Catch2::Catch2WithMain
)

spot_add_shaders(spot_gpu_tests
    shaders/depth_compare.frag.hlsl
    shaders/quad.vert.hlsl
    shaders/solid.frag.hlsl
    shaders/textured.frag.hlsl
    shaders/textured_quad.vert.hlsl
    shaders/triangle.frag.hlsl
    shaders/triangle.vert.hlsl
    shaders/uv.frag.hlsl
)

# glTF sample models for the golden-image tests, too large to keep in the repository: downloaded once, pinned to a
# commit and checked by hash. Without them, their tests are skipped.
option(SPOT_DOWNLOAD_SAMPLE_MODELS "Download glTF sample models for the golden-image tests" ON)
set(SPOT_SAMPLE_MODELS_DIR "${CMAKE_CURRENT_BINARY_DIR}/sample_models")
if(SPOT_DOWNLOAD_SAMPLE_MODELS)
    set(sample_models_url
        "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/edc7c9e67c639d230715049ee31f9a96a6babbbe/Models")
    foreach(model IN ITEMS
            "DamagedHelmet|a1e3b04de97b11de564ce6e53b95f02954a297f0008183ac63a4f5974f6b32d8"
            "AlphaBlendModeTest|37c3577d143071b42dd46e9d942b157837eb25c6340112171d7faecaa987b14e")
        string(REPLACE "|" ";" model "${model}")
        list(GET model 0 name)
        list(GET model 1 hash)
        set(path "${SPOT_SAMPLE_MODELS_DIR}/${name}.glb")
        if(NOT EXISTS "${path}")
            file(DOWNLOAD "${sample_models_url}/${name}/glTF-Binary/${name}.glb" "${path}"
                EXPECTED_HASH SHA256=${hash} STATUS status)
            list(GET status 0 code)
            if(NOT code EQUAL 0)
                file(REMOVE "${path}")
                message(WARNING "Could not download the glTF sample model ${name}; its golden-image test is skipped")
            endif()
        endif()
    endforeach()
endif()

target_compile_definitions(spot_gpu_tests PRIVATE
    SPOT_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    SPOT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    SPOT_GOLDEN_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/golden_output"
    SPOT_SAMPLE_MODELS_DIR="${SPOT_SAMPLE_MODELS_DIR}"
)

spot_set_compile_options(spot_gpu_tests)
spot_precompile_engine_header(spot_gpu_tests <catch2/catch_test_macros.hpp>)

# GPU processes compete for the same device and open windows; run each without other tests alongside it.
catch_discover_tests(spot_gpu_tests PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 60)

# What runs in the shared engine library, as the editor and game modules use it: loading modules, such as this one,
# built as projects with code build theirs, and running programs.
add_library(spot_test_module MODULE modules/test_module.cpp modules/test_component.cpp)
target_link_libraries(spot_test_module PRIVATE spot::shared)
spot_set_compile_options(spot_test_module)

add_executable(spot_module_tests module_tests.cpp)
target_link_libraries(spot_module_tests PRIVATE
    spot::shared
    Catch2::Catch2WithMain
)
target_compile_definitions(spot_module_tests PRIVATE
    SPOT_TEST_MODULE="$<TARGET_FILE:spot_test_module>"
    SPOT_CMAKE_COMMAND="${CMAKE_COMMAND}"
)
add_dependencies(spot_module_tests spot_test_module)
spot_set_compile_options(spot_module_tests)
spot_copy_runtime_dlls(spot_module_tests)
catch_discover_tests(spot_module_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 60)

# Editor tests link the editor's code, everything but its main().
if(TARGET spot_editor_core)
    add_executable(spot_editor_tools_tests editor/console_command_tests.cpp editor/profiler_model_tests.cpp editor/particle_preview_tests.cpp editor/animation_preview_tests.cpp editor/controller_graph_tests.cpp)
    target_link_libraries(spot_editor_tools_tests PRIVATE spot_editor_core Catch2::Catch2WithMain)
    target_include_directories(spot_editor_tools_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(spot_editor_tools_tests PRIVATE SPOT_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    spot_set_compile_options(spot_editor_tools_tests)
    spot_copy_runtime_dlls(spot_editor_tools_tests)
    catch_discover_tests(spot_editor_tools_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 30)
    add_executable(spot_editor_tests
        editor/asset_browser_tests.cpp
        editor/command_history_tests.cpp
        editor/console_log_tests.cpp
        editor/editor_layout_tests.cpp
        editor/headless_imgui.h
        editor/hierarchy_tests.cpp
        editor/imgui_platform_tests.cpp
        editor/inspector_tests.cpp
        editor/project_tests.cpp
        editor/play_session_tests.cpp
        editor/project_ui_tests.cpp
        editor/scene_commands_tests.cpp
        editor/scene_workspace_tests.cpp
        editor/transform_gizmo_tests.cpp
        editor/ui_editor_tests.cpp
        temp_directory.h
    )
    target_link_libraries(spot_editor_tests PRIVATE
        spot_editor_core
        Catch2::Catch2WithMain
    )
    target_include_directories(spot_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(spot_editor_tests PRIVATE SPOT_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    spot_set_compile_options(spot_editor_tests)
    spot_precompile_engine_header(spot_editor_tests <catch2/catch_test_macros.hpp>)
    # Before the tests are listed, which runs them.
    spot_copy_runtime_dlls(spot_editor_tests)
    catch_discover_tests(spot_editor_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 30)

    # Building projects' code with CMake takes a while, so those tests run on their own, with more time.
    add_executable(spot_editor_module_tests
        editor/module_host_tests.cpp
        temp_directory.h
    )
    target_link_libraries(spot_editor_module_tests PRIVATE
        spot_editor_core
        Catch2::Catch2WithMain
    )
    target_include_directories(spot_editor_module_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    spot_set_compile_options(spot_editor_module_tests)
    spot_precompile_engine_header(spot_editor_module_tests <catch2/catch_test_macros.hpp>)
    spot_copy_runtime_dlls(spot_editor_module_tests)
    catch_discover_tests(spot_editor_module_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 600)

    add_executable(spot_editor_gpu_tests
        editor/editor_gpu_tests.cpp
        rendering_application.h
        temp_directory.h
    )
    target_link_libraries(spot_editor_gpu_tests PRIVATE
        spot_editor_core
        Catch2::Catch2WithMain
    )
    target_include_directories(spot_editor_gpu_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(spot_editor_gpu_tests PRIVATE
        SPOT_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    spot_set_compile_options(spot_editor_gpu_tests)
    spot_precompile_engine_header(spot_editor_gpu_tests <catch2/catch_test_macros.hpp>)
    spot_copy_runtime_dlls(spot_editor_gpu_tests)
    catch_discover_tests(spot_editor_gpu_tests PROPERTIES LABELS gpu RUN_SERIAL TRUE TIMEOUT 60)
endif()

# The rendering benchmark, run by hand in Release when rendering changes; see its source.
add_executable(spot_benchmark benchmark/rendering_benchmark.cpp)
target_link_libraries(spot_benchmark PRIVATE spot::spot)
spot_set_compile_options(spot_benchmark)

# The sandbox's asset generator, when the sandbox is built.
if(TARGET sandbox_generator_core)
    target_sources(spot_tests PRIVATE sandbox_generator_tests.cpp)
    target_link_libraries(spot_tests PRIVATE sandbox_generator_core)
endif()

# The sandbox's game code on its real scenes, when the sandbox is built: the engine's integration test.
if(TARGET sandbox)
    file(GLOB sandbox_game_sources CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/sandbox/src/*.cpp)
    list(FILTER sandbox_game_sources EXCLUDE REGEX "/main[.]cpp$")
    add_executable(spot_sandbox_tests sandbox/sandbox_tests.cpp sandbox/interface_tests.cpp ${sandbox_game_sources})
    target_link_libraries(spot_sandbox_tests PRIVATE spot::import spot::spot Catch2::Catch2WithMain)
    target_include_directories(spot_sandbox_tests PRIVATE ${PROJECT_SOURCE_DIR}/sandbox/src ${CMAKE_CURRENT_SOURCE_DIR})
    # The assets the build cooks beside the sandbox, which the tests load as the game does.
    target_compile_definitions(spot_sandbox_tests PRIVATE
        SANDBOX_ASSETS_DIR="${PROJECT_SOURCE_DIR}/sandbox/assets"
        SANDBOX_IMPORTED_DIR="$<TARGET_FILE_DIR:sandbox>/imported")
    target_sources(spot_gpu_tests PRIVATE ${PROJECT_SOURCE_DIR}/sandbox/src/menu_thumbnails.cpp)
    target_compile_definitions(spot_gpu_tests PRIVATE SPOT_SANDBOX_IMPORTED_DIR="$<TARGET_FILE_DIR:sandbox>/imported")
    add_dependencies(spot_gpu_tests sandbox_assets)
    add_dependencies(spot_sandbox_tests sandbox_assets)
    spot_set_compile_options(spot_sandbox_tests)
    spot_precompile_engine_header(spot_sandbox_tests <catch2/catch_test_macros.hpp>)
    catch_discover_tests(spot_sandbox_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 60)
endif()
