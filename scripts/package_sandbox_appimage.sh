#!/bin/sh
# Packages the Sandbox sample game into a standalone Linux AppImage.
# Usage: ./scripts/package_sandbox_appimage.sh
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

dist_dir="$root/dist"
build_dir="$root/build"
tools_dir="$build_dir/tools"
appdir="$build_dir/Sandbox.AppDir"
export_dir="$build_dir/export/sandbox"
appimage_tool="$tools_dir/appimagetool-extracted/AppRun"
output_appimage="$dist_dir/Sandbox-x86_64.AppImage"

mkdir -p "$dist_dir" "$tools_dir"

# 1. Download and extract appimagetool if not already present
if [ ! -x "$appimage_tool" ]; then
    echo "Downloading appimagetool..."
    curl -sLo "$tools_dir/appimagetool.AppImage" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$tools_dir/appimagetool.AppImage"
    (cd "$tools_dir" && ./appimagetool.AppImage --appimage-extract >/dev/null 2>&1)
    mv "$tools_dir/squashfs-root" "$tools_dir/appimagetool-extracted"
    rm -f "$tools_dir/appimagetool.AppImage"
fi

# 2. Build spot CLI if not present
spot_cli="$build_dir/ninja/cli/Release/spot"
if [ ! -x "$spot_cli" ]; then
    echo "Building spot CLI..."
    [ -f "$build_dir/ninja/CMakeCache.txt" ] || cmake --preset ninja
    cmake --build --preset ninja-release --target spot_cli
fi

# 3. Export Sandbox game assets and binary
echo "Exporting Sandbox..."
"$spot_cli" export sandbox/Sandbox.spot --out "$export_dir"

# 4. Prepare AppDir structure
echo "Assembling AppDir structure..."
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin"

# Copy exported game files
cp -r "$export_dir"/* "$appdir/usr/bin/"

# Copy icon
if [ -f "$root/logo.png" ]; then
    cp "$root/logo.png" "$appdir/spot.png"
    cp "$root/logo.png" "$appdir/.DirIcon"
fi

# Desktop entry
cat << 'EOF' > "$appdir/Sandbox.desktop"
[Desktop Entry]
Type=Application
Name=Sandbox
Comment=Spot Sandbox Sample Game
Exec=AppRun
Icon=spot
Categories=Game;
Terminal=false
EOF

# AppRun launcher
cat << 'EOF' > "$appdir/AppRun"
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/bin:$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
cd "$HERE/usr/bin"
exec "$HERE/usr/bin/Sandbox" "$@"
EOF
chmod +x "$appdir/AppRun"

# 5. Build the AppImage
echo "Generating $output_appimage..."
ARCH=x86_64 "$appimage_tool" "$appdir" "$output_appimage"

echo "Successfully built AppImage at $output_appimage"
