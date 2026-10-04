cmake_minimum_required(VERSION 3.25)

project(spot
    VERSION 0.1.0
    DESCRIPTION "Spot 2D/3D game engine"
    LANGUAGES CXX
)

option(SPOT_BUILD_SANDBOX "Build the sandbox application" ${PROJECT_IS_TOP_LEVEL})
option(SPOT_BUILD_TESTS "Build the Spot test suite" ${PROJECT_IS_TOP_LEVEL})

# Set globally so fetched dependencies are built with the same standard as the engine;
# Catch2 built as C++14 omits its std::string_view support and the tests fail to link.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(spot_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_subdirectory(engine)

if(SPOT_BUILD_SANDBOX)
    add_subdirectory(sandbox)
    set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sandbox)
endif()

if(SPOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
