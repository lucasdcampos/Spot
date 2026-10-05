add_library(spot STATIC
    spot.h
    version.h
    version.cpp
    core/application.h
    core/application.cpp
    core/assert.h
    core/assert.cpp
    core/log.h
    core/log.cpp
    core/log_sinks.h
    core/log_sinks.cpp
    core/time.h
    core/time.cpp
    events/event.h
    graphics/color.h
    graphics/graphics_device.h
    graphics/render_context.h
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
    window/window.h
)
add_library(spot::spot ALIAS spot)

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
        graphics/sdl_gpu/sdl_gpu_context.h
        graphics/sdl_gpu/sdl_gpu_device.cpp
        graphics/sdl_gpu/sdl_gpu_render_context.cpp
    )
else()
    message(FATAL_ERROR "Unknown SPOT_GRAPHICS '${SPOT_GRAPHICS}'")
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

spot_set_warnings(spot)
