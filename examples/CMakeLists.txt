# One example per level of using the engine, built and run by the tests (see docs/architecture.md).
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
