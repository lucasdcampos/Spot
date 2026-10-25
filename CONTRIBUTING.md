# Contributing

Read [docs/internals/architecture.md](docs/internals/architecture.md) first; most rules below follow from its loosely coupled modules.

## Before submitting

- Headless tests pass: `ctest --preset msvc-debug` (`ninja-debug` on Linux and macOS).
- Changes to rendering, shaders or graphics backends also pass the GPU tests: `ctest --preset msvc-debug-gpu`
  (filter with `-R <pattern>`). For backend or shader changes, run the full suite on each backend
  (`msvc-debug-gpu-sdl-gpu`, `msvc-debug-gpu-vulkan`). GPU suites open windows; never run two at once.
- The code builds without warnings on Windows, macOS and Linux.
- C++ files are formatted with clang-format.

## Rules

- Everything is in English: code, comments, docs and commit messages.
- Modules in `engine/spot/` may depend on each other only where they need to; keep them loosely coupled and put
  each thing where its concept belongs.
- Backends (SDL3, SDL_GPU, NVRHI, Jolt, Box2D) stay in `engine/<system>/<backend>/`; public headers never expose them.
- The editor uses only the public engine API; ImGui stays in the editor.
- Every source asset has a committed `.meta` file; never edit its ids by hand. Changes to an imported format bump the
  importer's version.
- Scripting API changes update `ScriptApi::Types`, the editor's Luau highlighting and its tests, and affected scripts
  in the same change.

## Tests and docs

- New features come with tests in `tests/` (GPU tests in `tests/gpu_tests.cpp`, editor tests in `tests/editor/`).
- For intended visual changes, regenerate golden images with `SPOT_UPDATE_GOLDEN=1`, review them and commit them.
- A change to a system updates its page in [docs/internals/](docs/internals/index.md), and the
  [manual](docs/manual/index.md) when it changes what games see; new systems get a page linked from the index.
- Scripting API changes regenerate the manual's scripting reference: run the tests with `SPOT_UPDATE_DOCS=1`.

## Code style

| Element | Convention |
| --- | --- |
| Files and folders | `snake_case`; `.h` (with `#pragma once`) and `.cpp` |
| Types, functions, methods | `PascalCase`; `Get…`, `Is…`/`Has…` |
| Private/protected members | `m_camelCase`; static `s_camelCase` |
| Locals, parameters, plain struct fields | `camelCase` |
| Constants, enum values | `PascalCase`; always `enum class` |
| Macros | `SPOT_UPPER_SNAKE_CASE` |

Comment only when necessary, explaining why rather than what.

## Commits

Keep commits small, with a short imperative subject, e.g. "Add a script that builds the docs into one HTML file".
