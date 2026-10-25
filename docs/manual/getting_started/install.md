# Installing Spot

Spot is built from its source. You need CMake 3.25 or newer and a C++20 compiler: Visual Studio 2026 on Windows,
Clang or GCC on Linux and macOS. CMake fetches and builds every other dependency. On Linux, also install the X11 and
Wayland development packages ([SDL's Linux notes](https://github.com/libsdl-org/SDL/blob/main/docs/README-linux.md)).

From the folder you cloned Spot into:

```sh
cmake --preset msvc                      # ninja on Linux and macOS
cmake --build --preset msvc-release      # ninja-release
```

The build makes three programs:

| Program | Where | What it is for |
|---|---|---|
| Editor | `build/msvc/editor/Release/spot_editor` | Making projects: scenes, assets, scripts and playing them |
| Command line | `build/msvc/cli/Release/spot` | The same work without the editor: creating, importing, running and exporting projects |
| Player | `build/msvc/player/Release/spot_player` | Runs games made only of assets and scripts; the editor and the command line use it for you |

On Linux and macOS, they are in `build/ninja` instead of `build/msvc`. Use the Release build to
make games you ship.

## Games with and without C++

A Spot project is a folder with a `.spot` file and its `assets/`. Its game can be made only of scenes, assets and
[Luau](https://luau.org) scripts: it needs no compiler, and the player runs it. A project can also have C++ code in
`src/`, built by CMake against the engine, for what scripts cannot do; you can add code to a project later.

The manual starts with games without code. Next: [your first game](first_game.md).
