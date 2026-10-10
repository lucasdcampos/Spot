# Tools and the command line

The tools make games out of projects. They are `spot_tools` (`spot::tools`, `tools/`), shared by the editor and the
command line, and `spot` (target `spot_cli`, `cli/`), the command line itself. The tools are built against the
engine's headers only, so whatever links them links one engine library itself.

## Projects

Projects are a tools concept: the engine never knows about them. A `Project` (`tools/project/project.h`) is a folder
with this layout:

```
<name>.spot   the project file: name, version, startup scene and settings sections
assets/       the source assets, each beside its .meta file
src/          the game's C++ code, with its CMakeLists.txt and main.cpp
builds/       games exported from the editor or with `spot export`
.spot/        what is generated from the rest: imported assets, the code's build, the editor's state
```

The last two are not meant for version control. `ProjectSettings` holds what the project file stores, including the
settings sections (see [Application and scenes](application_and_scenes.md)); `Project::GetGameSettings()` derives the
`GameSettings` a game runs from. `CreateProject()` (`tools/project/project_templates.h`) makes new projects from
templates: a 3D scene, a component to start from, or nothing.

## From a project to a game

- **Importing**: `ImportProjectAssets()` imports the sources that changed into `.spot/imported` (see
  [Assets and importing](assets_and_importing.md)).
- **Building**: `CodeBuild` builds the game's code with CMake, in the background, with the compiler, generator and
  configuration that built the tools, against their own build of the shared engine library. The code's
  `CMakeLists.txt` calls `spot_add_game()`, from the engine's CMake package, which builds both the game's executable
  and the game module the editor loads, precompiling `<spot.h>` once for every file of the game's code, and
  compiling through sccache or ccache when one is installed and the generator runs one (Ninja and Makefiles do,
  Visual Studio does not), as the engine's own build does. A build configures again by itself when a CMake file
  changed or a file of code came or went, and builds only the module when that is all it needs, as the editor's
  builds on change do; running the game builds its executable too. Each build logs how long its phases took (`CodeBuild::GetPhases()`):
  configuring, compiling and linking, told apart where the build tool says when it links. The editor also logs how
  long loading the module and reloading the scenes took, and how long after the code changed.
- **Cooking**: `CookGame()` imports the sources into `imported/` in a folder and writes `game.json` there, so a game's
  executable in that folder runs the game. The engine's own build cooks the sandbox this way.
- **Running**: `WriteRunSettings()` writes `game.json` beside the built executable, pointing at the project's imports,
  and `GameRunner` runs it in its own window, logging what it logs as "Game".
- **Exporting**: `ExportGame()` makes the folder the game ships in: its executable named after the project, the
  engine library, its imported assets and `game.json`.

## The command line

```
spot new <name> [--path <folder>] [--template 3d|component|empty]
spot cook [<project>] [--out <folder>]
spot build [<project>]
spot run [<project>]
spot export [<project>] [--out <folder>]
spot help
```

A project is its `.spot` file or the folder that has one; the current folder by default. `cook` imports what
changed, and with `--out` cooks the game into that folder. `build` builds the game's executable and module. `run`
builds, imports and runs the game. `export` builds the game and makes its shipping folder, `builds/<platform>` by
default.

## The sandbox

`sandbox/` is a project like any other (`sandbox/Sandbox.spot`): the editor opens it, and `spot` builds it. The
engine's own build also builds its code into the `sandbox` executable with the static engine, and cooks its assets
beside it, so the engine is tried out and debugged with a game: `./run.ps1` or `./run.sh` builds and runs it.

### Generated assets

The sandbox's props are made by code: `sandbox_generator` (`sandbox/generator/`, built with the engine) writes their
glTF models (with cgltf's writer, from `MeshData`) and PNG textures (painted pixel by pixel, encoded with
`Image::EncodePng()`) into `sandbox/assets/generated/`: the props, each colored by its own texture with the normal and
ORM maps of its surface; a PBR texture set per surface (wood, painted metal, rusty metal, concrete, rubber, glass),
whose ORM texture packs occlusion, roughness and metallic as glTF allows; and the low-poly models of what the player
holds, made of boxes and cylinders of those surfaces. Fixed seeds make every run write the same files, and it only
rewrites files whose content changed. The files are committed and imported like any artist's assets; their `.meta`
files come from the import, and the generator never touches them. Models import their images as colors or data by
how their materials use them, but the surfaces' normal and ORM textures are also imported on their own, for the
level's `.material` files, so their `.meta` files have `srgb` off. After changing the generator, run it and let the
sandbox's build import what changed:

```sh
./build/msvc/sandbox/generator/Debug/sandbox_generator    # or with a folder to write into
```
