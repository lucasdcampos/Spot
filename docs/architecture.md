# Architecture

Spot is a library first. The engine is loosely coupled modules, each usable from code on its own; the
editor, the tools and the player are programs built on it, like any game. This page is the map: the modules, how
`Application` and `Module` put them together, and the three levels of rendering down to the graphics contract. The
system pages linked from the [index](index.md) go into each part.

## Three ways to use the engine

The [examples](../examples/) are where to start, one per way of using the engine, each built and run by the tests:

| Example | Uses the engine | Builds on |
|---|---|---|
| [`own_loop`](../examples/own_loop/main.cpp) | As a library, with a loop of its own: a window, events, input, a graphics device and `Renderer2D`, without `Application` or scenes | `<spot/platform.h>`, `<spot/graphics.h>`, `<spot/render.h>` |
| [`application`](../examples/application/main.cpp) | As an application in code: `Application` runs the loop, `RenderModule` draws, the scene is built from components in `OnStart()` | `<spot/runtime.h>`, `<spot/scene.h>`, `<spot/modules.h>` |
| [`data_driven`](../examples/data_driven/) | As a data-driven game: a project with a scene and a Luau script and no C++, which the tools cook and `spot_player` runs from its `game.json` | The tools and the player |

A game picks the simplest way that does what it needs. One with its own model of the world, such as an ECS for a
large world, stays below the scene ([Games without scenes](index.md#games-without-scenes)); most games are an
application with scenes; games that need no code of their own are data.

## Modules

The engine's modules are the folders of `engine/spot/`, each with its header. There are no fixed levels: the core, the
platform and graphics, near the operating system and the GPU, are what the rest builds on, and the systems above them
depend on each other only where they need to.

| Folders | Header |
|---|---|
| `core/`: logging, memory, files, math, time, jobs, archives, ids, the profiler | `<spot/core.h>` |
| `platform/`: windows, events, input, processes, shared libraries, crashes | `<spot/platform.h>` |
| `assets/`: loading imported assets | `<spot/assets.h>` |
| `graphics/`: the graphics API and its backends, images and meshes | `<spot/graphics.h>` |
| `render/`, `audio/`, `physics/`, `animation/`, `particles/`: the systems' APIs, without scenes | `<spot/render.h>`, `<spot/audio.h>`, `<spot/physics.h>`, `<spot/animation.h>`, `<spot/particles.h>` |
| `scene/`: entities, components, scenes, scene systems, prefabs, the gameplay toolkit | `<spot/scene.h>` |
| `runtime/`: `Application`, `Module`, game settings, the console, save data, developer tools | `<spot/runtime.h>` |
| `scene/<module>/`: each system's components and its `Module` | `<spot/modules.h>` |
| `scene/scripting/`: Luau scripts, only with `SPOT_SCRIPTING` | `<spot/scripting.h>` |

`<spot.h>` includes every header but scripting's. Backends stay inside their system's folder (`graphics/nvrhi/`,
`graphics/sdl_gpu/`, `platform/sdl3/`, `audio/sdl3/`, `physics/jolt/`, `physics/box2d/`) and public headers never show
their types, so a backend changes without the API changing. Importing (`import/`) and the programs outside the engine
are tools: games never link them, and the engine never knows about projects.

Why loose coupling rather than rigid levels: a game that only needs a window and a renderer gets just that and a
system can be tested and replaced on its own, while each thing still lives where its concept belongs instead of where
a rule of levels forces it.

## Application and modules

`Application` is the default game loop with one active scene. Its constructor only keeps the configuration;
`Run()` starts the platform, the window, the graphics device, the assets and the input in that order, then the
modules, and runs frames until the game quits ([Application and scenes](application_and_scenes.md)).

The systems join it as `Module`s (`engine/spot/runtime/module.h`), added before `Run()` with
`Application::AddModule<T>()`: `RenderModule` draws the scene, `AudioModule` plays its sounds, `UiModule` routes events to
canvases, and physics, 2D physics, animation, particles and scripting have theirs. Each registers its components and
asset loaders in `OnStart()` and hooks into the frame at fixed points (`OnEvent()`, `OnUpdate()`, `OnRender()`), in the
order it was added; they stop in reverse. A game adds only the modules it uses, and its own code is a module too, as
`DevToolsModule` is for the console and the overlays.

Only `Application` knows modules. A system's API and its scene components work without
one, so a loop of its own uses the same renderers and simulations without `Module` at all.

## Rendering in three levels

| Rendering level | What it is | Where |
|---|---|---|
| 1 | The graphics API: `GraphicsDevice`, `RenderContext`, passes, buffers, textures, shaders, pipelines, render graphs | `graphics/` |
| 2 | The code-only renderers: `Renderer2D`, `Renderer3D`, `LineRenderer`, particles, fonts, post-processing, `RenderWorld` | `render/` |
| 3 | Scenes drawn by their components: `MeshRenderer`, `Light`, `Sky`, `SceneRenderer`, drawn by `RenderModule` | `scene/rendering/` |

Each level uses only the public API of the one below: the renderers are built on the graphics API as any game could
build its own, and scenes are drawn through the renderers' public API (`scene/rendering/scene_drawing.h`). A game can
draw its own way at any level, next to the engine's.

### The graphics contract

The graphics API is the engine's own; Vulkan through NVRHI and SDL_GPU (Direct3D 12, Vulkan, Metal) implement it.
What a backend must do is written down as a **baseline**, which every device does and code never checks for, and
**capabilities** (`GraphicsCapabilities`: compressed textures, compute, indirect draws, several color targets, GPU
timers, texture tables), which a device reports and may lack. `tests/rhi_tests.cpp` checks both on every backend built.
The engine's renderers use the baseline or take a fallback when a capability is off, so every game runs on every
backend; [Rendering](rendering.md#the-contract) has the details.

## Where to start

- Using the engine: the [examples](../examples/), then [Application and scenes](application_and_scenes.md) and
  [Entities and components](entities_and_components.md).
- Drawing: [Rendering](rendering.md), from the graphics API to `SceneRenderer`.
- Adding a system: a folder for its API, without scenes, its components and `Module` in `scene/<system>/`, its
  header in `engine/spot/`, a page here.
- Building and the code rules: [`AGENTS.md`](../AGENTS.md).
