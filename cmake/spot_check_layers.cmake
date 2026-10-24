# Fails when a source of the engine includes a header of a folder at its own level or above, so each level stays
# usable on its own: a game can build on the core, the platform, graphics or a system without the scene or the
# Application above them, and the systems of a level stay independent of each other.
#
#     cmake -DSPOT_ENGINE_DIR=<repository>/engine/spot -P spot_check_layers.cmake
#
# A folder may include itself and the levels below. Each folder of scene/ is a folder of its own: scene/<module>/
# holds a system's components and its module, above the scene core. Backends live inside their system's folder.
set(SPOT_LEVELS
    "core=0"
    "platform=1"
    "assets=2"
    "graphics=3"
    "render=4" "audio=4" "physics=4" "animation=4" "particles=4"
    "scene=5"
    "runtime=6"
    "scene/animation=7" "scene/audio=7" "scene/particles=7" "scene/physics=7" "scene/physics2d=7"
    "scene/rendering=7" "scene/ui=7"
    "scene/scripting=8"
    "import=9"
)

# Includes across a level that are part of the design: models draw animation's skeletons and clips, and ragdolls
# animate physics' components.
set(SPOT_LEVEL_EXCEPTIONS
    "render>animation"
    "scene/animation>scene/physics"
)

# Today's violations, each fixed by a task of the refactor: the commit that fixes one removes its line.
set(SPOT_LEVEL_ALLOWLIST
    "scene/rendering/scene_model.cpp>scene/animation/animator.h"
    "scene/rendering/scene_model.cpp>scene/animation/bone_attachment.h"
    "scene/rendering/scene_model.cpp>scene/animation/skinned_mesh_renderer.h"
    "scene/rendering/scene_renderer.cpp>scene/ui/ui_system.h"
    "runtime/application.cpp>scene/animation/animation_components.h"
    "runtime/application.cpp>scene/audio/audio_components.h"
    "runtime/application.cpp>scene/particles/particles_components.h"
    "runtime/application.cpp>scene/physics/physics_components.h"
    "runtime/application.cpp>scene/physics2d/physics2d_components.h"
    "runtime/application.cpp>scene/rendering/rendering_components.h"
    "runtime/application.cpp>scene/scripting/scripting_components.h"
    "runtime/application.cpp>scene/ui/ui_components.h"

    "runtime/application.cpp>scene/audio/audio_system.h"
    "runtime/application.cpp>scene/rendering/scene_renderer.h"
    "runtime/application.cpp>scene/ui/ui_input_system.h"
    "runtime/engine_console.cpp>scene/audio/audio_system.h"
    "runtime/engine_console.cpp>scene/physics/physics_world.h"
    "runtime/game_settings.h>scene/physics/physics_settings.h"
    "runtime/game_settings.h>scene/rendering/rendering_settings.h"
    "runtime/runtime_stats.cpp>scene/physics/physics_world.h"
    "scene/audio/audio_system.cpp>scene/rendering/camera.h"
    "scene/physics/physics_settings.cpp>scene/physics2d/physics_world_2d.h"
    "scene/physics2d/physics_world_2d.h>scene/physics/physics_query.h"
    "scene/physics2d/tilemap_collider_2d.cpp>scene/rendering/tilemap.h"
    "scene/rendering/scene_drawing.cpp>scene/animation/skinned_mesh_renderer.h"
    "scene/rendering/scene_drawing.cpp>scene/particles/particle_system.h"
    "scene/rendering/scene_renderer.cpp>scene/particles/particle_system.h"
)

foreach(entry IN LISTS SPOT_LEVELS)
    string(REPLACE "=" ";" pair "${entry}")
    list(GET pair 0 folder)
    list(GET pair 1 level)
    set(LEVEL_${folder} ${level})
endforeach()

if(NOT IS_DIRECTORY "${SPOT_ENGINE_DIR}")
    message(FATAL_ERROR "SPOT_ENGINE_DIR must name the folder of the engine's modules")
endif()

# The folder a path of engine/spot/ belongs to: scene/<module>/ when it has one, else its first folder.
function(spot_get_level_folder path out)
    string(REGEX MATCH "^scene/[a-z0-9_]+/" sub "${path}")
    if(sub)
        string(REGEX REPLACE "/$" "" sub "${sub}")
        if(DEFINED LEVEL_${sub})
            set(${out} "${sub}" PARENT_SCOPE)
            return()
        endif()
    endif()
    string(REGEX MATCH "^[^/]+" folder "${path}")
    set(${out} "${folder}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE sources RELATIVE "${SPOT_ENGINE_DIR}" "${SPOT_ENGINE_DIR}/*.h" "${SPOT_ENGINE_DIR}/*.cpp")
set(violations "")
set(found "")
foreach(source IN LISTS sources)
    spot_get_level_folder("${source}" folder)
    if(NOT DEFINED LEVEL_${folder})
        continue()
    endif()
    file(STRINGS "${SPOT_ENGINE_DIR}/${source}" includes REGEX "^#include \"spot/[a-z0-9_]+/")
    foreach(line IN LISTS includes)
        string(REGEX REPLACE "^#include \"spot/([^\"]+)\".*$" "\\1" header "${line}")
        spot_get_level_folder("${header}" included)
        if(NOT DEFINED LEVEL_${included} OR included STREQUAL folder)
            continue()
        endif()
        if(LEVEL_${included} LESS LEVEL_${folder} OR "${folder}>${included}" IN_LIST SPOT_LEVEL_EXCEPTIONS)
            continue()
        endif()
        set(entry "${source}>${header}")
        list(APPEND found "${entry}")
        if(NOT entry IN_LIST SPOT_LEVEL_ALLOWLIST)
            string(APPEND violations "\n  ${source}: ${line}")
        endif()
    endforeach()
endforeach()

set(fixed "")
foreach(entry IN LISTS SPOT_LEVEL_ALLOWLIST)
    if(NOT entry IN_LIST found)
        string(APPEND fixed "\n  ${entry}")
    endif()
endforeach()

if(violations)
    message(FATAL_ERROR "Headers of the same level or above are included:${violations}")
endif()
if(fixed)
    message(FATAL_ERROR "Remove these fixed entries from SPOT_LEVEL_ALLOWLIST:${fixed}")
endif()
message(STATUS "Every engine source includes only its own folder and the levels below")
