# Runs projects made in the editor outside of it, as their games will run: their startup scene with their game's
# code. It links the shared engine library, as the game modules it loads do.
add_executable(spot_player
    main.cpp
    player_application.cpp
    player_application.h
)
target_link_libraries(spot_player PRIVATE spot::tools spot::import spot::shared)
spot_set_compile_options(spot_player)
spot_copy_runtime_dlls(spot_player)
