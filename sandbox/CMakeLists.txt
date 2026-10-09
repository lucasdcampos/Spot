# The sandbox is also a project the editor opens (Sandbox.spot). Its code in src/ is built into the executable here,
# and into the game module the editor and the player load by src/CMakeLists.txt.
file(GLOB_RECURSE sandbox_game_sources CONFIGURE_DEPENDS src/*.cpp src/*.h)

add_executable(sandbox
    main.cpp
    ${sandbox_game_sources}
)

target_link_libraries(sandbox PRIVATE
    spot::tools
    spot::import
    spot::spot
)

target_include_directories(sandbox PRIVATE src)

# The sandbox runs the project from the repository, importing its assets at startup, so editing them, in the editor
# or not, needs no rebuild.
target_compile_definitions(sandbox PRIVATE
    SPOT_SANDBOX_PROJECT="${CMAKE_CURRENT_SOURCE_DIR}/Sandbox.spot"
)

spot_set_compile_options(sandbox)
