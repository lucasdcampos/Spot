#!/bin/sh
# Installs the Spot engine on Linux and macOS: the newest release of a channel, one version, or a build of the source,
# and puts its command line, spot, on the PATH of the user's shell. From the web, or from a checkout of the repository:
#
#   curl -fsSL https://raw.githubusercontent.com/lucasdcampos/Spot/main/scripts/install.sh | sh
#   curl -fsSL https://raw.githubusercontent.com/lucasdcampos/Spot/main/scripts/install.sh | sh -s -- --channel nightly
#   ./scripts/install.sh --from-source
#
# --channel stable|nightly    the releases to follow, stable by default; `spot update` keeps to it
# --version <version>         that release instead of the newest, as 0.1.0
# --prefix <folder>           where to install, ~/.spot by default
# --from-source [<checkout>]  builds the engine instead of downloading it: from the checkout, the one this script is
#                             in, or else a clone in <prefix>-source (of the tag of --version, if given)
# --no-path                   leaves the shell's PATH as it is
# --force                     installs even when the same version is installed already
set -eu

repository=lucasdcampos/Spot
channel=stable
version=
prefix="$HOME/.spot"
from_source=0
checkout=
modify_path=1
force=0

fail() {
    printf 'spot install: %s\n' "$*" >&2
    exit 1
}

needs_value() {
    [ "$2" -ge 2 ] || fail "$1 needs a value after it"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --channel) needs_value "$1" $#; channel=$2; shift 2 ;;
        --version) needs_value "$1" $#; version=$2; shift 2 ;;
        --prefix) needs_value "$1" $#; prefix=$2; shift 2 ;;
        --from-source)
            from_source=1
            shift
            if [ $# -gt 0 ] && [ "${1#--}" = "$1" ]; then
                checkout=$1
                shift
            fi
            ;;
        --no-path) modify_path=0; shift ;;
        --force) force=1; shift ;;
        *) fail "there is no option '$1'" ;;
    esac
done
case "$channel" in
    stable | nightly) ;;
    *) fail "there are stable and nightly releases, not '$channel'" ;;
esac

case "$(uname -s)" in
    Linux) platform=linux ;;
    Darwin) platform=macos ;;
    *) fail "Spot is installed with this script on Linux and macOS; on Windows, use install.ps1" ;;
esac
case "$(uname -m)" in
    x86_64 | amd64) arch=x64 ;;
    arm64 | aarch64) arch=arm64 ;;
    *) fail "there are no builds for $(uname -m)" ;;
esac
archive="spot-$platform-$arch.tar.gz"

temp=$(mktemp -d "${TMPDIR:-/tmp}/spot-install.XXXXXX")
trap 'rm -rf "$temp"' EXIT

download() {
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$1" -o "$2"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$2" "$1"
    else
        fail "downloading needs curl or wget"
    fi
}

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

is_installation() {
    [ -f "$1/version.txt" ] && [ -f "$1/lib/cmake/Spot/SpotConfig.cmake" ]
}

installed_version() {
    if [ -f "$prefix/version.txt" ]; then
        tr -d '[:space:]' <"$prefix/version.txt"
    fi
}

# Replaces the prefix with the folder. Programs running from the old one, as spot while `spot update` runs it, keep
# their files until they exit.
install_tree() {
    if [ -e "$prefix" ] && [ -n "$(ls -A "$prefix")" ] && ! is_installation "$prefix"; then
        fail "'$prefix' has files and no Spot installation in it: choose another --prefix"
    fi
    mkdir -p "$(dirname "$prefix")"
    rm -rf "$prefix.new" "$prefix.old"
    mv "$1" "$prefix.new"
    if [ -e "$prefix" ]; then
        mv "$prefix" "$prefix.old"
    fi
    mv "$prefix.new" "$prefix"
    rm -rf "$prefix.old"
}

write_install_info() {
    printf 'channel=%s\nsource=%s\n' "$channel" "$1" >"$prefix/install.txt"
}

