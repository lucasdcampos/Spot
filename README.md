# Spot

A cross-platform 2D/3D game engine written in C++20, built as a static library.

## Building

Requires CMake 3.25+ and a C++20 compiler. Dependencies (SDL3, Catch2 and others) are downloaded and built by CMake.
On Linux, SDL3 needs the X11 and Wayland development packages; see
[SDL's Linux notes](https://github.com/libsdl-org/SDL/blob/main/docs/README-linux.md).

```sh
# Windows (Visual Studio 2026)
cmake --preset msvc
cmake --build --preset msvc-debug
ctest --preset msvc-debug

# Linux / macOS (Ninja)
cmake --preset ninja
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

## Running the sandbox and the editor

```sh
./run.ps1          # Windows (PowerShell)
./run.sh           # Linux / macOS
```

Pass `editor` to run the editor instead of the sandbox, and `release` to run an optimized build.

In the sandbox, WASD or the arrow keys move the camera, the mouse wheel zooms, F1 switches between the 2D and 3D
views and Escape quits. In 3D, dragging with the right mouse button also orbits the camera.

## Game code in the editor

Every project has the same layout: its `<name>.spot` project file, `assets/` with the source assets, `src/` with the
game's C++ code and its `CMakeLists.txt` when it has code, `builds/` for exported games and `.spot/` for what the
editor generates; the last two stay out of version control.

Projects made from the editor's "3D with C++" template have a `src/` folder with the game's own components, and the
asset browser's Create > C++ Component adds one to any project, as a `.h` and a `.cpp`; components register
themselves with `SPOT_COMPONENT(Type);` in their source file. The editor builds that code into a game module with
CMake and the compiler that built the editor, whenever it changes or on Ctrl+B, and loads it without closing the open
scenes, so their entities, selections and undo history stay as they were. Build errors and warnings show in the
console. Games made only in code link the static engine library and need none of this.

F5 runs the project's game in its own window with `spot_player`, from its startup scene, as it will ship; what the game
logs shows in the editor's console. `spot_player <project>` also runs it on its own.

## Assets

Games load assets through `Spot::Assets` (or `Application::GetAssets()`), never from raw files. During development
the application imports a source folder, set in `ApplicationConfig::assets`, into the engine's own binary formats:

- Every source file gets a `.meta` file beside it with a permanent id and its import settings. Commit both.
- Imports are cached, and run again only when a source, its settings or a file it depends on change.
- A shipped game only needs the import folder.

Supported sources: PNG/JPEG/BMP/TGA textures, glTF models (`.gltf`, `.glb`), WAV/Ogg sounds, TrueType/OpenType fonts,
`.material` files and `.scene` files.

## Layout

- `engine/` – the engine library
- `sandbox/` – application for trying out the engine
- `editor/` – the editor, built with Dear ImGui
- `player/` – runs projects made in the editor, with their game's code
- `tests/` – unit tests
