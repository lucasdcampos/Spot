# Spot internals

How Spot works, one page per system, for those who work on the engine. The headers stay the reference for every
function; these pages explain how the pieces fit together, why they are built that way, and where to start. Making
games with Spot is in the [manual](../manual/index.md).

Spot is a 2D/3D game engine written in C++20. A game is a project (`.spot`) with assets and C++ code; the tools
import its assets, build its code against the engine, and run or export it. The editor is a Dear ImGui application
that does the same with a window around it.

Where to start: the [examples](../../examples/), one per way of using the engine, each built and run by the tests: as a
library with a loop of its own ([`own_loop`](../../examples/own_loop/main.cpp)), as an application made in code
([`application`](../../examples/application/main.cpp)) and as a data-driven game without C++
([`data_driven`](../../examples/data_driven/)).

| Page | What it covers |
|---|---|
| [Architecture](architecture.md) | The levels, `Application` and `Module`, the three rendering levels and the graphics contract |
| [Memory](memory.md) | Linear and pool allocators, and memory counted by what it is for |
| [Job system](job_system.md) | Worker threads, jobs, counters and parallel loops, shared by every layer |
| [Application and scenes](application_and_scenes.md) | The application loop, scenes, the update order, scene systems, settings sections, game modules |
| [Entities and components](entities_and_components.md) | Entities, the hierarchy, components and their hooks, failures, saving and registering components |
| [Scripting](scripting.md) | Luau scripts on components, the scene API they reach, writing them in other editors, and adding languages |
| [Gameplay toolkit](gameplay_toolkit.md) | Time, timers, tweens, coroutines, signals, typed references and versioned saves |
| [Assets and importing](assets_and_importing.md) | Source assets and `.meta` files, the asset database and importers, loading imported assets |
| [Prefabs](prefabs.md) | Reusable entity tree assets, import validation and runtime instantiation |
| [Rendering](rendering.md) | The graphics device, shaders, Renderer2D, Renderer3D, SceneRenderer, lights, skies, shadows |
| [Clouds, stars and the moon](sky.md) | Drifting clouds configured in the scene, and the night sky's stars and moon |
| [Runtime UI and text](runtime_ui.md) | SDF fonts, world text and runtime interfaces |
| [Animation](animation.md) | Skeletons, clips, poses and animated characters |
| [Particles](particles.md) | Curves, gradients and particle effects |
| [Physics](physics.md) | Colliders, rigidbodies, the physics world and its backend |
| [2D games](2d.md) | 2D physics, sprite atlases, animation, tilemaps and pixel-perfect cameras |
| [Audio](audio.md) | Sounds, the audio device and the mixer |
| [Input](input.md) | Events, the input state, keys, the mouse and the cursor lock |
| [Diagnostics](diagnostics.md) | Logging, crash reports, contained failures, the profiler and debug drawing |
| [Developer tools](developer_tools.md) | Commands, typed cvars, consoles, statistics, profiler timelines and captures |
| [Editor](editor.md) | The editor's documents, panels, undo, play mode and game code reloading |
| [Tools and the command line](tools_and_command_line.md) | Projects, cooking, building, running and exporting games with `spot` |
| [Sandbox](sandbox.md) | Running the playground, prefab spawnables, controls and a play-test checklist |
| [Sandbox gameplay framework](sandbox_gameplay.md) | The sandbox's gameplay as independent systems that games can take, and its rules |
| [Sandbox release](sandbox_release.md) | Release exports, runtime libraries, relocation checks and platform acceptance |
| [Releases and installation](releases.md) | The installed engine's layout, the release workflow, the installers and `spot update` |

Building, testing and the code rules are in [`CONTRIBUTING.md`](../../CONTRIBUTING.md) and [`AGENTS.md`](../../AGENTS.md).

## Modules

The engine is the modules of `engine/spot/`, one folder each. There are no fixed levels: roughly, the modules near
the operating system and the hardware (the core, the platform, graphics) are what the others build on, and they never
know about scenes or the game loop. A module may depend on another where it needs to; what matters is to keep them as
loosely coupled as makes sense and to put each thing where its concept belongs, so a game that only needs a window and
a renderer gets just that and a system can be tested on its own.

Games include the headers with the prefix, as `<spot/core/debug/log.h>`, a module's public headers with its own
header, or all of them with `<spot.h>`; `<spot/scripting.h>` is only in builds with `SPOT_SCRIPTING`, and `<spot.h>`
leaves it out. A module header compiles on its own (`tests/module_headers/`).

| Folder | Header | What it holds |
|---|---|---|
| `core/` | `<spot/core.h>` | Logging, assertions, memory, files, math, time, jobs, archives (`Archive`), the profiler, ids, asset references, bounds, colors, curves |
| `platform/` | `<spot/platform.h>` | Windows, events, the input state and actions, processes, shared libraries, the crash handler, RenderDoc capture; SDL3 behind `platform/sdl3/` |
| `assets/` | `<spot/assets.h>` | Loading imported assets, the built-in assets and their primitive meshes (`PrimitiveMeshes`) |
| `graphics/` | `<spot/graphics.h>` | The graphics API and its backends (rendering level 1), and the CPU data it uploads: images and meshes (`MeshData`) |
| `render/`, `audio/`, `physics/`, `animation/`, `particles/` | `<spot/render.h>`, `<spot/audio.h>`… | The systems' APIs, without scenes: the code-only renderers (rendering level 2), the mixer, the physics simulations, poses and clips, particle simulation |
| `scene/` | `<spot/scene.h>` | Entities, components, scenes, scene systems, prefab instancing, tweens, timers, tasks and signals |
| `runtime/` | `<spot/runtime.h>` | `Application`, the default game loop with scenes, and what runs a game: settings, the console, save data, profile streaming, game modules |
| `scene/<module>/`: `scene/rendering/`, `scene/physics/`, `scene/audio/`, `scene/ui/`… | `<spot/modules.h>` | Each system's components and scene systems, built on its public API (rendering level 3): `MeshRenderer`, `Rigidbody`, `PhysicsWorld`, `SceneRenderer`, the entity-based UI |
| `scene/scripting/` | `<spot/scripting.h>` | Luau scripts and their bindings |
| `import/`, then `tools/`, `cli/`, `player/` and `editor/` outside the engine | | Importing and making games out of projects, which games never link |

Backends live in their system's folder, behind its contract: Vulkan through NVRHI in `graphics/nvrhi/` and SDL_GPU in `graphics/sdl_gpu/`, SDL3 audio in
`audio/sdl3/`, Jolt in `physics/jolt/`, Box2D in `physics/box2d/`. Public headers never show backend types.

### Games without scenes

`Application` and `Scene` are the default way to make a game, not the only one. A game that needs another model,
such as an ECS of its own for a large world, writes its own `main()` and loop on the layers below the scene:
`Window` and `Platform::PollEvents()` for events, an `InputState` it feeds them to, the `GraphicsDevice` with
`Renderer2D`, `Renderer3D` and the other code-only renderers, `AudioDevice`, and an `AssetManager` for imported
assets. The scene layer itself draws with those renderers' public API only (`scene/rendering/scene_drawing.h`), so
a game's own storage can draw the same way. `tests/custom_loop_tests.cpp` is such a loop.

The `Input::`, `Time::`, `Audio::` and `Assets::` shortcuts read what `Application` hands them while it runs; a loop
of its own sets them (`Input::SetState()` and the like) or passes its objects around instead.

The engine never knows about projects or the editor: a game runs from the `game.json` its build writes and from its
imported assets.
