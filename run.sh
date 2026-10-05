#!/bin/sh
# Builds and runs the sandbox or the editor: ./run.sh [sandbox|editor] [debug|release]
set -e
cd "$(dirname "$0")"

target=sandbox
config=debug
for arg in "$@"; do
    case "$arg" in
        sandbox | editor) target="$arg" ;;
        debug | release) config="$arg" ;;
        *) echo "Unknown argument '$arg': expected sandbox, editor, debug or release" >&2; exit 1 ;;
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

# Configure only once; later builds re-run CMake by themselves when the CMake files change.
[ -f "build/$preset/CMakeCache.txt" ] || cmake --preset "$preset"
cmake --build --preset "$preset-$config" --target "$cmakeTarget"

case "$config" in
    release) configDir=Release ;;
    *) configDir=Debug ;;
esac
exec "./build/$preset/$target/$configDir/$cmakeTarget"
