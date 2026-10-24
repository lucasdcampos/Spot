# Roadmap

The refactor that makes Spot a simple, modular engine library, before an example game decides what comes next. This
file is temporary: it is deleted once every task is done.

- A phase starts once the phases it depends on are done; phases that do not depend on each other run in parallel.
  Tasks of a phase run in parallel too, unless a task names the ones it waits for.
- Each task is one commit or a few, builds without warnings on every platform and keeps the test suite green. The
  editor only gets the mechanical changes an engine change forces on it.
- The commit that finishes a task ticks its box. Sizes: S is under a day, M a few days, L a week or more.
- Engine paths are relative to the folder of the engine's modules: `engine/`, then `engine/spot/` once 1.1 is done.

| Phase | Depends on | Runs in parallel with |
|---|---|---|
| 1. Layout | – | 2 |
| 2. Baseline | – | 1, 3, 4, 5, 6, 11 |
| 3. Untangle the systems | 1 | 2, 4–9, 11 |
| 4. A scene core that knows no system | 1 | 2, 3, 5, 7, 8, 9 |
| 5. One active scene | 1 | 2, 3, 4, 7, 8, 9 |
| 6. Modules and a thin application | 4, 5; task 6.2 also 3.3 | 2, 3, 7, 8, 9 |
| 7. Tools out of the runtime | 1, 2; task 7.3 also 6.4 and 6.5 | 3–6, 8, 9, 11 |
| 8. Graphics and rendering | 1, 2 | 3–7, 9, 11 |
| 9. Scripting | 1, 2; task 9.3 also 6.2 | 3–8, 11 |
| 10. Packaging and documentation | 3–9 | 11 |
| 11. Example game | 4, 5, 6 | 2, 3, 7–10 |

The longest chain is 1, then 4 and 5, then 6, which phases 10 and 11 wait for: start it first.

## Phase 1 – Layout

Depends on: nothing. Done first and quickly, since moving folders conflicts with every other change. Its tasks run
in order, as each moves files the next one touches.

- [x] **1.1 A `spot/` prefix for the engine's headers (S).** The modules move into `engine/spot/`, so games include
  `<spot/core/log.h>` instead of `<core/log.h>`, which could collide with their own folders; `<spot.h>` stays where it
  is. Every include in the engine, the editor, the tools, the tests and the sandbox follows, and so do the layer
  check, `AGENTS.md` and `docs/`.
- [x] **1.2 Merge folders (S).** `math/` into `core/`; `window/`, `events/` and `input/` into `platform/`; `physics2d/`
  into `physics/`.
- [x] **1.3 A foundation-only core (S).** Tweens, timers, tasks and signals move to `scene/`; the console, save data and
  profile streaming to `runtime/`; the crash handler, RenderDoc capture, processes and shared libraries to
  `platform/`.
- [x] **1.4 Split `graphics/` (M).** The GPU interface and its backends stay in `graphics/` (rendering level 1);
  renderers, meshes, materials, models, fonts, text, cameras, lights, sky, shadows, post-processing, debug drawing and
  particle drawing move to `render/` (rendering level 2). No behavior changes.
- [x] **1.5 Layer check by levels (S).** `cmake/spot_check_layers.cmake` gives each folder its level and fails on
  includes of the same level or above, except two it lists: rendering reaching animation's skeletons and clips for
  skinned models, and animation's components reaching physics' for ragdolls. It starts with an allowlist of today's
  violations; the commit that fixes one removes its entry. "Layers" in `docs/index.md` and the layer rule in
  `AGENTS.md` describe the levels:

  | Level | Folders |
  |---|---|
  | 0 | `core/`: logging, asserts, memory, files, math, time, jobs, archives, the profiler, ids |
  | 1 | `platform/`: window, events, input, processes, shared libraries, the crash handler; the SDL backend |
  | 2 | `assets/` |
  | 3 | `graphics/`: the GPU interface and its backends (rendering level 1) |
  | 4 | `render/` (rendering level 2), `audio/`, `physics/`, `animation/`, `particles/`: low-level APIs, without scenes |
  | 5 | `scene/`: entities, components, scenes, scene systems, prefab instancing |
  | 6 | `runtime/`: `Application`, `Module` |
  | 7 | `scene/<module>/`: each system's components and its `Module` (rendering level 3), and UI |
  | 8 | `scene/scripting/` |

## Phase 2 – Baseline

Depends on: nothing. Must be done before phases 7, 8 and 9.

