#!/bin/sh
# Builds and runs: ./run.sh [sandbox|editor] [debug|release] [no-build]
#                  ./run.sh [debug|release] [no-build] cli <arguments>
# Everything after cli goes to the command line, which runs in the current folder, as installed it would.
set -e
caller=$(pwd)
root=$(cd "$(dirname "$0")" && pwd)
cd "$root"

target=sandbox
config=release
build=true
while [ $# -gt 0 ]; do
    case "$1" in
        cli) target=cli; shift; break ;;
        sandbox | editor) target="$1" ;;
        debug | release) config="$1" ;;
        no-build) build=false ;;
        *) echo "Unknown argument '$1': expected sandbox, editor, cli, debug, release or no-build" >&2; exit 1 ;;
    esac
    shift
done
case "$target" in
    editor) cmakeTarget=spot_editor; executableName=spot_editor ;;
    cli) cmakeTarget=spot_cli; executableName=spot ;;
    *) cmakeTarget=sandbox; executableName=sandbox ;;
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
executable="$root/build/$preset/$target/$configDir/$executableName"
if [ ! -x "$executable" ]; then
    echo "Executable '$executable' not found or not executable. Run './run.sh $config $target' to build it first." >&2
    exit 1
fi
if [ "$target" = cli ]; then
    cd "$caller"
fi
exec "$executable" "$@"
