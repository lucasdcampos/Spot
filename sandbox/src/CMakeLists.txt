# Builds the sandbox's code into the game module that the editor and the player load. The editor builds it whenever
# the code changes, pointing Spot_DIR at its own build of the engine. The sandbox executable builds the same sources
# in itself, from ../CMakeLists.txt.
cmake_minimum_required(VERSION 3.25)
project(Sandbox LANGUAGES CXX)

find_package(Spot CONFIG REQUIRED)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp *.h)
spot_add_game_module(game ${sources})