- [x] **2.1 Profile the sandbox in Release (S).** Frame time, prefab spawn cost, scene load time and script update
  time, written under this task, so later phases measure against them.

  Baseline captured on 2026-10-23 from `d3e666a` with the Phase 1.1 layout changes and the benchmark
  instrumentation in this working tree. Windows 11 Pro 10.0.26200, Intel Core i5-11600K, NVIDIA GeForce
  GTX 1660, MSVC toolset 14.51.36231, Release, Luau enabled, Vulkan (NVRHI). Three fresh processes,
  High quality, 1600x900, vsync/graphics validation off, orbit camera, 500 spawned crates and 50 added
  lamps; 934 physics bodies. Each capture warms up for 240 frames and measures the next 1,200.
  Builds/tests were monitored during captures; captures overlapping them were discarded. Asset-manager
  caches start empty; the operating system's file cache is warm and is not flushed.

  ```powershell
  cmake --build --preset msvc-release --target spot_sandbox_benchmark
  ./build/msvc/tests/Release/spot_sandbox_benchmark --props 500 --lamps 50 --warmup 240 --frames 1200 --quality 2
  ```

  | Measurement (ms) | Capture 1 | Capture 2 | Capture 3 |
  |---|---:|---:|---:|
  | Frame mean | 9.088 | 9.200 | 9.268 |
  | Frame median | 8.935 | 9.033 | 9.138 |
  | Frame p95 | 10.378 | 10.568 | 10.577 |
  | Frame worst | 12.158 | 12.915 | 12.218 |
  | Startup scene load | 177.644 | 158.769 | 213.998 |
  | Prefab spawn mean | 0.120272 | 0.127238 | 0.109111 |
  | Prefab spawn median | 0.102900 | 0.114800 | 0.098800 |
  | Prefab spawn p95 | 0.193000 | 0.191300 | 0.172900 |
  | First prefab spawn | 0.191000 | 0.289500 | 0.209700 |
  | Script update mean per frame | 0.036402 | 0.036367 | 0.036462 |
  | Script update median per frame | 0.035100 | 0.035200 | 0.035500 |
  | Script update p95 per frame | 0.051400 | 0.051800 | 0.052300 |

  Scene load times the startup `InstantiateScene`: imported data read/decode, entity/component creation
  and asset-reference loading, excluding platform/device setup, activation and first updates. Spawn
  times 500 calls to `PropSpawner::Spawn` for the imported Crate, including the placement raycast and
  undo entry, excluding the benchmark's subsequent grid placement. Script update sums inclusive
  `<script>.OnUpdate` scopes per measured frame: three hooks per frame, 3,600 calls per capture,
  no dropped profiler samples. It includes dispatch/bindings, excluding start/fixed hooks. CPU scope
  averages now cover all measured frames, not only the profiler's last 240; frame intervals include
  collection overhead. Resident memory was 221.6/220.5/222.1 MiB.

  Use the median of the three run means for comparison: **9.200 ms/frame**, **0.120272 ms/spawn**,
  **177.644 ms/scene load**, **0.036402 ms/frame of script updates**. Scene loading has only three
  samples; retain the spread rather than treating its median as a precise threshold. Keep the workload
  and instrumentation fixed in later phases; see [Sandbox](docs/sandbox.md#play-test-checklist).

## Phase 3 – Untangle the systems

Depends on: Phase 1.

- [x] **3.1 Ids in the core (S).** `AssetRef` moves to `core/` beside `AssetId`, and `EntityId` to `core/`.
  `core/archive.h` stops declaring scene and asset types; `FieldOptions::entityFilter` takes a filter on entity ids
  that the tools give.
- [x] **3.2 CPU geometry in the core (S).** `MeshData` and the bounds types move to `core/`, so physics stops including
  rendering.
- [x] **3.3 Asset loaders register themselves (M).** Waits for 3.1. The asset manager stops building every type:
  graphics and each system register their own (`RegisterGraphicsAssets()`, `RegisterRenderAssets()`,
  `RegisterAudioAssets()`, `RegisterAnimationAssets()`), decoding on workers and creating on the main thread. Loaders
  belong to a manager instead of being static. Asset references load only through the archive's manager, never
  through `Assets::GetManager()`. `Application` calls the registrations until 6.2 moves them into modules.
- [x] **3.4 Models and particles (S).** `render/` includes animation only for models' skeletons and clips, the listed
  exception, and particles not at all: `ParticleRenderer` draws plain particle arrays instead of a
  `ParticleSimulation`.
- [ ] **3.5 3D physics public (M).** `Detail::PhysicsSimulation` becomes `Spot::Physics3D`, beside `Spot::Physics2D`,
  with the same naming for bodies, shapes and queries. A test simulates 3D bodies without a scene.

## Phase 4 – A scene core that knows no system

Depends on: Phase 1.

- [ ] **4.1 Scene notifications (M).** `SceneSystem` hears about components added and removed, entity layers changed
  and entities moved. `PhysicsWorld` follows the scene through them; `Entity` loses its `dynamic_cast`s and direct
  physics calls.
- [ ] **4.2 Listener interfaces (M).** Collision, trigger, 2D collision and trigger, animation event, joint break and UI
  drawing hooks move from `Component` into interfaces of their `scene/<module>/` folders (`CollisionListener`…), and
  `Component` loses its system friends. `ScriptComponent` implements them; the sandbox's components add the ones they
  use.
- [ ] **4.3 Systems by type (S).** `Scene::GetOrAddSystem<T>()`, looked up by type index, replaces `GetPhysics()`,
  `GetPhysics2D()`, `GetAudio()`, `GetUi()`, `GetUiInput()` and the `dynamic_cast` scan of `GetSystem<T>()`.
- [ ] **4.4 UI and render hooks out of `Scene` (M).** Waits for 4.3. Canvas drawing and UI event routing move into
  `UiSystem` and `UiInputSystem`, which is given its `InputState`; render hooks move into a `scene/rendering` system.
- [ ] **4.5 Components registered by their folders (S).** Each `scene/<module>/` registers its own components
  (`RegisterPhysicsComponents()`…), and `scene/component_registry.cpp` includes none of them. `Application` calls the
  registrations until 6.2 moves them into modules.
- [ ] **4.6 `Scene::Spawn<T>()` and component shortcuts (S).** `Spawn<T>()` creates an entity with its component;
  `Component` gets `GetTransform()`, `AddComponent<T>()`, `GetComponent<T>()` and `Destroy()`, forwarding to its
  entity.

## Phase 5 – One active scene

Depends on: Phase 1.

- [ ] **5.1 Chunks (M).** A scene loads a scene asset into itself, under a root entity it returns, in the background;
  unloading the chunk destroys its root.
- [ ] **5.2 One scene per application (L).** Waits for 5.1. `Application` runs one active scene: the persistent scene,
  the additive scene list, per-scene event routing and drawing several scenes go. Entities marked with
  `Entity::SetKeepOnSceneChange()` move into the next scene. `SceneRenderer` draws one scene, so the whole world is one
  physics simulation. The editor's play mode follows; `docs/application_and_scenes.md` describes it.

## Phase 6 – Modules and a thin application

Depends on: phases 4 and 5. Task 6.2 also waits for 3.3 to move asset loader registrations.

- [ ] **6.1 `Module` (M).** The interface (`OnStart`, `OnStop`, `OnEvent`, `OnUpdate`, `OnRender`),
  `Application::AddModule<T>()` and `GetModule<T>()`. `Run()` starts modules in the order they were added, stops them
  in reverse, and calls their hooks at fixed points of the frame. Only `Application` uses `Module`: no system's
  low-level API depends on it.
- [ ] **6.2 A module per system (M).** Waits for 6.1. `RenderModule` owns the `SceneRenderer` and draws the scene,
  `AudioModule` owns the `AudioDevice`, `UiModule` routes events to canvases and draws them; physics, animation,
  particles and scripting get theirs. Each registers its components and asset loaders. `runtime/` includes no system.
  The sandbox, the player and the editor add the modules they use.
- [ ] **6.3 Startup written down (M).** Waits for 6.2. The constructor does no work; `Run()` is a sequence read top to
  bottom: log file and crash reports when configured, platform, window, graphics device, assets, input, modules,
  `OnStart()`, the start scene. `docs/application_and_scenes.md` describes it.
- [ ] **6.4 Data-driven games through `Run()` (M).** Waits for 6.1. `ApplicationConfig::game` is the only way game
  settings reach an application, and `Run()` applies their rebinds, console settings and start scene. Only
  `GameSettings::LoadBesideExecutable()`, which the player and games call in `main()`, looks for `game.json`. The
  game's code is a `Module` that registers its components in `OnStart()` (`SPOT_COMPONENT` stays as a shorthand it
  runs); `SPOT_GAME_MODULE(Type)` exports a function that makes it, for the editor; `Detail::GameCode` goes. The
  project templates and the sandbox write the new `main()`.
