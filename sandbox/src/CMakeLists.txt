# Builds the sandbox's code into its game's executable, and into the game module that the editor loads. The tools
# build it, the editor whenever the code changes, pointing Spot_DIR at their own build of the engine. The sandbox
# executable of the engine's own build builds the same sources, from ../CMakeLists.txt.
cmake_minimum_required(VERSION 3.25)
project(Sandbox LANGUAGES CXX)

find_package(Spot CONFIG REQUIRED)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp *.h)
spot_add_game(${sources})
# Files include each other by their path from here, such as "gameplay/core/pawn.h", from any folder.
target_include_directories(game_code PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
