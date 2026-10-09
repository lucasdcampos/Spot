# The tools that make games out of projects, which the editor and the command line share. Games never link them.
#
# They are built against the engine's headers only: whatever links them also links one of the engine's libraries,
# spot::spot or spot::shared, so a program never holds two copies of the engine. Programs list spot::tools, then
# spot::import, then that library, as linkers that read libraries in order, such as GNU ld, only find what a library
# uses in the libraries after it.
add_library(spot_tools STATIC
    project/project.cpp
    project/project.h
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