- [ ] **6.5 Developer tools module (M).** Waits for 6.1. The console overlay, stats overlay, screenshots, RenderDoc
  capture and profile streaming move into `DevToolsModule`, which development builds and the editor add.
- [ ] **6.6 Editor hooks off `Application` (M).** Waits for 6.1. Pausing and stepping move to the scene, event filtering
  and game settings switching to the editor's own subclass. `Scene::GetApplication()` goes; game code reaches the
  application through `Application::Get()`.

## Phase 7 – Tools out of the runtime

Depends on: phases 1 and 2. Task 7.3 also waits for 6.4 and 6.5.

- [ ] **7.1 Prefabs: instantiated by games, edited by tools (M).** Overrides, diffing, apply, revert and unpack move to
  `spot_tools`. Instances keep only their prefab root and local ids. A prefab is parsed once when it loads, and nested
  prefabs are flattened when imported.
- [ ] **7.2 Unknown components (S).** `MissingComponent` keeps unknown components only when tools ask for it (an option
  of `InstantiateOptions`); games log a warning and skip them.
- [ ] **7.3 Reloads in the developer tools (S).** `GameModule`, renamed `GameLibrary` so "module" has one meaning,
  `ReloadAssetReferences()` and `AssetManager::Reload()` move into `DevToolsModule`.

