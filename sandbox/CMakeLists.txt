add_executable(sandbox
    main.cpp
)

target_link_libraries(sandbox PRIVATE
    spot::spot
)

spot_set_warnings(sandbox)
