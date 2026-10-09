# The sandbox is a project like any other (Sandbox.spot), which the editor opens and builds by src/CMakeLists.txt.
# The engine's own build builds its code here too, into an executable that links the static engine library, so the
# engine is tried out, and debugged, with a game.
file(GLOB_RECURSE sandbox_sources CONFIGURE_DEPENDS src/*.cpp src/*.h)

add_executable(sandbox ${sandbox_sources})
target_link_libraries(sandbox PRIVATE spot::spot)
target_include_directories(sandbox PRIVATE src)
spot_set_compile_options(sandbox)

# The game runs from its imported assets and settings beside the executable, as a shipped game does. The tools
# import what changed every build, so assets edited in the editor or not need no other step.
add_custom_target(sandbox_assets ALL
    COMMAND spot_cli cook "${CMAKE_CURRENT_SOURCE_DIR}/Sandbox.spot" --out "$<TARGET_FILE_DIR:sandbox>"
    COMMENT "Importing the sandbox's assets"
    VERBATIM
)
add_dependencies(sandbox sandbox_assets)

# Writes the sandbox's generated assets, which are committed, so it only runs when asked to.
add_subdirectory(generator)
