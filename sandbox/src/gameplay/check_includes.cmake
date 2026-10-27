# Fails when a source of the gameplay framework includes what a game that takes the framework would not have: the
# sandbox's code, a folder of the framework that its own does not need, or a library other than the engine and the
# standard library. Taking a folder and those it needs then builds in any game (docs/internals/sandbox_gameplay.md).
#
#     cmake -DGAMEPLAY_DIR=<repository>/sandbox/src/gameplay -P check_includes.cmake
#
# Sources live in a system's folder and include the framework by its path, "gameplay/<folder>/<file>.h". A folder
# includes itself and the folders it needs below; the engine as <spot.h> or <spot/...>; and the standard library.

# The other folders each folder needs, as "folder=needed,needed".
set(GAMEPLAY_NEEDS
    "camera=core"
    "character=camera,core,effects,health,hud,persistence"
    "environment=core,effects,hud"
    "game=core,health,hud,menus,persistence"
    "health=core,hud"
    "hud=core"
    "inventory=core,hud,menus"
    "menus=core,hud"
    "vehicles=core,environment,hud,persistence"
    "weapons=camera,core,effects,health,inventory"
)

if(NOT IS_DIRECTORY "${GAMEPLAY_DIR}")
    message(FATAL_ERROR "GAMEPLAY_DIR must name the folder of the gameplay framework")
endif()

foreach(entry IN LISTS GAMEPLAY_NEEDS)
    string(REPLACE "=" ";" pair "${entry}")
    list(GET pair 0 folder)
    list(GET pair 1 needed)
    string(REPLACE "," ";" NEEDS_${folder} "${needed}")
endforeach()

file(GLOB_RECURSE sources RELATIVE "${GAMEPLAY_DIR}" "${GAMEPLAY_DIR}/*.h" "${GAMEPLAY_DIR}/*.cpp")
set(violations "")
foreach(source IN LISTS sources)
    if(NOT source MATCHES "^([^/]+)/")
        string(APPEND violations "\n  ${source}: not in a system's folder")
        continue()
    endif()
    set(folder "${CMAKE_MATCH_1}")
    file(STRINGS "${GAMEPLAY_DIR}/${source}" includes REGEX "^[ \t]*#[ \t]*include")
    foreach(line IN LISTS includes)
        if(line MATCHES "^[ \t]*#[ \t]*include[ \t]*\"gameplay/([^/\"]+)/[^\"]+\"")
            if(CMAKE_MATCH_1 STREQUAL folder OR CMAKE_MATCH_1 IN_LIST NEEDS_${folder})
                continue()
            endif()
        elseif(line MATCHES "^[ \t]*#[ \t]*include[ \t]*<(spot\\.h|spot/[^>]+|[a-z0-9_]+)>")
            continue()
        endif()
        string(APPEND violations "\n  ${source}: ${line}")
    endforeach()
endforeach()

if(violations)
    message(FATAL_ERROR "The gameplay framework includes what its folders do not need:${violations}")
endif()
list(LENGTH sources count)
message(STATUS "The gameplay framework's ${count} sources include only the engine and the folders they need")
