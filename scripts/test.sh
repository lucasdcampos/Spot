#!/bin/sh
# Runs targeted tests during development to avoid full rebuilds and whole-suite test runs:
#   ./scripts/test.sh [debug|release] [no-build] [msvc|ninja] [-t|--target <target>] [filter] [options...]
#
# Examples:
#   ./scripts/test.sh "[math]"
#   ./scripts/test.sh "Transform*"
#   ./scripts/test.sh -t editor "ConsoleLog*"
#   ./scripts/test.sh no-build "[scene]"
#   ./scripts/test.sh -t gpu

set -e
caller=$(pwd)
scriptDir=$(cd "$(dirname "$0")" && pwd)
projectRoot=$(cd "$scriptDir/.." && pwd)
cd "$projectRoot"

config=debug
build=true
preset=""
target="spot_tests"

resolve_target() {
    case "$1" in
        main | tests | spot) echo "spot_tests" ;;
        editor) echo "spot_editor_tests" ;;
        editor-gpu) echo "spot_editor_gpu_tests" ;;
        editor-module) echo "spot_editor_module_tests" ;;
        editor-tools) echo "spot_editor_tools_tests" ;;
        gpu) echo "spot_gpu_tests" ;;
        anim | animation) echo "spot_animation_tests" ;;
        ui) echo "spot_ui_tests" ;;
        particle | particles) echo "spot_particle_tests" ;;
        gameplay) echo "spot_gameplay_tests" ;;
        console) echo "spot_console_tests" ;;
        luau) echo "spot_luau_tests" ;;
        luau-shared) echo "spot_luau_shared_tests" ;;
        script) echo "spot_script_tests" ;;
        script-asset) echo "spot_script_asset_tests" ;;
        script-shared) echo "spot_script_shared_tests" ;;
        profile) echo "spot_profile_process_tests" ;;
        spot_*) echo "$1" ;;
        *) echo "spot_$1" ;;
    esac
}

while [ $# -gt 0 ]; do
    case "$1" in
        debug | release) config="$1" ;;
        no-build) build=false ;;
        msvc | ninja) preset="$1" ;;
        -t | --target)
            shift
            if [ $# -gt 0 ]; then
                target=$(resolve_target "$1")
            fi
            ;;
        main | tests | spot | editor | editor-gpu | editor-module | editor-tools | gpu | \
        anim | animation | ui | particle | particles | gameplay | console | luau | \
        luau-shared | script | script-asset | script-shared | profile)
            target=$(resolve_target "$1")
            ;;
        *) break ;;
    esac
    shift
done

if [ -z "$preset" ]; then
    case "$(uname -s)" in
        MINGW* | MSYS* | CYGWIN*)
            if [ -f "$projectRoot/build/msvc/CMakeCache.txt" ]; then
                preset=msvc
            elif [ -f "$projectRoot/build/ninja/CMakeCache.txt" ]; then
                preset=ninja
            else
                preset=msvc
            fi
            ;;
        *)
            preset=ninja
            ;;
    esac
fi

if [ "$build" = true ]; then
    [ -f "build/$preset/CMakeCache.txt" ] || cmake --preset "$preset"
    cmake --build --preset "$preset-$config" --target "$target"
fi

case "$config" in
    release) configDir=Release ;;
    *) configDir=Debug ;;
esac

executable=""
for cand in \
    "$projectRoot/build/$preset/tests/$configDir/$target.exe" \
    "$projectRoot/build/$preset/tests/$target.exe" \
    "$projectRoot/build/$preset/tests/$configDir/$target" \
    "$projectRoot/build/$preset/tests/$target"; do
    if [ -x "$cand" ] || [ -f "$cand" ]; then
        executable="$cand"
        break
    fi
done

if [ -z "$executable" ]; then
    echo "Test executable for target '$target' not found." >&2
    exit 1
fi

case "$target" in
    *gpu*) ;;
    *) export SDL_VIDEO_DRIVER="${SDL_VIDEO_DRIVER:-dummy}" ;;
esac

cd "$caller"
"$executable" "$@"
