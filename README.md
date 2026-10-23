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

The normal test presets run headless tests with at most two processes and open no GPU test windows. GPU tests
are separate: use `ctest --preset msvc-debug-gpu` (or `ninja-debug-gpu`) when changing rendering, shaders or visual
editor behavior, optionally selecting cases with `-R <pattern>`. They open real windows and run one at a time.
Use `ctest --preset msvc-debug-all` (or `ninja-debug-all`) for the complete suite. Release presets offer the same
`-gpu` and `-all` variants.

## Running the sandbox and the editor

```sh
./run.ps1          # Windows (PowerShell)
./run.sh           # Linux / macOS
```

Both scripts build and run Release by default. Pass `editor` to run the editor instead of the sandbox,
or `debug` for a Debug build (for example, `./run.ps1 editor debug`). Explicit `release` is also accepted.

To open an existing build immediately, pass `no-build`: `./run.ps1 editor no-build` or `./run.ps1 no-build`
for the sandbox (`./run.sh` accepts the same arguments). This skips CMake, compilation and asset cooking;
it runs the last build of the selected configuration. Omit `no-build` to include code and asset changes.

In the sandbox, WASD moves, Shift runs, Space jumps, the mouse looks around and a click throws a ball. F switches the
flashlight, G the shadows, T speeds up time and R resets the scene; Escape frees the mouse, then quits.

The sandbox is a project like any other, `sandbox/Sandbox.spot`, which the editor opens and plays the same way: its
scene is `assets/scenes/playground.scene` and its code is in `src/`. The engine's build also builds that code into the
`sandbox` executable, with the static engine library, and imports its assets beside it with `spot cook`, so the
engine is tried out and debugged with a game.

## The command line

`spot` (the `spot_cli` target) does with projects what the editor does, for scripts and builds that run without it:

```sh
spot new MyGame --path ~/Games    # a project from a template: 3d (the default), component or empty
spot cook MyGame                  # imports the assets that changed; --out <folder> cooks a game's folder
spot build MyGame                 # builds the game's executable and the module the editor loads
spot run MyGame                   # builds, imports and runs the game, showing what it logs
spot export MyGame                # makes the folder the game ships in: builds/<platform>, or --out <folder>
```

A project is its `.spot` file or its folder, the current folder by default. Games are built with the compiler and
the engine library the tools were built with, as the editor builds them.

## Logs and crashes

Applications log to the console and, when `ApplicationConfig::logFile` is set, to that file, line by line, keeping the
previous run's log beside it as `<name>.previous.log`. The editor's is `editor.log` in its settings folder
(`%APPDATA%/Spot/Editor` on Windows). When a program crashes, the crash and the stack that led to it go to the log,
and a report goes to a `crashes` folder beside it, with a minidump on Windows; the editor also says so in a message.

## Game code in the editor

Every project has the same layout: its `<name>.spot` project file, `assets/` with the source assets, `src/` with the
game's C++ code and its `CMakeLists.txt`, `builds/` for exported games and `.spot/` for what the tools generate; the
last two stay out of version control.

Every game has code, if only its `main.cpp`, whose `main()` runs `Spot::Application().Run()`: the application reads
the `game.json` the tools write beside the executable, loads the startup scene it names and draws it. The asset
browser's Create > C++ Component adds a component, as a `.h` and a `.cpp`; components register themselves with
`SPOT_COMPONENT(Type);` in their source file, and list the fields that scenes save and the inspector shows with
`SPOT_FIELDS(speed, health)` in their class. `spot_add_game()` in `src/CMakeLists.txt` builds the code twice from one
compilation: into the game's executable, and into a game module that the editor loads. The editor builds it with
CMake and the compiler that built the editor, whenever it changes or on Ctrl+B, and loads the module without closing
the open scenes, so their entities, selections and undo history stay as they were. Build errors and warnings show in
the console. Games made only in code link the static engine library and need none of this.

Components draw what goes over the game's view, such as a HUD, in `OnDrawUi()`, and first-person games hide and hold
the cursor with `Spot::Input::SetCursorLocked(true)`; both work alike in the game and in the editor's Play, where
Escape hands the mouse and the keys back to the editor, and a click on the view gives them to the game again.

F5 runs the project's game in its own window, its own executable built from its code, from its startup scene, as it
will ship; what the game logs shows in the editor's console.

## Assets

Games load assets through `Spot::Assets` (or `Application::GetAssets()`) from an import folder, set in
`ApplicationConfig::assets`. The tools (`spot_import`, which games never link) import the source folder into the
engine's own binary formats there:

- Every source file gets a `.meta` file beside it with a permanent id and its import settings. Commit both.
- Imports are cached, and run again only when a source, its settings or a file it depends on change.
- A shipped game only needs the import folder.

Games can still load a raw file when they need one, such as a picture the player picks, with `Model::Load()`,
`Image::Load()` and `GraphicsDevice::CreateTexture()`, `Font::Load()` or `Sound::Load()`.

Supported sources: PNG/JPEG/BMP/TGA textures, glTF models (`.gltf`, `.glb`), WAV/Ogg sounds, TrueType/OpenType fonts,
`.material` files and `.scene` files.

## Layout

- `engine/` – the engine library
- `sandbox/` – application for trying out the engine
- `tools/` – the tools that make games out of projects, shared by the editor and the command line
- `cli/` – `spot`, the tools' command line
- `editor/` – the editor, built with Dear ImGui
- `tests/` – unit tests
