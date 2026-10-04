#!/bin/sh
set -e
cd "$(dirname "$0")"

config="${1:-debug}"

case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) preset=msvc ;;
    *) preset=ninja ;;
esac

# Configure only once; later builds re-run CMake by themselves when the CMake files change.
[ -f "build/$preset/CMakeCache.txt" ] || cmake --preset "$preset"
cmake --build --preset "$preset-$config" --target sandbox

case "$config" in
    release) configDir=Release ;;
    *) configDir=Debug ;;
esac
exec "./build/$preset/sandbox/$configDir/sandbox"
