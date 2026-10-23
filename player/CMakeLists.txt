# spot_player: runs any game that has no code of its own, from the GameSettings and imported assets it is given. The
# tools run projects without code with it, and ship it, named after the game, as their executable. It links the shared
# engine library, which goes beside it as it does beside the games projects' code builds.
add_executable(spot_player main.cpp)
target_link_libraries(spot_player PRIVATE spot::shared)
spot_set_compile_options(spot_player)

# A folder of its own, with only what ships beside it, which exports copy.
set_target_properties(spot_player PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/player/$<CONFIG>")
if(APPLE)
    set(player_rpath "@loader_path")
else()
    set(player_rpath "$ORIGIN")
endif()
set_target_properties(spot_player PROPERTIES BUILD_RPATH "${player_rpath}" INSTALL_RPATH "${player_rpath}"
    BUILD_WITH_INSTALL_RPATH TRUE)
add_custom_command(TARGET spot_player POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:spot_shared>" "$<TARGET_FILE_DIR:spot_player>"
)
include("${PROJECT_SOURCE_DIR}/cmake/spot_runtime_libraries.cmake")
spot_get_runtime_libraries(player_runtime_libraries)
foreach(library IN LISTS player_runtime_libraries)
    add_custom_command(TARGET spot_player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${library}" "$<TARGET_FILE_DIR:spot_player>"
        VERBATIM
    )
endforeach()
spot_copy_runtime_dlls(spot_player)
