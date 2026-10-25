# What the engine's CMake package gives games, wherever the engine is: in its build directory (SpotConfig.cmake.in) or
# installed (SpotInstalledConfig.cmake.in). The package sets, before including this:
#   SPOT_LIBRARY, SPOT_IMPORT_LIBRARY   the shared engine library, and on Windows its import library
#   SPOT_INCLUDE_DIR                    the engine's headers
#   SPOT_WITH_SCRIPTING, SPOT_WITH_LUAU whether the library was built with scripting, and with Luau
#   SPOT_SHADER_GLSLANG, SPOT_SHADER_SPIRV_CROSS, SPOT_DXC, SPOT_SHADER_INCLUDE_DIR   as spot_shader_function.cmake
#                                       expects them

if(NOT TARGET Spot::Shared)
    add_library(Spot::Shared SHARED IMPORTED)
    set_target_properties(Spot::Shared PROPERTIES
        IMPORTED_LOCATION "${SPOT_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SPOT_INCLUDE_DIR}"
        INTERFACE_COMPILE_FEATURES cxx_std_20
    )
    if(WIN32)
        set_target_properties(Spot::Shared PROPERTIES IMPORTED_IMPLIB "${SPOT_IMPORT_LIBRARY}")
    endif()
    if(SPOT_WITH_SCRIPTING)
        target_compile_definitions(Spot::Shared INTERFACE SPOT_SCRIPTING=1)
        if(SPOT_WITH_LUAU)
            target_compile_definitions(Spot::Shared INTERFACE SPOT_ENABLE_LUAU=1)
        endif()
    endif()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/spot_compiler_cache.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/spot_runtime_libraries.cmake")

# Gives every configuration the same output folder: generators add none to per-configuration directories.
function(spot_set_game_output target directory)
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${directory}"
        RUNTIME_OUTPUT_DIRECTORY "${directory}"
    )
    foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES CMAKE_BUILD_TYPE)
        string(TOUPPER "${config}" config)
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${config} "${directory}"
            RUNTIME_OUTPUT_DIRECTORY_${config} "${directory}"
        )
    endforeach()
endfunction()

# Builds the game's code, the sources given, twice over, from one compilation of them:
#   game          the game's executable, in the build directory's game folder, with the engine library beside it;
#                 its main() comes from the source named main.cpp, which nothing else has
#   game_module   the rest as a game module, in the build directory's module folder, which the editor loads so the
#                 game's components are in it, and reloads as the code changes
function(spot_add_game)
    spot_use_compiler_cache()
    set(main_sources ${ARGN})
    list(FILTER main_sources INCLUDE REGEX "(^|/)main\\.cpp$")
    set(code_sources ${ARGN})
    list(FILTER code_sources EXCLUDE REGEX "(^|/)main\\.cpp$")
    if(NOT main_sources)
        message(FATAL_ERROR "The game's code needs a main.cpp, with the main() of its executable")
    endif()

    add_library(game_code OBJECT ${code_sources})
    target_link_libraries(game_code PUBLIC Spot::Shared)
    set_target_properties(game_code PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Every file of game code includes the engine's header, which is most of what compiling one costs, so it is
    # compiled once, with the standard headers it brings.
    target_precompile_headers(game_code PRIVATE <spot.h>)
    # Game code overrides many functions it ignores the parameters of, such as OnUpdate()'s time, so those are no
    # warning.
    if(MSVC)
        target_compile_options(game_code PUBLIC /W4 /permissive- /utf-8 /wd4100)
    else()
        target_compile_options(game_code PUBLIC -Wall -Wextra -Wno-unused-parameter)
    endif()
    # dlclose() only unloads libraries without unique symbols, which GCC makes of some static variables.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(game_code PUBLIC -fno-gnu-unique)
    endif()

    add_library(game_module MODULE)
    target_link_libraries(game_module PRIVATE game_code)
    set_target_properties(game_module PROPERTIES PREFIX "" OUTPUT_NAME game)
    spot_set_game_output(game_module "${CMAKE_BINARY_DIR}/module")

    add_executable(game ${main_sources})
    target_link_libraries(game PRIVATE game_code)
    spot_set_game_output(game "${CMAKE_BINARY_DIR}/game")
    # The engine library goes beside the executable, where it is found when the game runs, built or shipped.
    if(APPLE)
        set(game_rpath "@loader_path")
    else()
        set(game_rpath "$ORIGIN")
    endif()
    set_target_properties(game PROPERTIES BUILD_RPATH "${game_rpath}" INSTALL_RPATH "${game_rpath}"
        BUILD_WITH_INSTALL_RPATH TRUE)
    add_custom_command(TARGET game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Spot::Shared>" "${CMAKE_BINARY_DIR}/game"
        VERBATIM
    )
    # Those of the compiler that builds the game, which an installed engine's build may not have had.
    spot_get_runtime_libraries(runtime_libraries)
    foreach(library IN LISTS runtime_libraries)
        add_custom_command(TARGET game POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${library}" "${CMAKE_BINARY_DIR}/game"
            VERBATIM
        )
    endforeach()
endfunction()

# Shaders of the game's own, compiled by the tools the engine's build made, and able to include the engine's shader
# headers, as surface_lighting.hlsl for surfaces lit like the engine's meshes (see Renderer3D::DrawSurface()).
set(SPOT_SHADER_TOOL_TARGETS "")
include("${CMAKE_CURRENT_LIST_DIR}/spot_shader_function.cmake")
