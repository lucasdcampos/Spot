cmake_minimum_required(VERSION 3.25)

project(spot
    VERSION 0.1.0
    DESCRIPTION "Spot 2D/3D game engine"
    LANGUAGES CXX
)

option(SPOT_BUILD_SANDBOX "Build the sandbox application" ${PROJECT_IS_TOP_LEVEL})
option(SPOT_BUILD_EDITOR "Build the Spot editor" ${PROJECT_IS_TOP_LEVEL})
option(SPOT_BUILD_TESTS "Build the Spot test suite" ${PROJECT_IS_TOP_LEVEL})

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

include(cmake/spot_shaders.cmake)
include(cmake/spot_embed_files.cmake)

add_subdirectory(engine)

if(SPOT_BUILD_SANDBOX OR SPOT_BUILD_EDITOR OR SPOT_BUILD_TESTS)
    add_subdirectory(tools)
endif()

if(SPOT_BUILD_SANDBOX)
    add_subdirectory(sandbox)
    set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sandbox)
endif()

if(SPOT_BUILD_EDITOR)
    # The player runs what the editor makes, so it comes with it.
    add_subdirectory(player)
    add_subdirectory(editor)
endif()

if(SPOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
