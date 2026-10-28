# Sandbox release

Build the engine and tools in Release on each target platform, then export with those tools. `spot export`
builds the sandbox's code, imports source assets and produces the executable, shared engine, compiler runtime
libraries, `game.json` and imported assets. Its sources, `.spot` build, editor and shader tools are not needed
by players. Export keeps versioned Unix runtime libraries and rejects output folders that overlap its inputs.

## Build and export

Windows with the repository's MSVC preset:

```powershell
cmake --preset msvc
cmake --build --preset msvc-release
ctest --preset msvc-release
./build/msvc/cli/Release/spot.exe export sandbox/Sandbox.spot --out build/exports/windows
```

Linux or macOS with Ninja:

```sh
cmake --preset ninja
cmake --build --preset ninja-release
ctest --preset ninja-release
./build/ninja/cli/Release/spot export sandbox/Sandbox.spot --out build/exports/native
```

`spot export` targets the platform/compiler/configuration that built the tools. Build natively on each OS;
the Windows command does not produce Linux or macOS binaries. All shaders are already compiled into the engine.
MSVC redistributable libraries or GCC runtime libraries are copied beside the game. Linux uses `$ORIGIN` and
macOS uses `@loader_path` to locate its engine after relocation. Players need a compatible OS and GPU/audio
drivers; they need no development tools or separately installed compiler runtime.

## Relocation and smoke test

Copy or move the whole exported directory to another location, change into it and run `Sandbox` (`Sandbox.exe`
on Windows). Verify the [play-test checklist](sandbox.md#play-test-checklist) from that copy, including menus,
settings persistence, weapons, tools, physics, sound, animation, particles, saving and the console.

`Sandbox --smoke-test` runs a bounded headless integration sequence against the imported startup scene beside
the executable. It checks the permanent level, camera/aim agreement, inventory and prefab catalog, authored
spawn menu, firing and recoil, checkpoint load during pause, cleanup and npc ragdoll. It uses temporary user
data, exits successfully only when every check passes, and cleans up afterward. Headless GPU assets log that
they need a graphics device; visible rendering is covered separately by the sandbox's offscreen GPU cases.

```powershell
$env:SDL_VIDEO_DRIVER = 'dummy'
./Sandbox.exe --smoke-test
```

```sh
SDL_VIDEO_DRIVER=dummy ./Sandbox --smoke-test
```

The smoke test complements the GPU regressions and an interactive play-through; it does not verify native
desktop integration, audible output, physical gamepads or pixels on a platform without a GPU test run.

## Automated platform exports

The [release workflow](releases.md#the-release-workflow) exports the sandbox on Windows, Linux and macOS with
each release: its jobs build Release, run headless regressions, install the engine, invoke the installed
`spot export`, relocate the result and execute its smoke test. Each release then has the sandbox of each
platform, as `sandbox-<platform>.zip` or `.tar.gz`, preserving executable permissions on Unix. A failing build,
regression, export or smoke test fails the release. The workflow does not replace the interactive checklist.

Local validation on 2026-10-13:

| Platform | Result |
|---|---|
| Windows | Native MSVC Release export passes after relocation with PATH limited to OS directories; Debug MSVC/GCC sandbox GPU cases pass. |
| Linux | Native GCC Release export with X11/ALSA passes after relocation under WSL, including bundled GCC runtimes and `$ORIGIN`; headless regressions pass, with unavailable default-output audio skipped. Local Vulkan rendering remains untested. |
| macOS | Export workflow and `@loader_path` support are ready; a native build and play-through remain pending. |

The interactive checklist remains pending on the three native desktop platforms. Automated smoke and offscreen
checks establish gameplay and rendering regressions separately; they do not claim an interactive platform pass.
