# Architecture

Spot is a library first. The engine is modules in levels, each usable from code without the ones above it; the
editor, the tools and the player are programs built on it, like any game. This page is the map: the levels, how
`Application` and `Module` put them together, and the three levels of rendering down to the graphics contract. The
system pages linked from the [index](index.md) go into each part.

## Three ways to use the engine

The [examples](../examples/) are where to start, one per level, each built and run by the tests:

| Example | Uses the engine | Builds on |
|---|---|---|
| [`own_loop`](../examples/own_loop/main.cpp) | As a library, with a loop of its own: a window, events, input, a graphics device and `Renderer2D`, without `Application` or scenes | `<spot/platform.h>`, `<spot/graphics.h>`, `<spot/render.h>` |
| [`application`](../examples/application/main.cpp) | As an application in code: `Application` runs the loop, `RenderModule` draws, the scene is built from components in `OnStart()` | `<spot/runtime.h>`, `<spot/scene.h>`, `<spot/modules.h>` |
| [`data_driven`](../examples/data_driven/) | As a data-driven game: a project with a scene and a Luau script and no C++, which the tools cook and `spot_player` runs from its `game.json` | The tools and the player |

A game picks the lowest level that does what it needs. One with its own model of the world, such as an ECS for a
large world, stays below the scene ([Games without scenes](index.md#games-without-scenes)); most games are an
application with scenes; games that need no code of their own are data.

## Levels

The engine's modules are the folders of `engine/spot/`. A folder includes only itself and the levels below it, so
each level builds and works without the ones above, and the systems of one level never know each other. The test
"Engine folders include only the levels below them" (`cmake/spot_check_layers.cmake`) enforces it.

| Level | Folders | Header |
|---|---|---|
| 0 | `core/`: logging, memory, files, math, time, jobs, archives, ids, the profiler | `<spot/core.h>` |
| 1 | `platform/`: windows, events, input, processes, shared libraries, crashes | `<spot/platform.h>` |
| 2 | `assets/`: loading imported assets | `<spot/assets.h>` |
| 3 | `graphics/`: the graphics API and its backends | `<spot/graphics.h>` |
| 4 | `render/`, `audio/`, `physics/`, `animation/`, `particles/`: the systems' low-level APIs, without scenes | `<spot/render.h>`, `<spot/audio.h>`, `<spot/physics.h>`, `<spot/animation.h>`, `<spot/particles.h>` |
| 5 | `scene/`: entities, components, scenes, scene systems, prefabs, the gameplay toolkit | `<spot/scene.h>` |
| 6 | `runtime/`: `Application`, `Module`, game settings, the console, save data, developer tools | `<spot/runtime.h>` |
| 7 | `scene/<module>/`: each system's components and its `Module` | `<spot/modules.h>` |
| 8 | `scene/scripting/`: Luau scripts, only with `SPOT_SCRIPTING` | `<spot/scripting.h>` |

`<spot.h>` includes every header but scripting's. Backends stay inside their system's folder (`graphics/nvrhi/`,
`graphics/sdl_gpu/`, `platform/sdl3/`, `audio/sdl3/`, `physics/jolt/`, `physics/box2d/`) and public headers never show
their types, so a backend changes without the API changing. Importing (`import/`) and the programs outside the engine
are tools: games never link them, and the engine never knows about projects.

Why levels: a game that only needs a window and a renderer gets just that, a system can be tested and replaced on its
own, and the dependencies that would make the engine one block cannot creep in unnoticed.

## Application and modules

`Application` (level 6) is the default game loop with one active scene. Its constructor only keeps the configuration;
`Run()` starts the platform, the window, the graphics device, the assets and the input in that order, then the
modules, and runs frames until the game quits ([Application and scenes](application_and_scenes.md)).

The systems join it as `Module`s (`engine/spot/runtime/module.h`), added before `Run()` with
`Application::AddModule<T>()`: `RenderModule` draws the scene, `AudioModule` plays its sounds, `UiModule` routes events to
canvases, and physics, 2D physics, animation, particles and scripting have theirs. Each registers its components and
asset loaders in `OnStart()` and hooks into the frame at fixed points (`OnEvent()`, `OnUpdate()`, `OnRender()`), in the
order it was added; they stop in reverse. A game adds only the modules it uses, and its own code is a module too, as
`DevToolsModule` is for the console and the overlays.

Only `Application` knows modules. A system's low-level API (level 4) and its scene components (level 7) work without
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
- Adding a system: a folder at level 4 for its low-level API, its components and `Module` in `scene/<system>/`, its
  header in `engine/spot/`, a page here.
- Building and the code rules: [`AGENTS.md`](../AGENTS.md).
