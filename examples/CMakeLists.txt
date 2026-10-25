# One example per level of using the engine, built and run by the tests (see docs/internals/architecture.md).
foreach(example IN ITEMS own_loop application)
    add_executable(example_${example} ${example}/main.cpp)
    target_link_libraries(example_${example} PRIVATE spot::spot)
    spot_set_compile_options(example_${example})
endforeach()

# A project without code: the tools cook its game beside the player, which runs it from its game.json.
add_custom_target(example_data_driven ALL
    COMMAND spot_cli cook "${CMAKE_CURRENT_SOURCE_DIR}/data_driven/DataDriven.spot"
            --out "${CMAKE_CURRENT_BINARY_DIR}/data_driven"
    COMMENT "Cooking the data-driven example"
    VERBATIM
)

# The manual's first game, a project without code like the one above, played by the headless tests.
add_custom_target(example_first_game ALL
    COMMAND spot_cli cook "${CMAKE_CURRENT_SOURCE_DIR}/first_game/Coins.spot"
            --out "${CMAKE_CURRENT_BINARY_DIR}/first_game"
    COMMENT "Cooking the first game example"
    VERBATIM
)
