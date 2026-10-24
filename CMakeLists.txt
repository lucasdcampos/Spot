cmake_minimum_required(VERSION 3.25)

project(spot
    VERSION 0.1.0
    DESCRIPTION "Spot 2D/3D game engine"
    LANGUAGES CXX
)

option(SPOT_BUILD_SANDBOX "Build the sandbox application" ${PROJECT_IS_TOP_LEVEL})
option(SPOT_BUILD_EDITOR "Build the Spot editor" ${PROJECT_IS_TOP_LEVEL})
option(SPOT_BUILD_TESTS "Build the Spot test suite" ${PROJECT_IS_TOP_LEVEL})
option(SPOT_SCRIPTING "Build scripting components, bindings and runtime support" ON)
option(SPOT_ENABLE_LUAU "Build Luau scripting support" ON)

# The bundled programs and their integration tests run scripted projects. Library-only consumers can omit it.
if(NOT SPOT_SCRIPTING AND (SPOT_BUILD_EDITOR OR SPOT_BUILD_SANDBOX OR SPOT_BUILD_TESTS))
    message(FATAL_ERROR
        "The bundled applications and tests require SPOT_SCRIPTING=ON. "
        "For a library-only build, set SPOT_BUILD_EDITOR, SPOT_BUILD_SANDBOX and SPOT_BUILD_TESTS to OFF."
    )
endif()

# Set globally so fetched dependencies are built with the same standard as the engine;
# Catch2 built as C++14 omits its std::string_view support and the tests fail to link.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(spot_set_compile_options target)
    if(MSVC)
        # Without /utf-8, MSVC reads sources in the system code page and garbles non-ASCII text.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Code built on the engine includes its header in every file, which is most of what compiling one costs, so targets
# made of many such files compile it once, with the other headers given.
function(spot_precompile_engine_header target)
    target_precompile_headers(${target} PRIVATE <spot.h> ${ARGN})
endfunction()

# The shared engine library links static dependencies, which must then work at any address too.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Programs that link shared libraries built here find them next to themselves, which is where Windows looks.
function(spot_copy_runtime_dlls target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()

include(cmake/spot_compiler_cache.cmake)
spot_use_compiler_cache()
if(SPOT_COMPILER_CACHE AND CMAKE_CXX_COMPILER_LAUNCHER)
    message(STATUS "Compiling through ${SPOT_COMPILER_CACHE}")
endif()

include(cmake/spot_shaders.cmake)
include(cmake/spot_embed_files.cmake)

add_subdirectory(engine)

# The tools and their command line, which the editor shares and the sandbox imports its assets with, and the player
# that the tools run and ship games without code of their own with.
if(SPOT_BUILD_SANDBOX OR SPOT_BUILD_EDITOR OR SPOT_BUILD_TESTS)
    add_subdirectory(player)
    add_subdirectory(tools)
    add_subdirectory(cli)
endif()

if(SPOT_BUILD_SANDBOX)
    add_subdirectory(sandbox)
    set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sandbox)
endif()

if(SPOT_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

if(SPOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
