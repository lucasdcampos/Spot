# Installing Spot

The installer downloads the newest release of Spot and puts its command line, `spot`, on your PATH. On Windows, in
PowerShell:

```powershell
irm https://raw.githubusercontent.com/lucasdcampos/Spot/main/scripts/install.ps1 | iex
```

On Linux and macOS:

```sh
curl -fsSL https://raw.githubusercontent.com/lucasdcampos/Spot/main/scripts/install.sh | sh
```

Then open a new terminal and check it worked:

```sh
spot version
spot doctor
```

Spot is installed in `%LOCALAPPDATA%\Spot` on Windows and `~/.spot` elsewhere. Releases are made for Windows (x64),
Linux (x64, Ubuntu 24.04 or newer) and macOS (Apple silicon, macOS 14 or newer); elsewhere, install from source.

## Options

| Windows | Linux and macOS | What it does |
|---|---|---|
| `-Channel nightly` | `--channel nightly` | Follows the nightly releases, built from the newest source every night, instead of the stable ones |
| `-Version 0.1.0` | `--version 0.1.0` | Installs that release |
| `-Prefix <folder>` | `--prefix <folder>` | Installs into that folder |
| `-FromSource [-Source <checkout>]` | `--from-source [<checkout>]` | Builds Spot from its source instead of downloading it |
| `-NoPath` | `--no-path` | Leaves your PATH as it is |
| `-Force` | `--force` | Installs again, even the version you have |

To pass options to the installer from the web:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/lucasdcampos/Spot/main/scripts/install.ps1))) -Channel nightly
```

```sh
curl -fsSL https://raw.githubusercontent.com/lucasdcampos/Spot/main/scripts/install.sh | sh -s -- --channel nightly
```

## Updating

```sh
spot update                     # the newest release of the channel you installed from
spot update --channel nightly   # switches to the nightly releases
```

It does nothing when you have the newest already. Close the editor before updating.

## Installing from source

From a clone of the repository, the installer builds Spot and installs it as a release would be:

```sh
./scripts/install.sh --from-source       # Linux and macOS
./scripts/install.ps1 -FromSource        # Windows
```

Run from the web with that option, it clones Spot first. Building needs Git, CMake 3.25 or newer and a C++20 compiler:
Visual Studio 2026 on Windows, Clang or GCC with Ninja on Linux and macOS. CMake fetches and builds every other
dependency. On Linux, also install the X11 and Wayland development packages
([SDL's Linux notes](https://github.com/libsdl-org/SDL/blob/main/docs/README-linux.md)). To update, run it again.

## What you get

| Program | What it is for |
|---|---|
| Editor (`spot editor`) | Making projects: scenes, assets, scripts and playing them |
| Command line (`spot`) | The same work without the editor: creating, importing, building, running and exporting projects (see the [command line](../reference/command_line.md)) |
| Player | Runs games made only of assets and scripts; the editor and the command line use it for you |

## Games with and without C++

A Spot project is a folder with a `.spot` file and its `assets/`. Its game can be made only of scenes, assets and
[Luau](https://luau.org) scripts: it needs no compiler, and the player runs it. A project can also have C++ code in
`src/`, built by CMake against the engine, for what scripts cannot do; you can add code to a project later. Games with
C++ need CMake 3.25 or newer and a C++20 compiler: Visual Studio 2022 or newer on Windows, GCC 13 or Clang 17 on Linux,
Xcode's on macOS. `spot doctor` tells whether Spot finds them.

The manual starts with games without code. Next: [your first game](first_game.md).
