add_executable(sandbox
    main.cpp
)

target_link_libraries(sandbox PRIVATE
    spot::spot
)

spot_add_shaders(sandbox
    shaders/triangle.vert.hlsl
    shaders/triangle.frag.hlsl
)

spot_set_warnings(sandbox)
