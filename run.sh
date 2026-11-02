#!/bin/sh
# Builds the CLI and runs it in the caller's working directory:
#   ./run.sh [debug|release] [no-build] <command> [options]
#   ./run.sh new MyGame
#   ./run.sh build
#   ./run.sh run
set -e
caller=$(pwd)
root=$(cd "$(dirname "$0")" && pwd)
cd "$root"

config=release
build=true

while [ $# -gt 0 ]; do
    case "$1" in
        debug | release) config="$1" ;;
        no-build) build=false ;;
        *) break ;;
    esac
    shift
done

cmakeTarget=spot_cli
executableName=spot

case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) preset=msvc ;;
    *) preset=ninja ;;
esac

if [ "$build" = true ]; then
    [ -f "build/$preset/CMakeCache.txt" ] || cmake --preset "$preset"
    cmake --build --preset "$preset-$config" --target "$cmakeTarget"
fi

case "$config" in
    release) configDir=Release ;;
    *) configDir=Debug ;;
esac

executable="$root/build/$preset/cli/$configDir/$executableName"
if [ ! -x "$executable" ]; then
    echo "Executable '$executable' not found or not executable. Run './run.sh' to build it first." >&2
    exit 1
fi

cd "$caller"
"$executable" "$@"
