add_executable(sandbox
    main.cpp
)

target_link_libraries(sandbox PRIVATE
    spot::spot
)

spot_add_shaders(sandbox
    shaders/color.frag.hlsl
    shaders/quad.vert.hlsl
)

spot_set_warnings(sandbox)
