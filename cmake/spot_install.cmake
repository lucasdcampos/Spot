# The installed engine, which releases ship and the installers install: the tools and the editor, the player and what
# games' code builds against, laid out to work wherever it is put. The dependencies' own install rules are no part of
# it, so it is the spot component alone:
#   cmake --install <build> --config Release --component spot --prefix <folder>
#
#   bin/              spot, spot_editor and the shared engine library they run with
#   player/           spot_player with only what ships beside it, which exports copy (see Toolchain::player)
#   include/          the engine's headers, without its backends' or the importers'
#   lib/              the engine library's import library on Windows, and the engine's CMake package in cmake/Spot
#   libexec/spot/     the shader tools that spot_add_shaders() runs
#   share/spot/       the engine's shader headers
#   version.txt       the version, which the installers compare with the latest release's

if(APPLE)
    set(spot_install_rpath "@loader_path")
else()
    set(spot_install_rpath "$ORIGIN")
endif()
set(spot_tools spot_cli)
if(TARGET spot_editor)
    list(APPEND spot_tools spot_editor)
endif()
set_target_properties(${spot_tools} PROPERTIES INSTALL_RPATH "${spot_install_rpath}")

install(TARGETS ${spot_tools} RUNTIME DESTINATION bin COMPONENT spot)
install(TARGETS spot_shared
    RUNTIME DESTINATION bin COMPONENT spot
    LIBRARY DESTINATION bin COMPONENT spot
    ARCHIVE DESTINATION lib COMPONENT spot
)
if(TARGET spot_editor)
    install(FILES editor/resources/licenses/imgui_color_text_edit_license.txt DESTINATION bin COMPONENT spot)
endif()

install(TARGETS spot_player RUNTIME DESTINATION player COMPONENT spot)
install(FILES $<TARGET_FILE:spot_shared> DESTINATION player COMPONENT spot)
# By their names, which are links on Linux, to the files they name.
include("${CMAKE_CURRENT_LIST_DIR}/spot_runtime_libraries.cmake")
spot_get_runtime_libraries(spot_install_runtime_libraries)
foreach(library IN LISTS spot_install_runtime_libraries)
    get_filename_component(name "${library}" NAME)
    file(REAL_PATH "${library}" file)
    install(FILES "${file}" DESTINATION player RENAME "${name}" COMPONENT spot)
    # Windows installs no compiler runtime of its own, as Linux and macOS have theirs.
    if(WIN32)
        install(FILES "${file}" DESTINATION bin RENAME "${name}" COMPONENT spot)
    endif()
endforeach()

install(DIRECTORY engine/ DESTINATION include COMPONENT spot
    FILES_MATCHING PATTERN "*.h"
    PATTERN "spot_import.h" EXCLUDE
    PATTERN "import" EXCLUDE
    PATTERN "sdl3" EXCLUDE
    PATTERN "sdl_gpu" EXCLUDE
    PATTERN "nvrhi" EXCLUDE
    PATTERN "jolt" EXCLUDE
    PATTERN "box2d" EXCLUDE
)
install(DIRECTORY "${SPOT_SHADER_INCLUDE_DIR}/" DESTINATION share/spot/shaders COMPONENT spot)
install(PROGRAMS $<TARGET_FILE:glslang-standalone> $<TARGET_FILE:spirv-cross> DESTINATION libexec/spot COMPONENT spot)

configure_file(cmake/SpotInstalledConfig.cmake.in "${PROJECT_BINARY_DIR}/SpotInstalledConfig.cmake.in" @ONLY)
file(GENERATE
    OUTPUT "${PROJECT_BINARY_DIR}/installed_package/$<CONFIG>/SpotConfig.cmake"
    INPUT "${PROJECT_BINARY_DIR}/SpotInstalledConfig.cmake.in"
)
install(FILES
    "${PROJECT_BINARY_DIR}/installed_package/$<CONFIG>/SpotConfig.cmake"
    cmake/spot_game.cmake
    cmake/spot_compiler_cache.cmake
    cmake/spot_runtime_libraries.cmake
    cmake/spot_find_dxc.cmake
    cmake/spot_shader_function.cmake
    cmake/spot_embed_shader.cmake
    cmake/spot_embed_common.cmake
    DESTINATION lib/cmake/Spot COMPONENT spot
)

file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/version.txt" CONTENT "${SPOT_VERSION}\n" NEWLINE_STYLE UNIX)
install(FILES "${PROJECT_BINARY_DIR}/version.txt" DESTINATION . COMPONENT spot)
