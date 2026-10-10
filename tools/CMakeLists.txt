# The tools that make games out of projects, which the editor and the command line share. Games never link them.
#
# They are built against the engine's headers only: whatever links them also links one of the engine's libraries,
# spot::spot or spot::shared, so a program never holds two copies of the engine. Programs list spot::tools, then
# spot::import, then that library, as linkers that read libraries in order, such as GNU ld, only find what a library
# uses in the libraries after it.
add_library(spot_tools STATIC
    build/build_info.cpp
    build/code_build.cpp
    build/code_build.h
    build/game_build.cpp
    build/game_build.h
    project/project.cpp
    project/project.h
    project/project_templates.cpp
    project/project_templates.h
    run/game_runner.cpp
    run/game_runner.h
    spot_tools.h
    tools_log.cpp
    tools_log.h
)
add_library(spot::tools ALIAS spot_tools)

target_include_directories(spot_tools PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<TARGET_PROPERTY:spot_objects,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_features(spot_tools PUBLIC cxx_std_20)
target_link_libraries(spot_tools PUBLIC spot::import PRIVATE nlohmann_json::nlohmann_json)

spot_set_compile_options(spot_tools)
spot_precompile_engine_header(spot_tools)

# Games are built as the tools were, against their build of the shared engine library, so they work together.
get_property(spot_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
set_property(SOURCE build/build_info.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    SPOT_TOOLCHAIN_CMAKE="${CMAKE_COMMAND}"
    SPOT_TOOLCHAIN_GENERATOR="${CMAKE_GENERATOR}"
    SPOT_TOOLCHAIN_PLATFORM="${CMAKE_GENERATOR_PLATFORM}"
    SPOT_TOOLCHAIN_TOOLSET="${CMAKE_GENERATOR_TOOLSET}"
    SPOT_TOOLCHAIN_INSTANCE="${CMAKE_GENERATOR_INSTANCE}"
    SPOT_TOOLCHAIN_COMPILER="${CMAKE_CXX_COMPILER}"
    SPOT_TOOLCHAIN_MAKE_PROGRAM="${CMAKE_MAKE_PROGRAM}"
    SPOT_TOOLCHAIN_CONFIGURATION="$<CONFIG>"
    SPOT_TOOLCHAIN_MULTI_CONFIGURATION=$<BOOL:${spot_multi_config}>
    SPOT_TOOLCHAIN_PACKAGE="${PROJECT_BINARY_DIR}/package/$<CONFIG>"
    SPOT_TOOLCHAIN_ENGINE_LIBRARY="$<TARGET_FILE:spot_shared>"
)
# What the tools build games against comes with them.
add_dependencies(spot_tools spot_shared)
