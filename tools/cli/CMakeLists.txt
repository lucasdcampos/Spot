# spot, the tools' command line: creates projects, imports their assets, builds, runs and exports their games. It
# links the shared engine library, which the games it builds link too.
add_executable(spot_cli main.cpp)
set_target_properties(spot_cli PROPERTIES OUTPUT_NAME spot)
target_link_libraries(spot_cli PRIVATE spot::tools spot::import spot::shared)
spot_set_compile_options(spot_cli)
spot_copy_runtime_dlls(spot_cli)
# `spot editor` opens the editor installed beside it, or else the one of this build.
if(SPOT_BUILD_EDITOR)
    target_compile_definitions(spot_cli PRIVATE SPOT_CLI_EDITOR="$<TARGET_FILE:spot_editor>")
endif()
