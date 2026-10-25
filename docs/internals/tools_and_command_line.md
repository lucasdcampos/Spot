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
src/          the game's C++ code, with its CMakeLists.txt and main.cpp, for games that have code of their own
builds/       games exported from the editor or with `spot export`
.spot/        what is generated from the rest: imported assets, the code's build, the editor's state
```

The last two are not meant for version control. `ProjectSettings` holds what the project file stores, including the
settings sections (see [Application and scenes](application_and_scenes.md)); `Project::GetGameSettings()` derives the
`GameSettings` a game runs from. `CreateProject()` (`tools/project/project_templates.h`) makes new projects from
templates: a 3D scene, a component to start from, or nothing.

### Projects with and without code

A project has C++ code when its code folder has a build script, `src/CMakeLists.txt` (`Project::HasCode()`); nothing
else records it. New projects get code or not (`ProjectCode`), and templates about the code, such as the component
one, need it. Games without code are made of assets and scripts: they run with the **player**, `spot_player`
(`player/`), an executable built with the engine that runs whatever game it is given, so making them takes no CMake or
compiler and nothing is built before they run. `AddProjectCode()` gives such a project the code every project with code
starts with, as adding a C++ component does; deleting `src/` makes it a project without code again.

The player runs the game whose `game.json` is beside it, as a shipped game, or the one in the folder after `--game`,
whose import directory is relative to that folder. The tools find it beside their build of the engine
(`Toolchain::player`), with the engine library and compiler runtime libraries beside it, or in the `player/` folder
of an installed engine (see [Releases and installation](releases.md)).

## From a project to a game

- **Importing**: `ImportProjectAssets()` imports the sources that changed into `.spot/imported` (see
  [Assets and importing](assets_and_importing.md)).
- **Building**: `CodeBuild` builds the game's code with CMake, in the background, with the compiler, generator and
  configuration that built the tools, against their own build of the shared engine library; installed tools build
  against the installed engine, with the CMake of the machine they are on when what built them is not there
  (`Toolchain::GetCurrent()`, see [Releases and installation](releases.md#how-installed-tools-find-the-engine)). The code's
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
- **Running**: `PrepareGameRun()` writes `game.json`, pointing at the project's imports, and tells what to start
  (`GameLaunch`): a game with code runs its built executable, with `game.json` beside it; one without runs the player
  with `--game .spot/run`, since every game without code shares it. `GameRunner` runs it in its own window, logging
  what it logs as "Game".
- **Exporting**: `ExportGame()` makes the folder the game ships in: its executable named after the project, the
  engine library, compiler runtime libraries, its imported assets and `game.json`. Games without code ship the player
  as their executable, with the libraries beside it. Export destinations that overlap
  the project's source, imports or code build are rejected before any files are replaced.

## The command line

From a checkout, `./run.ps1 cli <arguments>` or `./run.sh cli <arguments>` builds `spot` and runs it with them, in
the current folder (`debug` and `no-build` go before `cli`).

```
spot new <name> [--path <folder>] [--template 3d|component|empty] [--code cpp|none]
spot cook [<project>] [--out <folder>]
spot build [<project>]
spot run [<project>]
spot export [<project>] [--out <folder>]
spot pack <imported folder>
spot editor [<project>]
spot version
spot doctor
spot update [--channel stable|nightly]
spot help
```

A project is its `.spot` file or the folder that has one; the current folder by default. `cook` imports what
changed and writes the engine's API for Luau's language server into `.spot/luau/` (`WriteLuauTypes()`, see
[scripting](scripting.md#writing-scripts-in-other-editors)), and with `--out` cooks the game into that folder. `new --code none` makes a project without code. `build` builds the game's executable and module, and
does nothing for a game without code; `run` and `export` skip building those too. `run`
builds, imports and runs the game. `export` builds the game and makes its shipping folder, `builds/<platform>` by
default, with the imported assets packed into one `assets.pak` beside their manifest. `pack` packs a folder of
imported assets the same way (see [Assets and importing](assets_and_importing.md)). `editor` launches the editor
installed beside `spot`, or the one of its build, on the project given (its file, or the folder that has it) or on its
hub. `version` and `doctor` tell what the tools are, where they are and what building games needs; `update` runs the
installer again (see [Releases and installation](releases.md#the-installers)).

Use the Release build of the tools for shipping: it builds the game's code against that same Release engine.
The generated game CMake package copies the MSVC redistributable runtime or the GCC runtime libraries beside
the executable. Versioned Unix libraries such as `libstdc++.so.6` are included when exporting. Linux searches
`$ORIGIN` and macOS searches `@loader_path`, so moving the complete export keeps its engine reachable. The
operating system and graphics/audio drivers remain platform prerequisites. The shipped shaders are embedded;
players need no CMake, compiler, shader tools, source assets or project files. See [Sandbox release](sandbox_release.md)
for export commands, relocation testing and platform acceptance.

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
rewrites files whose content changed. The sandbox's build runs it (`sandbox_generated_assets`, before its assets are
imported) whenever the generator is rebuilt, so changing its code is all it takes. Its files are imported like any
artist's assets, but only their `.meta` files are committed: the files themselves are ignored by git, since the
generator makes them again exactly, and keeping every version of its images would swell the repository. The `.meta`
files hold the ids that materials, prefabs and scenes refer to; they come from the import, the generator never touches
them, and the asset database keeps one whose file is missing until the file comes back. Models import their images as
colors or data by how their materials use them, but the surfaces' normal and ORM textures are also imported on their
own, for the level's `.material` files, so their `.meta` files have `srgb` off. The generator can also be run by hand,
as after deleting its files, whose stamp in the build would otherwise keep it from running:

```sh
./build/msvc/sandbox/generator/Debug/sandbox_generator    # or with a folder to write into
```
