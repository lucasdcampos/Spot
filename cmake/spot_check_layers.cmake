# Fails when a source of the engine includes a header of a layer above its own, so the layers stay usable on their
# own: a game can build on the core, the platform or the systems without the scene or the Application above them.
#
#     cmake -DSPOT_ENGINE_DIR=<repository>/engine/spot -P spot_check_layers.cmake
#
# A folder may include its own layer and the ones below. Backends live inside their system's folder.
set(SPOT_LAYERS
    "core=0"
    "platform=1"
    "graphics=2" "audio=2" "physics=2" "animation=2" "particles=2" "assets=2"
    "scene=3"
    "runtime=4"
    "import=5"
)

foreach(entry IN LISTS SPOT_LAYERS)
    string(REPLACE "=" ";" pair "${entry}")
    list(GET pair 0 folder)
    list(GET pair 1 level)
    set(LAYER_${folder} ${level})
endforeach()

if(NOT IS_DIRECTORY "${SPOT_ENGINE_DIR}")
    message(FATAL_ERROR "SPOT_ENGINE_DIR must name the engine's folder")
endif()

file(GLOB_RECURSE sources RELATIVE "${SPOT_ENGINE_DIR}" "${SPOT_ENGINE_DIR}/*.h" "${SPOT_ENGINE_DIR}/*.cpp")
set(violations "")
foreach(source IN LISTS sources)
    string(REGEX MATCH "^[^/]+" folder "${source}")
    if(NOT DEFINED LAYER_${folder})
        continue()
    endif()
    file(STRINGS "${SPOT_ENGINE_DIR}/${source}" includes REGEX "^#include \"spot/[a-z0-9_]+/")
    foreach(line IN LISTS includes)
        string(REGEX REPLACE "^#include \"spot/([a-z0-9_]+)/.*$" "\\1" included "${line}")
        if(DEFINED LAYER_${included} AND LAYER_${included} GREATER LAYER_${folder})
            string(APPEND violations "\n  ${source}: ${line}")
        endif()
    endforeach()
endforeach()

if(violations)
    message(FATAL_ERROR "Headers of a higher layer are included:${violations}")
endif()
message(STATUS "Every engine source includes only its own layer and the ones below")
