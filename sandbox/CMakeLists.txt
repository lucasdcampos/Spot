add_executable(sandbox
    main.cpp
)

target_link_libraries(sandbox PRIVATE
    spot::spot
)

# The sandbox imports its assets from the repository at startup, so editing them needs no rebuild.
target_compile_definitions(sandbox PRIVATE
    SPOT_SANDBOX_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

spot_set_compile_options(sandbox)
