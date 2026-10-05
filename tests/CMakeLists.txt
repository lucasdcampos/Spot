include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.0
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(Catch2)

add_executable(spot_tests
    event_tests.cpp
    log_tests.cpp
    version_tests.cpp
)

target_link_libraries(spot_tests PRIVATE
    spot::spot
    Catch2::Catch2WithMain
)

target_compile_definitions(spot_tests PRIVATE
    SPOT_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SPOT_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SPOT_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    SPOT_EXPECTED_VERSION_STRING="${PROJECT_VERSION}"
)

spot_set_warnings(spot_tests)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(spot_tests)
