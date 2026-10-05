add_library(spot STATIC
    spot.h
    version.h
    version.cpp
    core/assert.h
    core/assert.cpp
    core/log.h
    core/log.cpp
    core/log_sinks.h
    core/log_sinks.cpp
    events/event.h
    input/input.h
    input/input.cpp
    input/input_state.h
    input/input_state.cpp
    input/keys.h
    input/keys.cpp
    platform/platform.h
    window/window.h
)
add_library(spot::spot ALIAS spot)

# The backend is chosen at build time. Its types never reach the public headers, so swapping it does not
# change the engine API.
set(SPOT_PLATFORM "SDL3" CACHE STRING "Windowing and input backend")
set_property(CACHE SPOT_PLATFORM PROPERTY STRINGS SDL3)

if(SPOT_PLATFORM STREQUAL "SDL3")
    include(FetchContent)

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
