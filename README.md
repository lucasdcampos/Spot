# Spot Engine

A 2D/3D game engine library in C++20 for Windows, macOS and Linux.

Spot is made of loosely coupled modules, each usable on its own: drive your own loop, write an application in code,
or ship a data-driven game without C++. The editor and command-line tools are built on the same public API as any game.

## Getting started

Requirements: CMake 3.25+ and a C++20 compiler. Dependencies are fetched and built by CMake. On Linux, install the
X11 and Wayland development packages ([SDL's Linux notes](https://github.com/libsdl-org/SDL/blob/main/docs/README-linux.md)).

```sh
cmake --preset msvc                  # ninja on Linux and macOS
cmake --build --preset msvc-debug    # ninja-debug
ctest --preset msvc-debug            # ninja-debug
```

Run the sample project with `./run.ps1` (Windows) or `./run.sh` (Linux, macOS); add `editor` to open the editor.

## Documentation

- [Manual](docs/manual/index.md): [your first game](docs/manual/getting_started/first_game.md), guides and the
  scripting and command-line reference
- [Internals](docs/internals/index.md): one page per system, starting with the
  [architecture](docs/internals/architecture.md)
- [Examples](examples/): one per way of using the engine

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT; see [LICENSE.txt](LICENSE.txt).
