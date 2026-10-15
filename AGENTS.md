# AGENTS.md

Spot is a 2D/3D game engine written in C++20 and built as a static library.
It targets Windows, macOS and Linux.

## Layout

Each project lives in its own folder at the repository root, with headers next to their sources.

- `engine/` – the engine, static library target `spot` (`spot::spot`), also built as the shared library `spot_shared`
  (`spot::shared`) that the editor and the game modules it loads share
- `sandbox/` – the project `sandbox/Sandbox.spot`, which the editor opens too; the engine's build builds its code
  (`sandbox/src/`) into the `sandbox` executable with the static engine, and cooks its assets beside it with `spot`
- `tests/` – unit tests using Catch2
- `tools/` – `spot_tools` (`spot::tools`), what makes games out of projects (`Project`), shared by the editor and the
  command line; built against the engine's headers only, so whatever links it links one engine library itself
- `cli/` – `spot` (target `spot_cli`), the tools' command line: `new`, `cook`, `build`, `run` and `export` projects
- `editor/` – the editor (`spot_editor`), a Dear ImGui application; everything but `main.cpp` is the
  `spot_editor_core` library its tests link

## Build and test

```sh
cmake --preset msvc                  # or: cmake --preset ninja
cmake --build --preset msvc-debug    # or: ninja-debug
ctest --preset msvc-debug            # or: ninja-debug; headless tests, two processes at most
```

The normal test presets exclude GPU tests. Use them for routine changes to gameplay, assets, serialization,
physics, audio, tools and headless editor behavior. Run the relevant GPU tests when changing rendering, shaders,
graphics backends or visual editor behavior; use `ctest --preset msvc-debug-gpu` (or `ninja-debug-gpu`) for that
suite, and `-R <pattern>` to select cases. GPU tests open real windows and are marked `RUN_SERIAL`, so only one
runs at a time even when parallelism is requested. `ctest --preset msvc-debug-all` (or `ninja-debug-all`) explicitly
runs the complete suite. Release presets have the same `-gpu` and `-all` variants. Avoid repeating graphical
suites for unrelated changes or running MSVC and GCC GPU suites concurrently.
For a build directory without presets, use `ctest --test-dir <build> -LE gpu --parallel 2` for routine tests, or
`-L gpu --parallel 1` for the graphical suite.

With the Ninja or Makefile generators, the build compiles through sccache or ccache when one is installed (see
`cmake/spot_compiler_cache.cmake`; `-DSPOT_USE_COMPILER_CACHE=OFF` turns it off), and so do games built by the tools.
The Visual Studio generator runs no compiler cache.

Shaders are HLSL files named `*.vert.hlsl` or `*.frag.hlsl`. `spot_add_shaders(<target> ...)` (in
`cmake/spot_shaders.cmake`) compiles them to SPIR-V, MSL and, on Windows, DXIL, and generates a header per shader
with a ready-to-use `Spot::ShaderDesc`.

To build and run the sandbox: `./run.ps1` (PowerShell) or `./run.sh` (Linux, macOS, Git Bash). Both accept
`editor` to run the editor instead, and `release` for an optimized build.

## Documentation

`docs/` explains each system of the engine and the tools, starting from `docs/index.md`; the headers stay the
reference for each function.

## Roadmap

`ROADMAP.md` plans the engine's next features, a checkbox per step: the next task is the first unchecked step, and
the commit that finishes a step ticks its box. Follow its architecture principles and its "How to use this plan".

## Rules

- Everything in the repository is in English: code, comments, docs and commit messages.
- Code must build on Windows, macOS and Linux. Keep platform-specific code isolated.
- Backends are only used in their own folders: windowing (SDL3, `SPOT_PLATFORM`) under `engine/platform/<backend>/`,
  graphics (SDL_GPU, `SPOT_GRAPHICS`) under `engine/graphics/<backend>/`, audio (SDL3, `SPOT_AUDIO`) under
  `engine/audio/<backend>/` and physics (Jolt, `SPOT_PHYSICS`) under `engine/physics/<backend>/`. Public headers never
  expose backend types, so a backend can be replaced without changing the engine API. Audio backends only deliver what
  the engine's mixer produces, and physics backends only simulate the bodies the engine's physics world asks for.
- Engine features built on top of the graphics API (such as the 2D renderer) only use its public interface, so
  users can always do anything the engine does.
- The engine is layers of folders (see "Layers" in `docs/index.md`): core (`core/`, `math/`), platform, systems
  (`graphics/`, `audio/`, `physics/`, `assets/`…), scene (`scene/`, a folder per system's components), runtime
  (`runtime/`, `Application`) and import. A folder only includes its own layer and the ones below;
  `cmake/spot_check_layers.cmake` runs as a test. Systems keep their mechanism usable without scenes, so a game can
  run a loop and an object model of its own; their scene components go in `scene/<system>/`.
- GPU tests go in `tests/gpu_tests.cpp` (the `spot_gpu_tests` executable) and are skipped where no GPU is available.
  Golden-image tests (`tests/golden_tests.cpp`, same executable) compare reference scenes with the images in
  `tests/golden/`; when a change to how things look is meant, run them with `SPOT_UPDATE_GOLDEN=1`, look at the new
  images, and commit them with the change.
- Sandbox tests go in `tests/sandbox/` (`spot_sandbox_tests`): the sandbox's game code on its real scenes, headless,
  driven by simulated input, so a regression shows as something broken in the game.
- Editor tests go in `tests/editor/`: `spot_editor_tests` (headless ImGui, no GPU), `spot_editor_gpu_tests` and
  `spot_editor_module_tests` (building projects' code with CMake, which takes a while).
- Importing is for tools: the asset database and importers are `spot_import` (`spot::import`, in `engine/import/`),
  which games never link; they load imported assets, or raw files through `Model::Load()`, `Image::Load()` and the like.
- Projects are a tools concept: the engine never knows about them. A game runs from the `GameSettings`
  (`game.json`) its build derives from the project, and from its imported assets.
- The editor uses only the engine's public API, like any game; it never reaches a backend. ImGui stays in the
  editor, so the runtime never depends on it. The editor never keeps pointers to entities or components across
  frames: it refers to them by `EntityId`, so undo history and game code reloads stay valid.
- The build must have no warnings.
- New engine features come with tests in `tests/`.
- Features keep their page in `docs/` up to date in the same commit: how the system works, why, and where to start.
  New systems get a page, linked from `docs/index.md`.
- Assets (`engine/assets/`) are loaded from imported files, not raw sources. Every source asset has a `.meta` file
  beside it, which is committed with it; never edit ids in them by hand. New source formats get an importer in
  `engine/import/importers/`, and a change to any imported format bumps its importer's version.

## Code style

Formatting is defined by `.clang-format`; format C++ files with clang-format before committing.

- Files and folders: `snake_case`. Headers use `.h`, sources use `.cpp`.
- Types (classes, structs, enums, aliases), functions and methods: `PascalCase`.
- Getters use `Get` (`GetWidth()`), boolean queries use `Is`/`Has` (`IsRunning()`).
- Private and protected members: `m_camelCase`. Static members: `s_camelCase`.
- Public fields of plain data structs: `camelCase` (`position.x`).
- Local variables and parameters: `camelCase`.
- Constants and enum values: `PascalCase`. Always use `enum class`.
- Macros: `SPOT_UPPER_SNAKE_CASE`.
- Namespaces: `PascalCase` (`Spot`).
- Headers start with `#pragma once`. Include with quotes inside the engine, `<spot.h>` outside.
- Comment only when necessary, and explain why, not what.
