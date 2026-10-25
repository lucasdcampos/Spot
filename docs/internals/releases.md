# Releases and installation

Spot ships as an **installed engine**: the tools, the editor, the player and what games' code builds against, in one
folder that works wherever it is put. Releases are archives of it, made on each platform by the release workflow; the
installers download one, or build the source into the same layout, and put `spot` on the PATH.

## The installed layout

`cmake/spot_install.cmake` lays it out, as the `spot` component of the build's install rules; the dependencies' own
install rules are left out by installing only that component:

```sh
cmake --install build/msvc --config Release --component spot --prefix <folder>
```

```
bin/           spot, spot_editor and the shared engine library; the MSVC runtime on Windows
player/        spot_player with only what ships beside it: the engine library and the compiler's runtime
include/       the engine's headers, without its backends' (sdl3/, nvrhi/...) or the importers'
lib/           the engine library's import library on Windows, and the engine's CMake package in cmake/Spot
libexec/spot/  glslang and SPIRV-Cross, which spot_add_shaders() runs for games' shaders
share/spot/    the engine's shader headers, as surface_lighting.hlsl
version.txt    the version, with its nightly suffix
install.txt    written by the installers: the channel followed and whether it was downloaded or built
```

The player has a folder of its own because exports copy every library beside it (see
[Tools and the command line](tools_and_command_line.md#from-a-project-to-a-game)). On Linux and macOS, programs find
the engine library beside themselves (`$ORIGIN`, `@loader_path`), so `bin/` holds it too.

### How installed tools find the engine

The tools know they are installed when `lib/cmake/Spot/SpotConfig.cmake` is beside the folder they run from
(`Installation::GetCurrent()`, `tools/install/installation.h`). `Toolchain::GetCurrent()` then builds games' code
against that package, links the library in `bin/` and runs the player in `player/`, wherever the folder was moved.
Run from the engine's build directory, the tools use that build, as before (`Toolchain::GetBuilt()`).

The compiler is the hard part: game code and the engine library share C++ types, so they must be built by compatible
compilers in the same configuration. `Toolchain::GetInstalled()` keeps the toolchain that built the tools when it is
on the machine, as for an install from source. Released tools were built on CI machines, whose CMake and compiler are
not there; they then build with the CMake found on the PATH (or Visual Studio's own, or Homebrew's), with CMake's
default generator on Windows, which picks the newest Visual Studio, and Ninja elsewhere when there is one. The
configuration stays the engine's, Release, since MSVC's debug and release runtimes do not mix. `spot doctor` shows
what was found.

The installed package, `cmake/SpotInstalledConfig.cmake.in`, finds everything from its own folder; the build tree's,
`cmake/SpotConfig.cmake.in`, points into the build. Both set the locations and include `cmake/spot_game.cmake`, which
has `spot_add_game()` and is installed with the CMake files it needs. Games copy the runtime libraries of the compiler
that builds them, found when their code is configured, since that may not be the engine's.

## The release workflow

`.github/workflows/release.yml` makes two kinds of release:

- **Stable**: pushing a tag `v<version>` releases that version. The tag must be the `project()` version of
  `CMakeLists.txt`, so bump it first.
- **Nightly**: every night, when `main` changed since the last one, and when run by hand. Its version is the project's
  with `-nightly.<date>+<commit>` (`SPOT_VERSION_SUFFIX`), and it replaces the one prerelease tagged `nightly`.

Each platform's job (Windows x64, Linux x64, macOS arm64) builds Release with Ninja, runs the headless tests, installs
into a staging folder, then uses that installed `spot` to export the sandbox, which builds its code against the
installed package, and runs the moved export's `--smoke-test` (see [Sandbox release](sandbox_release.md)). The release
has, per platform, `spot-<platform>.zip` or `.tar.gz` and `sandbox-<platform>.zip` or `.tar.gz`, each with a
`.sha256`, plus `version.txt` and the installers. Assets are named without the version, so
`releases/latest/download/<asset>` always reaches the newest stable one.

Linux releases are built on Ubuntu 24.04, so they need its glibc or newer. macOS releases need macOS 14.

## The installers

`scripts/install.ps1` (Windows) and `scripts/install.sh` (Linux, macOS) do the same:

1. Downloading: fetch `version.txt` of the release (the newest stable, the nightly, or `--version`'s), stop if that
   version is installed, else download the archive, check its SHA-256 and extract it.
2. Or, with `--from-source` (`-FromSource`), build a checkout: the one given, the one the script is in, or a clone
   in `<prefix>-source`. It builds only what installs, with the repository's presets, so it shares `build/msvc` or
   `build/ninja` with working on the engine, then installs the `spot` component into a staging folder.
3. Replace the prefix with the new files. They refuse a prefix that has files but no installation, so a wrong
   `--prefix` deletes nothing. On Windows, files in use, such as `spot.exe` while `spot update` runs it, cannot be
   deleted but can be renamed, so they are moved aside as `*.old` and deleted by the next install. On Unix, the old
   folder is moved away and deleted; running programs keep their files until they exit.
4. Write `install.txt` and add `bin/` to the PATH: the user's `Path` in the registry on Windows, kept unexpanded;
   the shell's startup file elsewhere (`.zshrc`, `.bashrc` or `.bash_profile` on macOS, fish's `conf.d`, or
   `.profile`), once.

`spot update` runs the installer of its platform from `main`, with the installation's channel (or `--channel`'s) and
prefix, so updating is installing again. Installations built from source are updated by running the installer with
`--from-source` again. Managing several versions side by side is left to a version manager to come.
