# Spot

A cross-platform 2D/3D game engine written in C++20, built as a static library.

## Building

Requires CMake 3.25+ and a C++20 compiler. Dependencies (SDL3, Catch2) are downloaded and built by CMake.
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

## Running the sandbox

```sh
./run.ps1          # Windows (PowerShell)
./run.sh           # Linux / macOS
```

Pass `release` to run an optimized build.

## Layout

- `engine/` – the engine library
- `sandbox/` – application for trying out the engine
- `tests/` – unit tests
