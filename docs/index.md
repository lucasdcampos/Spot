# Spot documentation

How Spot works, one page per system. The headers stay the reference for every function; these pages explain how
the pieces fit together, why they are built that way, and where to start.

Spot is a 2D/3D game engine written in C++20. A game is a project (`.spot`) with assets and C++ code; the tools
import its assets, build its code against the engine, and run or export it. The editor is a Dear ImGui application
that does the same with a window around it.

| Page | What it covers |
|---|---|
| [Application and scenes](application_and_scenes.md) | The application loop, scenes, the update order, scene systems, settings sections, game modules |
| [Entities and components](entities_and_components.md) | Entities, the hierarchy, components and their hooks, failures, saving and registering components |
| [Gameplay toolkit](gameplay_toolkit.md) | Time, timers, tweens, coroutines, signals, typed references and versioned saves |
| [Assets and importing](assets_and_importing.md) | Source assets and `.meta` files, the asset database and importers, loading imported assets |
| [Prefabs](prefabs.md) | Reusable entity tree assets, import validation and runtime instantiation |
| [Rendering](rendering.md) | The graphics device, shaders, Renderer2D, Renderer3D, SceneRenderer, lights, skies, shadows |
| [Runtime UI and text](runtime_ui.md) | SDF fonts, world text and runtime interfaces |
| [Animation](animation.md) | Skeletons, clips, poses and animated characters |
| [Particles](particles.md) | Curves, gradients and particle effects |
| [Physics](physics.md) | Colliders, rigidbodies, the physics world and its backend |
| [Audio](audio.md) | Sounds, the audio device and the mixer |
| [Input](input.md) | Events, the input state, keys, the mouse and the cursor lock |
| [Diagnostics](diagnostics.md) | Logging, crash reports, contained failures, the profiler and debug drawing |
| [Developer tools](developer_tools.md) | Commands, typed cvars, consoles, statistics, profiler timelines and captures |
| [Editor](editor.md) | The editor's documents, panels, undo, play mode and game code reloading |
| [Tools and the command line](tools_and_command_line.md) | Projects, cooking, building, running and exporting games with `spot` |
| [Sandbox](sandbox.md) | Running the playground, prefab spawnables, controls and a play-test checklist |

Building, testing and the code rules are in [`AGENTS.md`](../AGENTS.md); the plan for what comes next is
[`ROADMAP.md`](../ROADMAP.md).

## Layers

Every system sits in one layer and only uses the ones below it:

1. **Core**: logging, assertions, crash handling, paths, time, the profiler, math.
2. **Backends** behind engine contracts: windowing (SDL3, `engine/platform/`), graphics (SDL_GPU,
   `engine/graphics/sdl_gpu/`), audio (SDL3, `engine/audio/`) and physics (Jolt, `engine/physics/jolt/`). Public
   headers never show backend types.
3. **Engine features** built on public APIs only: the renderers, the mixer, the physics world, assets.
4. **Scene components** on top of those features: `MeshRenderer`, `Light`, `Rigidbody` and the rest.
5. **Tools** (`spot_import`, `spot_tools`, the `spot` command line) **and the editor**, which games never link.

The engine never knows about projects or the editor: a game runs from the `game.json` its build writes and from its
imported assets.