## Phase 8 – Graphics and rendering

Depends on: phases 1 and 2.

- [ ] **8.1 The graphics contract (M).** `tests/rhi_tests.cpp` checks the baseline every backend must do, and each
  capability a backend reports, on every backend built. Renderer and golden tests run on each platform's default
  backend; the whole GPU suite runs on every backend when a backend or a shader changes (test presets and
  `AGENTS.md`). `docs/rendering.md` writes the contract down.
- [ ] **8.2 Renderers on the baseline (S).** GPU tests draw each engine renderer with every capability off, so none
  needs one without a fallback.
- [ ] **8.3 Split `Renderer3D` (L).** A small public header (pimpl) over internal classes: mesh batching and drawing,
  shadows, sky lighting and world culling.
- [ ] **8.4 GPU culling optional (S).** Waits for 8.3. GPU culling with Hi-Z occlusion and LODs becomes an option, off
  by default. `RenderWorld` stays public at rendering level 2, documented for scenes with many objects.
- [ ] **8.5 Debug lines given, not read (S).** `LineRenderer` draws the lines it is given instead of reading
  `DebugDraw::GetLines()`; `SceneRenderer` passes them.

## Phase 9 – Scripting

Depends on: phases 1 and 2. Task 9.3 also waits for 6.2.

- [ ] **9.1 Allocation-free property access (M).** Script wrappers are cached per entity in the script context, and
  member names are resolved once instead of compared on each access. The baseline's script update time shows the
  gain.
- [ ] **9.2 Bindings by module (S).** The built-in bindings in `scene/scripting/script_scene.cpp` split into a file per
  module.
- [ ] **9.3 Optional scripting (M).** Waits for 9.2 and 6.2. A `SPOT_SCRIPTING` CMake option; scripting leaves `spot.h`, and
  only builds with it on compile and link Luau. The player and the editor build with it.

## Phase 10 – Packaging and documentation

Depends on: phases 3 to 9.

- [ ] **10.1 A header per module (S).** `<spot/core.h>`, `<spot/platform.h>`, `<spot/graphics.h>`, `<spot/render.h>`,
  `<spot/audio.h>`, `<spot/physics.h>`, `<spot/scene.h>`, `<spot/runtime.h>` and the others; `<spot.h>` includes them
  all.
- [ ] **10.2 One example per level (S).** Small programs using the engine as a library with a loop of their own, as an
  application in code, and as a data-driven game, built and run by the tests; the docs link them as where to start.
- [ ] **10.3 Architecture page (S).** `docs/architecture.md`, linked from `docs/index.md`: the levels, modules and
  `Module`, the three rendering levels and the graphics contract.

## Phase 11 – Example game

Depends on: phases 4, 5 and 6, which settle the API a game writes against.

- [ ] **11.1 Start the game (S).** A game made as an application in code, on the public API only, with a friction log
  beside it: each missing function, awkward call, slow path or surprising default, with the line of game code that
  hit it.
- [ ] **11.2 Measure it (S).** Profile it in Release from the first week. The friction log, not a feature list, decides
  the next roadmap.