# The startup file of the user's shell, once.
add_to_path() {
    bin=$1
    case "$(basename "${SHELL:-sh}")" in
        fish)
            mkdir -p "$HOME/.config/fish/conf.d"
            printf 'fish_add_path "%s"\n' "$bin" >"$HOME/.config/fish/conf.d/spot.fish"
            return
            ;;
        zsh) file="${ZDOTDIR:-$HOME}/.zshrc" ;;
        # Terminals on macOS start login shells, which read .bash_profile instead.
        bash) if [ "$platform" = macos ]; then file="$HOME/.bash_profile"; else file="$HOME/.bashrc"; fi ;;
        *) file="$HOME/.profile" ;;
    esac
    if ! grep -qsF "$bin" "$file"; then
        printf '\n# Spot\nexport PATH="%s:$PATH"\n' "$bin" >>"$file"
    fi
}

find_checkout() {
    if [ -n "$checkout" ]; then
        (cd "$checkout" && pwd)
        return
    fi
    # Not when piped into sh, where $0 is the shell.
    script_dir=$(dirname "$0")
    if [ -f "$0" ] && [ -f "$script_dir/../CMakePresets.json" ]; then
        (cd "$script_dir/.." && pwd)
        return
    fi
    command -v git >/dev/null 2>&1 || fail "building from source needs Git, to clone the engine"
    clone="$prefix-source"
    if [ -d "$clone/.git" ]; then
        if [ -n "$version" ]; then
            git -C "$clone" fetch --depth 1 origin tag "v$version" >&2 || fail "could not fetch v$version"
            git -C "$clone" checkout "v$version" >&2 || fail "could not check out v$version"
        else
            git -C "$clone" pull --ff-only >&2 || fail "could not update '$clone'"
        fi
    else
        branch=main
        if [ -n "$version" ]; then branch="v$version"; fi
        git clone --depth 1 --branch "$branch" "https://github.com/$repository.git" "$clone" >&2 ||
            fail "could not clone the engine into '$clone'"
    fi
    printf '%s\n' "$clone"
}

if [ "$from_source" = 1 ]; then
    command -v cmake >/dev/null 2>&1 || fail "building from source needs CMake 3.25 or newer"
    command -v ninja >/dev/null 2>&1 || fail "building from source needs Ninja"
    source_dir=$(find_checkout)
    printf 'Building Spot from %s\n' "$source_dir"
    # The repository's own presets, so the install shares the build directory of working on the engine.
    (
        cd "$source_dir" &&
            cmake --preset ninja &&
            cmake --build --preset ninja-release --target spot_cli spot_editor spot_player glslang-standalone spirv-cross &&
            cmake --install build/ninja --config Release --component spot --prefix "$temp/spot"
    ) || fail "building the engine failed"
    install_tree "$temp/spot"
    write_install_info source
else
    case "$platform-$arch" in
        linux-x64 | macos-arm64) ;;
        *) fail "there are no releases for $platform on $arch: install with --from-source" ;;
    esac
    if [ -n "$version" ]; then
        base="https://github.com/$repository/releases/download/v$version"
    elif [ "$channel" = nightly ]; then
        base="https://github.com/$repository/releases/download/nightly"
    else
        base="https://github.com/$repository/releases/latest/download"
    fi
    download "$base/version.txt" "$temp/version.txt" || fail "there is no release at $base"
    latest=$(tr -d '[:space:]' <"$temp/version.txt")
    if [ "$force" = 0 ] && [ "$(installed_version)" = "$latest" ]; then
        printf 'Spot %s is installed already, in %s\n' "$latest" "$prefix"
    else
        printf 'Downloading Spot %s\n' "$latest"
        download "$base/$archive" "$temp/$archive"
        download "$base/$archive.sha256" "$temp/$archive.sha256"
        expected=$(cut -d ' ' -f 1 <"$temp/$archive.sha256")
        actual=$(sha256 "$temp/$archive")
        [ "$actual" = "$expected" ] || fail "the download is not what was released: its SHA-256 is $actual, not $expected"
        mkdir "$temp/extracted"
        tar -xzf "$temp/$archive" -C "$temp/extracted"
        install_tree "$temp/extracted/spot"
    fi
    write_install_info download
fi

bin="$prefix/bin"
if [ "$modify_path" = 1 ]; then
    add_to_path "$bin"
fi
printf 'Spot %s is installed in %s.\n' "$(installed_version)" "$prefix"
if [ "$modify_path" = 1 ]; then
    printf "Run 'spot help' in a new terminal to start.\n"
else
    printf 'Its command line is %s/spot.\n' "$bin"
fi
