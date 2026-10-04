add_library(spot STATIC
    spot.h
    version.h
    version.cpp
)
add_library(spot::spot ALIAS spot)

target_include_directories(spot PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_features(spot PUBLIC cxx_std_20)

target_compile_definitions(spot PRIVATE
    SPOT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SPOT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SPOT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    SPOT_VERSION_STRING="${PROJECT_VERSION}"
)

spot_set_warnings(spot)
