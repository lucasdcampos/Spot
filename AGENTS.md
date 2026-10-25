# AGENTS.md

Spot is a 2D/3D game engine library in C++20 for Windows, macOS and Linux. The engine (`engine/`) is modules in
layers, each usable from code on its own; the editor and the tools are programs built on it, like any game.

## Layout

- `engine/` – the engine, its modules in `engine/spot/`: static library `spot` (`spot::spot`), and shared library
  `spot_shared` for the editor and the game code it loads
- `editor/` – the editor (`spot_editor`, Dear ImGui); everything but `main.cpp` is `spot_editor_core`
- `tools/` – `spot_tools`, which makes games out of projects, for the editor and the command line
- `cli/` – `spot`, the command line: `new`, `cook`, `build`, `run` and `export`
- `player/` – `spot_player`, which runs games without C++ code from their `game.json`
- `examples/` – one small program per way of using the engine, run by the tests
- `sandbox/` – the sample project (`Sandbox.spot`), built into the `sandbox` executable
- `tests/` – Catch2 tests
- `docs/` – `manual/` for making games with Spot, `internals/` a page per system for working on it

## Build and test

```sh
cmake --preset msvc                  # or: ninja
cmake --build --preset msvc-debug    # or: ninja-debug
ctest --preset msvc-debug            # headless tests
ctest --preset msvc-debug-gpu        # GPU tests, when rendering, shaders or graphics backends change
```

GPU tests open real windows and run one at a time: run the relevant ones (`-R <pattern>`) only for changes that can
affect what is drawn, and never two GPU suites at once. When a graphics backend or a shader changes, run the whole GPU
suite on each backend (`msvc-debug-gpu-sdl-gpu`, `msvc-debug-gpu-vulkan`). The `-all` presets run everything; Release
presets have the same variants. `./run.ps1` or `./run.sh` build and run the sandbox (`editor` runs the editor, `debug` a
Debug build, `no-build` skips building).

Shaders are HLSL (`*.vert.hlsl`, `*.frag.hlsl`), compiled by `spot_add_shaders()` (`cmake/spot_shaders.cmake`) into
a header per shader with a ready-to-use `Spot::ShaderDesc`.

## Rules

- Everything in the repository is in English: code, comments, docs and commit messages.
- Code builds on Windows, macOS and Linux without warnings. Platform-specific code stays isolated.
- The modules of `engine/spot/` may depend on each other, but only where they need to: keep them as loosely coupled
  as makes sense, and put each thing where its concept belongs (see "Modules" in `docs/internals/index.md`). Systems work
  without scenes; their components go in `scene/<system>/`.
- Backends (SDL3, SDL_GPU, NVRHI, Jolt, Box2D) are used only in their system's folder, `engine/<system>/<backend>/`.
  Public headers never expose backend types.
- Engine features built on the graphics API use only its public interface, so users can do anything the engine does.
- The editor uses only the engine's public API, and ImGui stays in the editor. The editor refers to entities by
  `EntityId`, never by pointers kept across frames.
- Games never link `spot_import` (importers and the asset database), and the engine never knows about projects: a
  game runs from its `game.json` and its imported assets.
- Every source asset has a `.meta` file beside it, committed with it; never edit its ids by hand. New source formats
  get an importer in `engine/spot/import/importers/`, and a change to an imported format bumps its importer's version.
- Scripting API changes update `ScriptApi::Types` (`engine/spot/scene/scripting/script_api.h`), the editor's Luau
  highlighting and its tests, the scripts they affect, the sandbox's included, and the generated scripting reference
  (run the tests with `SPOT_UPDATE_DOCS=1`), in the same change.

## Tests

- New engine features come with tests in `tests/`.
- GPU tests go in `tests/gpu_tests.cpp` and are skipped where no GPU is available. Golden-image tests
  (`tests/golden_tests.cpp`) compare scenes with `tests/golden/`: when a change to how things look is meant, run them
  with `SPOT_UPDATE_GOLDEN=1`, look at the new images and commit them with the change.
- Sandbox tests (`tests/sandbox/`) run the sandbox's game code on its real scenes, headless, with simulated input.
- Editor tests go in `tests/editor/`: headless (`spot_editor_tests`), GPU (`spot_editor_gpu_tests`) and project code
  builds (`spot_editor_module_tests`, slow).

## Documentation

`docs/manual/` is for people making games: tutorials, guides by task and reference, in terms of what they do, not
of the engine's code. `docs/internals/` is for people working on Spot: how each system works, why, and where to
start. A change to a system updates its internals page in the same commit, and the manual when it changes what games
see; new systems get a page, linked from `docs/internals/index.md`. Tutorials have their finished project in
`examples/`, played by the tests. Headers stay the reference for each function.

## Code style

`.clang-format` formats C++ files; run clang-format before committing.

- Files and folders `snake_case`; headers `.h` starting with `#pragma once`, sources `.cpp`. Include with quotes
  inside the engine (`"spot/core/debug/log.h"`), `<spot.h>` or `<spot/core/debug/log.h>` outside.
- Types, functions and methods `PascalCase`; getters `Get…`, boolean queries `Is…`/`Has…`.
- Private and protected members `m_camelCase`, static members `s_camelCase`; public fields of plain structs, locals
  and parameters `camelCase`.
- Constants and enum values `PascalCase`, always `enum class`; macros `SPOT_UPPER_SNAKE_CASE`; namespaces
  `PascalCase` (`Spot`).
- Comment only when necessary, and explain why, not what.
