add_executable(sandbox
    main.cpp
)

target_link_libraries(sandbox PRIVATE
    spot::spot
)

# Assets are found next to the executable, so they are copied there after every build.
add_custom_command(TARGET sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:sandbox>/assets"
    VERBATIM
)

spot_set_compile_options(sandbox)
