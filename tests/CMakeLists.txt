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
    event_tests.cpp
    graphics_tests.cpp
    input_tests.cpp
    log_capture.h
    log_tests.cpp
    math_tests.cpp
    platform_tests.cpp
    shader_tests.cpp
    time_tests.cpp
    version_tests.cpp
    window_tests.cpp
)

target_link_libraries(spot_tests PRIVATE
    spot::spot
    Catch2::Catch2WithMain
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
    SPOT_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SPOT_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SPOT_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    SPOT_EXPECTED_VERSION_STRING="${PROJECT_VERSION}"
)

spot_set_warnings(spot_tests)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
# The dummy video driver lets window tests run on machines without a display, such as CI servers.
# The timeout turns a main loop that never quits into a failure instead of a hang.
catch_discover_tests(spot_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 30)

# GPU tests need a real display and GPU, so they live in their own executable without the dummy video driver.
# They are skipped on machines without a usable GPU.
add_executable(spot_gpu_tests
    gpu_tests.cpp
)

target_link_libraries(spot_gpu_tests PRIVATE
    spot::spot
    Catch2::Catch2WithMain
)

spot_add_shaders(spot_gpu_tests
    shaders/quad.vert.hlsl
    shaders/triangle.frag.hlsl
    shaders/triangle.vert.hlsl
)

spot_set_warnings(spot_gpu_tests)

catch_discover_tests(spot_gpu_tests PROPERTIES TIMEOUT 60)
