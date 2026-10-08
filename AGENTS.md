# AGENTS.md

Spot is a 2D/3D game engine written in C++20 and built as a static library.
It targets Windows, macOS and Linux.

## Layout

Each project lives in its own folder at the repository root, with headers next to their sources.

- `engine/` – the engine, static library target `spot` (`spot::spot`), also built as the shared library `spot_shared`
  (`spot::shared`) that the editor and the game modules it loads share
- `sandbox/` – executable that links the engine, used to try it out
- `tests/` – unit tests using Catch2
- `editor/` – the editor (`spot_editor`), a Dear ImGui application; everything but `main.cpp` is the
  `spot_editor_core` library its tests link

## Build and test

```sh
cmake --preset msvc                  # or: cmake --preset ninja
cmake --build --preset msvc-debug    # or: ninja-debug
ctest --preset msvc-debug            # or: ninja-debug
```

Shaders are HLSL files named `*.vert.hlsl` or `*.frag.hlsl`. `spot_add_shaders(<target> ...)` (in
`cmake/spot_shaders.cmake`) compiles them to SPIR-V, MSL and, on Windows, DXIL, and generates a header per shader
with a ready-to-use `Spot::ShaderDesc`.

To build and run the sandbox: `./run.ps1` (PowerShell) or `./run.sh` (Linux, macOS, Git Bash). Both accept
`editor` to run the editor instead, and `release` for an optimized build.

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
- GPU tests go in `tests/gpu_tests.cpp` (the `spot_gpu_tests` executable) and are skipped where no GPU is available.
- Editor tests go in `tests/editor/`: `spot_editor_tests` (headless ImGui, no GPU), `spot_editor_gpu_tests` and
  `spot_editor_module_tests` (building projects' code with CMake, which takes a while).
- The editor uses only the engine's public API, like any game; it never reaches a backend. ImGui stays in the
  editor, so the runtime never depends on it. The editor never keeps pointers to entities or components across
  frames: it refers to them by `EntityId`, so undo history and game code reloads stay valid.
- The build must have no warnings.
- New engine features come with tests in `tests/`.
- Assets (`engine/assets/`) are loaded from imported files, not raw sources. Every source asset has a `.meta` file
  beside it, which is committed with it; never edit ids in them by hand. New source formats get an importer in
  `engine/assets/importers/`, and a change to any imported format bumps its importer's version.

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
