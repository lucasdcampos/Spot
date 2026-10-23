#!/bin/sh
# Builds and runs: ./run.sh [sandbox|editor] [debug|release] [no-build]
set -e
cd "$(dirname "$0")"

target=sandbox
config=release
build=true
for arg in "$@"; do
    case "$arg" in
        sandbox | editor) target="$arg" ;;
        debug | release) config="$arg" ;;
        no-build) build=false ;;
        *) echo "Unknown argument '$arg': expected sandbox, editor, debug, release or no-build" >&2; exit 1 ;;
    esac
done
case "$target" in
    editor) cmakeTarget=spot_editor ;;
    *) cmakeTarget=sandbox ;;
esac

case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) preset=msvc ;;
    *) preset=ninja ;;
esac

if [ "$build" = true ]; then
    # Configure only once; later builds re-run CMake when its files change.
    [ -f "build/$preset/CMakeCache.txt" ] || cmake --preset "$preset"
    cmake --build --preset "$preset-$config" --target "$cmakeTarget"
fi

case "$config" in
    release) configDir=Release ;;
    *) configDir=Debug ;;
esac
executable="./build/$preset/$target/$configDir/$cmakeTarget"
if [ ! -x "$executable" ]; then
    echo "Executable '$executable' not found or not executable. Run './run.sh $target $config' to build it first." >&2
    exit 1
fi
exec "$executable"
