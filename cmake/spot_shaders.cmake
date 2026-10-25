# Shader build: HLSL sources compiled to every format the graphics backend may ask for.
#   SPIR-V (Vulkan)   glslang, built from source, twice: for SDL_GPU, with each texture combined with its sampler, and
#                     for the NVRHI backend, with textures and samplers apart and bindings offset by resource type
#                     as NVRHI numbers them
#   MSL (Metal)       SPIRV-Cross, translated from the SPIR-V
#   DXIL (Direct3D)   the DXC that ships with the Windows SDK, when it is installed
# The results are embedded in a generated header, so shaders need no files at runtime.

include(FetchContent)

# SPIRV-Cross targets an old CMake policy where option() ignores the variables set below.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(ENABLE_OPT OFF)
set(ENABLE_GLSLANG_BINARIES ON)
set(GLSLANG_TESTS OFF)
set(GLSLANG_ENABLE_INSTALL OFF)
set(BUILD_EXTERNAL OFF)
FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG 16.6.0
    GIT_SHALLOW TRUE
    SYSTEM
)

set(SPIRV_CROSS_CLI ON)
set(SPIRV_CROSS_ENABLE_TESTS OFF)
set(SPIRV_CROSS_SHARED OFF)
set(SPIRV_CROSS_STATIC ON)
set(SPIRV_CROSS_SKIP_INSTALL ON)
FetchContent_Declare(spirv_cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG vulkan-sdk-1.4.363.0
    GIT_SHALLOW TRUE
    SYSTEM
)

# glslang warns at configure time that its HLSL front end will be removed in a future major release. The tag above is
# pinned, and the HLSL sources would move to another compiler unchanged, so the warning needs no action for now.
FetchContent_MakeAvailable(glslang spirv_cross)

include("${CMAKE_CURRENT_LIST_DIR}/spot_find_dxc.cmake")

if(SPOT_DXC)
    message(STATUS "Spot: compiling shaders to DXIL with ${SPOT_DXC}")
else()
    message(STATUS "Spot: DXC not found, shaders will not include DXIL (Direct3D 12 unavailable)")
endif()

# What spot_add_shaders() runs, which the engine's CMake package hands games too (see SpotConfig.cmake.in).
set(SPOT_SHADER_GLSLANG "$<TARGET_FILE:glslang-standalone>")
set(SPOT_SHADER_SPIRV_CROSS "$<TARGET_FILE:spirv-cross>")
set(SPOT_SHADER_TOOL_TARGETS glslang-standalone spirv-cross)
set(SPOT_SHADER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/engine/spot/render/shaders/include")
include("${CMAKE_CURRENT_LIST_DIR}/spot_shader_function.cmake")
