# Spot documentation

How Spot works, one page per system. The headers stay the reference for every function; these pages explain how
the pieces fit together, why they are built that way, and where to start.

Spot is a 2D/3D game engine written in C++20. A game is a project (`.spot`) with assets and C++ code; the tools
import its assets, build its code against the engine, and run or export it. The editor is a Dear ImGui application
that does the same with a window around it.

| Page | What it covers |
|---|---|
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
| [Sandbox release](sandbox_release.md) | Release exports, runtime libraries, relocation checks and platform acceptance |

Building, testing and the code rules are in [`AGENTS.md`](../AGENTS.md).

## Layers

The engine is layers, each a set of folders of `engine/spot/` that only includes its own layer and the ones below it. The
test "Engine layers include only the layers below them" (`cmake/spot_check_layers.cmake`) fails the build's tests
otherwise. Games include the headers with the prefix, as `<spot/core/log.h>`, or all of them with `<spot.h>`.

| Layer | Folders | What it holds |
|---|---|---|
| 0. Core | `core/` | Logging, assertions, paths, time, the profiler, jobs, serialization (`Archive`), ids, math, colors, curves |
| 1. Platform | `platform/` | Windows, events, the input state and actions, processes, shared libraries, the crash handler, RenderDoc capture; SDL3 behind `platform/sdl3/` |
| 2. Systems | `graphics/`, `render/`, `audio/`, `physics/`, `animation/`, `particles/`, `assets/` | The mechanism: the graphics device (`graphics/`) and the code-only renderers (`render/`), the mixer, the physics simulations, poses and clips, particle simulation, loading imported assets |
| 3. Scene | `scene/`, with a folder per system: `scene/rendering/`, `scene/physics/`, `scene/audio/`, `scene/ui/`… | Entities, components and scene systems built on the systems' public API, tweens, timers, tasks and signals: `MeshRenderer`, `Rigidbody`, `PhysicsWorld`, `SceneRenderer`, the entity-based UI |
| 4. Runtime | `runtime/` | `Application`, the default game loop with scenes, and what runs a game: settings, the console, save data, profile streaming, game modules |
| 5. Tools | `import/`, then `tools/`, `cli/`, `player/` and `editor/` outside the engine | Importing and making games out of projects, which games never link |

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
