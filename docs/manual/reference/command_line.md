# Command line

`spot` does to projects what the editor does, for scripts, CI and editors of your own; the installer puts it on your
PATH (see [Installing Spot](../getting_started/install.md)). A project is its `.spot` file or the folder that has
one; the current folder by default.

| Command | What it does |
|---|---|
| `spot new <name> [--path <folder>] [--template 3d\|component\|empty] [--code cpp\|none]` | Creates a project in a new folder named after it, in `--path` or the current folder. `--code none` makes a game without C++, which needs no compiler |
| `spot cook [<project>] [--out <folder>]` | Imports the assets that changed, writing `.meta` files for new ones, and writes the engine's types for Luau's language server into `.spot/luau/`. With `--out`, writes a game that runs into that folder |
| `spot build [<project>]` | Builds the game's C++ code, if it has any |
| `spot run [<project>]` | Builds, imports and runs the game |
| `spot export [<project>] [--out <folder>]` | Makes the folder the game ships in, `builds/<platform>` by default, with its assets packed into `assets.pak` |
| `spot pack <imported folder>` | Packs a folder of imported assets into its `assets.pak`, as `export` does |
| `spot editor [<project>]` | Opens the editor on the project, or on its hub without one |
| `spot version` | Shows Spot's version and where it is installed |
| `spot doctor` | Checks what making games needs: Spot's own files, and CMake for games with C++ |
| `spot update [--channel stable\|nightly]` | Installs the newest release of the channel you installed from, or of the one given |
| `spot help` | Lists the commands |

## Templates

| Template | What it starts with |
|---|---|
| `3d` | A lit scene with a camera, a sky, the ground and a cube (the default) |
| `component` | The 3D scene and a C++ component to start from; needs C++ code |
| `empty` | Nothing |

## Shipping

Export with an installed Spot or the Release build of the tools. The exported folder has everything the game needs,
the engine and its runtime libraries included, and keeps working when moved; players need nothing else besides their
graphics and audio drivers.
