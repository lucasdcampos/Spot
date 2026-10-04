# AGENTS.md

Spot is a 2D/3D game engine written in C++20 and built as a static library.
It targets Windows, macOS and Linux.

## Layout

Each project lives in its own folder at the repository root, with headers next to their sources.

- `engine/` – the engine, static library target `spot` (`spot::spot`)
- `sandbox/` – executable that links the engine, used to try it out
- `tests/` – unit tests using Catch2
- `editor/` – reserved for the future editor

## Build and test

```sh
cmake --preset msvc                  # or: cmake --preset ninja
cmake --build --preset msvc-debug    # or: ninja-debug
ctest --preset msvc-debug            # or: ninja-debug
```

To build and run the sandbox: `./run.ps1` (PowerShell) or `./run.sh` (Linux, macOS, Git Bash).
Both accept `release` as an argument.

## Rules

- Everything in the repository is in English: code, comments, docs and commit messages.
- Code must build on Windows, macOS and Linux. Keep platform-specific code isolated.
- The build must have no warnings.
- New engine features come with tests in `tests/`.

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
