# Spot Roadmap

The plan for taking Spot from its current foundation to a complete, professional engine: what to build, in which
order, and how each piece is designed. It was written on 2026-10-09 after comparing Spot with its legacy C# version
(`Spot_Legacy_CSharp`), which had far more features but grew them as workarounds: a 3,000-line editor class, undo
that fell back to whole-scene snapshots, half-finished PBR, prefabs without overrides, a renderer stuck on OpenGL.
The goal is the same breadth, built on the architecture Spot already has.

**Current focus:** M0 – Foundations

## How to use this plan

- Work top to bottom. The next task is the first unchecked step. Milestones depend on the ones before them unless
  they say otherwise.
- A step is one commit, or a few small ones. Tick its box in the commit that finishes it, so this file always matches
  `main`, and update **Current focus** when a milestone ends. Commit messages may end with the step, as in
  `Add raycasts to the physics world (M1.4)`.
- A step is done when it builds without warnings, its tests pass, the editor exposes it where that applies, its
  `docs/` page says how it works (from M0.6 on), and any imported format it changed bumped its importer's version.
  Each milestone ends with a GCC build and its acceptance check.
- When work stops in the middle of a step, leave the box unchecked and add an `> In progress:` line under it saying
  what is done and what is left.
- Decisions taken while implementing, and departures from this plan, go in the milestone's **Notes**, so the reason
  survives beyond the commit message.
- Steps can be split, reordered within a milestone or added when the work shows a better way; update this file in the
  same commit.

## Architecture principles

Every milestone follows these, on top of the rules in `AGENTS.md`.

1. **Layers.** Core, then backends behind engine contracts (platform, graphics, audio, physics), then engine features
   built on public APIs, then scene components, then tools and the editor. Nothing reaches past its layer, and the
   engine never knows about the editor or projects.
2. **Mechanism before policy.** Each feature first has a code-only API that works without scenes (as `Renderer3D`,
   `AudioDevice` and the physics queries do), then components on top of it. Games can always do what the engine does.
3. **Plain, versioned data.** Components describe their fields with `SPOT_FIELDS()` or `Serialize()`, assets are
   imported into versioned formats, and settings live in the project and reach games through `game.json`.
4. **Identity by id.** `EntityId` and `AssetId` are what lasts across frames, undo, reloads and, later, the network.
   Pointers are never kept across frames by tools.
5. **Time.** Simulation runs in fixed steps, gameplay reads scaled time, and engine services and tools use real time.
6. **Contained failures.** A failure is logged with its context and contained to the smallest unit that owns it (a
   component, a system, an asset). It is never silent, and it takes the frame down only when nothing else is
   possible. Crashes leave reports.
7. **Performance by design.** No allocations per frame on hot paths, data that systems iterate stored contiguously,
   profiler scopes on every system, and budgets measured in Release.
8. **Threads.** The game thread owns scenes. The audio thread only mixes. Worker jobs, when they come, only touch data
   handed to them.
9. **Small units.** One responsibility per class and file. A file growing past ~800 lines is a signal to split it.
10. **Tested and documented.** Every feature has unit tests, plus GPU or editor tests where it needs them, and a page
    in `docs/`.

## Milestones

| | Milestone | Delivers |
|---|---|---|
| [ ] | [M0 – Foundations](#m0--foundations) | Contained failures, scene systems, settings sections, profiler, debug drawing, docs |
| [ ] | [M1 – Physics for gameplay](#m1--physics-for-gameplay) | Layers, triggers, collision events, queries, character controller, mesh colliders, joints |
| [ ] | [M2 – Input actions and gamepads](#m2--input-actions-and-gamepads) | Gamepads, text input, actions, rebinding, input ownership |
| [ ] | [M3 – Audio in scenes](#m3--audio-in-scenes) | Buses, voice management, streaming, 3D sources and listener, mixer panel |
| [ ] | [M4 – Fast game-code iteration](#m4--fast-game-code-iteration) | A one-file change in game code reloads in seconds |
| [ ] | [M5 – Rendering: HDR and post-processing](#m5--rendering-hdr-and-post-processing) | HDR, tonemapping, bloom, AA, grading, fog, render hooks |
| [ ] | [M6 – Rendering: PBR and transparency](#m6--rendering-pbr-and-transparency) | Metallic-roughness PBR, normal maps, sky lighting, alpha modes, golden-image tests |
| [ ] | [M7 – Rendering: scale](#m7--rendering-scale) | Culling, instancing, clustered lights, better shadows, benchmarks |
| [ ] | [M8 – Model hierarchy and prefabs](#m8--model-hierarchy-and-prefabs) | Model node trees, prefab assets, overrides, prefab editing |
| [ ] | [M9 – Skeletal animation](#m9--skeletal-animation) | Skins, clips, GPU skinning, Animator, controllers, blend trees, events, root motion |
| [ ] | [M10 – Gameplay toolkit](#m10--gameplay-toolkit) | Timers, time scale, tweens, coroutines, signals, typed references, save data |
| [ ] | [M11 – Developer tools](#m11--developer-tools) | Console commands and cvars, overlays, profiler panel, captures |
| [ ] | [M12 – Runtime UI and text](#m12--runtime-ui-and-text) | World text, SDF fonts, entity-based UI, widgets, layout, navigation |
| [ ] | [M13 – Particles](#m13--particles) | Particle systems, curves and gradients, particle rendering |
| [ ] | [M14 – 2D](#m14--2d) | 2D physics, sprite animation, tilemaps |

The sandbox (`sandbox/`) is how each milestone is tried as a game: every milestone ends by using what it added there.

---

## M0 – Foundations

Cross-cutting pieces that every later milestone builds on.

- [ ] **M0.1 Contain component failures.** Every component hook (the lifecycle hooks now, and the physics, animation
  and UI callbacks later) runs inside one guard. The guard catches exceptions, logs the entity, component type and
  hook once, and marks the component as faulted, which disables it until its scene is reloaded. `IsFaulted()` tells
  tools, and the inspector shows it. Tests: a throwing component stops, the others keep running, and the error is
  logged once.
- [ ] **M0.2 Scene systems.** Add `SceneSystem`, logic that runs over a whole scene at fixed points of
  `Scene::Update()` (before fixed steps, after fixed steps, update, late update, before render), in a declared order.
  The engine's systems are created when a scene first needs them, as `PhysicsWorld` is now, and games add their own
  with `Scene::AddSystem<T>()`. `PhysicsWorld` becomes the first system and `Scene::GetPhysics()` stays. Systems run
  under the M0.1 guard. Tests: order, lazy creation, a failing system is skipped.
- [ ] **M0.3 Game settings sections.** `GameSettings` gets typed sections that later milestones add (physics, input,
  audio, rendering), each with its `Serialize()`. Projects store them, `spot build` and `spot cook` write them to
  `game.json`, and `Application` applies them at start. The editor's project settings panel draws each section with
  the inspector backend. Tests: round trip, and sections that one build does not know are kept.
- [ ] **M0.4 Profiler.** `SPOT_PROFILE_SCOPE("Name")` records nested CPU timings on the game thread into a ring of the
  last frames (`Profiler::GetFrames()`), cheap enough to stay on in Release. The application, scenes, systems,
  renderers and the mixer's game-side calls are instrumented. Tests: nesting, frame boundaries, ring wrap.
- [ ] **M0.5 Debug drawing.** `DebugDraw` lets any code queue lines, boxes, spheres, capsules, arrows and crosses for
  this frame or for a duration, with or without depth testing. `LineRenderer` draws them on the public graphics API,
  and both `SceneRenderer` and the editor's views draw them. Tests: queue lifetimes (unit) and drawing (GPU).
- [ ] **M0.6 Documentation.** Create `docs/` with an index and one page per existing system: application and scenes,
  entities and components, assets and importing, rendering, physics, audio, input, editor, tools and the command
  line. Add a rule to `AGENTS.md` that features update their page.

**Done when:** a throwing sandbox component is reported and contained, the sandbox's frame timings can be read from the
profiler, and debug lines show in both the game and the editor.

**Notes:**

---

## M1 – Physics for gameplay

Everything games need from physics beyond falling bodies. Jolt already provides it; this milestone puts it behind the
engine's contract (`physics_simulation.h`) and the public API.

Design:
- **Layers belong to entities.** An entity has one layer, out of 32 named in the physics settings, as Unity has. The
  physics layer matrix, query masks and, in M7, camera culling masks all use it. Tags stay for lookup.
- **Triggers are separate bodies.** An entity's trigger colliders form a sensor body of their own, which follows the
  entity, so one entity can have solid shapes and trigger shapes.
- **Events are buffered.** The backend records contacts during a step (Jolt reports them from its threads). After the
  step, `PhysicsWorld` turns them into enter, stay and exit events per pair of entities and dispatches them on the
  game thread to the components of both entities, under the M0.1 guard.
- **Queries live on `PhysicsWorld`.** They take a `QueryFilter` (layer mask, whether triggers count, an entity to
  ignore) and return hits with the `EntityId`, the collider, the point, the normal and the distance.

Steps:
- [ ] **M1.1 Layers.** Add `Entity::GetLayer()`/`SetLayer()`, layer names and a symmetric collision matrix in the
  physics settings section, and Jolt object layers and filters in the backend. The editor gets a layer dropdown on
  the entity and a matrix editor in project settings. Layers are serialized. Tests.
- [ ] **M1.2 Triggers.** Add `Collider::SetTrigger()` and sensor bodies, which detect overlaps but never push. Tests.
- [ ] **M1.3 Collision and trigger events.** Contact buffering in the backend contract, pairing in `PhysicsWorld`, and
  the hooks `OnCollisionEnter/Stay/Exit(const Collision&)` (other entity, contact points, normal, relative velocity,
  impulse) and `OnTriggerEnter/Exit(const TriggerContact&)`. Disabling or destroying a collider sends its exits.
  Tests: event sequences, destroying entities from inside callbacks, disabled colliders.
- [ ] **M1.4 Queries.** `Raycast`, `RaycastAll`, `SphereCast`, `OverlapSphere` and `OverlapBox` with `QueryFilter`.
  Queries see the bodies of the last step plus pending rebuilds. Tests.
- [ ] **M1.5 More rigidbody control.** `AddForceAtPosition`, `AddImpulseAtPosition`, `AddTorque`, freezing rotation
  axes, continuous collision detection for fast bodies, and sleep control. Tests.
- [ ] **M1.6 Character controller.** A `CharacterController` component on Jolt's `CharacterVirtual`: `Move()`,
  `IsGrounded()`, the ground normal and velocity, slope limit, step height, pushing dynamic bodies, collision events,
  and layers. The sandbox player moves with it. Tests.
- [ ] **M1.7 Mesh colliders.** A `MeshCollider` from a mesh asset or from runtime `MeshData` (`SetMesh()`): a static
  triangle mesh, or a convex hull for dynamic bodies. Built shapes are cached per mesh. Tests.
- [ ] **M1.8 Joints.** `FixedJoint`, `HingeJoint` (limits, motor), `DistanceJoint` and `SpringJoint`, each connecting
  to another entity by `EntityId` or to the world. Tests.
- [ ] **M1.9 Debug view and editor.** Colliders, contacts and queries drawn with `DebugDraw`, a Colliders toggle in the
  editor's views and at runtime, and **Fit to Mesh** for box, sphere and capsule colliders.

**Done when:** in the sandbox, the player walks on a mesh-collider level with the character controller, a raycast
weapon pushes crates at the hit point, and a trigger pickup reacts to the player.

**Notes:**

---

## M2 – Input actions and gamepads

Design: actions are data in the input settings, not code. Each action has a type (button, axis, 2D vector) and
bindings with processing (dead zone, invert, scale). A player's rebinds are saved in the game's user data folder.

- [ ] **M2.1 Gamepads.** SDL3 gamepad events become engine events (connected, disconnected, button, axis), and
  `InputState` tracks each gamepad. Add `Input::IsGamepadButtonDown/Pressed/Released`, `GetGamepadAxis`, rumble, and
  a choice between one gamepad and any gamepad. Tests with synthetic events.
- [ ] **M2.2 Text input.** A `TextInputEvent` from SDL text events, `Window::StartTextInput()`/`StopTextInput()` with
  the IME area, and `Input::GetTypedText()`. Tests.
- [ ] **M2.3 Actions.** An `InputActionMap` of actions and bindings: keys, mouse buttons, mouse delta, scroll, gamepad
  buttons and axes, and composites (four buttons into a vector). Values and press/release edges per action, each
  frame. Add `Input::IsActionDown/Pressed/Released`, `GetActionValue` and `GetActionVector2`. It also works from code
  alone. Tests.
- [ ] **M2.4 Action settings.** The input settings section, its project settings editor (action list, binding capture
  by pressing the input), and default actions in the project templates (Move, Look, Jump, Fire, Pause).
- [ ] **M2.5 Rebinding.** Runtime rebinding, saving and loading per user, and reset to defaults. Tests.
- [ ] **M2.6 Input ownership.** A capture stack, so overlays (console, UI text fields, the editor) can withhold input
  from the game without changing the game's requests: the cursor lock comes back afterwards, and the mouse delta is
  zero on each hand-over. The editor's play view uses it. Tests.
- [ ] **M2.7 Sandbox.** The sandbox and the templates use actions, and the sandbox plays with a gamepad.

**Done when:** the sandbox plays the same with keyboard and mouse or a gamepad, and a rebind survives a restart.

**Notes:**

---

## M3 – Audio in scenes

Design: the mixer stays engine code (`Detail::Mixer`). Spatial audio is computed on the game thread each frame from
the listener and the sources, and sent to the mixer as per-voice gains and pan. HRTF waits. Buses form a small graph
defined in the audio settings. Streamed sounds decode on a worker thread into a ring buffer the mixer reads.

- [ ] **M3.1 Buses.** A bus graph in the mixer (Master plus user buses, each with volume, mute and a parent), voices
  routed to a bus, `AudioDevice` bus control, and peak meters per bus read without locks. Tests on `Mixer`, no device
  needed.
- [ ] **M3.2 Voice management.** Voice limits, priorities and stealing, short fades on start and stop to avoid
  clicks, and ramped volume and pan changes. Tests.
- [ ] **M3.3 Streaming.** Sounds marked "stream" in their import settings stay encoded and decode while they play.
  Bump the sound importer's version. Tests.
- [ ] **M3.4 Sources and listener.** `AudioListener` and `AudioSource` components. A source has a sound, a bus,
  volume, pitch, loop, play on start, spatial on or off, min and max distance, a rolloff curve, and `Play()`,
  `Stop()` and `Pause()`. Add `Audio::PlayOneShot(sound, position, bus)`. An `AudioSystem` (M0.2) updates gains and
  pan from transforms. A scene's sounds stop when it unloads and pause when scenes are paused. Tests.
- [ ] **M3.5 Mixer settings and panel.** The audio settings section (buses, default volumes) and an editor Audio Mixer
  panel with faders, meters, mute and solo, which take effect live during Play.
- [ ] **M3.6 Sandbox.** Music on a Music bus, footsteps, and impact sounds from collision events (M1.3).

**Done when:** sandbox sounds come from where their entities are, and the mixer panel changes them while playing.

**Notes:**

---

## M4 – Fast game-code iteration

Game code is C++, so the time from saving a file to seeing it in the editor is the engine's "script reload" time. The
budget is a few seconds for a one-file change in Debug.

- [ ] **M4.1 Measure.** The editor logs how long each module build phase takes (configure, compile, link, load).
  Record the baseline in Notes.
- [ ] **M4.2 Precompiled headers.** `spot_add_game()` precompiles `<spot.h>` and the standard headers it pulls in.
  The engine's own targets use PCH where it pays off.
- [ ] **M4.3 Compiler cache.** Use sccache or ccache as the compiler launcher when one is installed, for the engine
  and for games. Document it.
- [ ] **M4.4 Header cost.** Profile the include cost of `spot.h` (`-ftime-trace`) and cut the top offenders with
  forward declarations and by moving heavy includes out of widely used headers.
- [ ] **M4.5 Skip needless work.** Configure only when the CMake files changed, build only what changed, and record
  the result against the budget in Notes.

**Done when:** a one-line change in a sandbox component reloads in the editor within the budget.

**Notes:**

---

## M5 – Rendering: HDR and post-processing

Design: scenes render into an HDR target, and a post-processing stack turns it into the output. Each effect is a pass
built on the public graphics API. The look comes from a `PostProcessing` component (the first active one, as with
`Sky`) over the defaults in the rendering settings. MSAA resolves before post-processing.

- [ ] **M5.1 Graphics API.** Float color formats (`Rgba16Float`, and `R11G11B10Float` where supported) as render
  targets, MSAA resolve into sampled textures, and `FullscreenPass` (one triangle and a fragment shader). GPU tests.
- [ ] **M5.2 HDR and tonemapping.** `SceneRenderer` draws into the HDR target and tonemaps it (ACES or AgX, with
  exposure) into the output. Sky exposure stays consistent, and the editor's views use the same path.
- [ ] **M5.3 Bloom.** A downsample and upsample chain, with intensity and scatter.
- [ ] **M5.4 FXAA.** Optional FXAA next to MSAA, chosen in the settings.
- [ ] **M5.5 Grading.** A LUT (strip format), contrast, saturation, white balance and vignette.
- [ ] **M5.6 Fog.** Distance and height fog in the mesh shader through a `Fog` component (color, start, density,
  height falloff), applied before tonemapping and matched to the sky at the horizon.
- [ ] **M5.7 Settings and component.** The rendering settings section (defaults, MSAA samples, shadow sizes and
  distance, which feed `Renderer3DConfig`), the `PostProcessing` component, and their inspectors.
- [ ] **M5.8 Render hooks.** Stages in `SceneRenderer` (before opaque, after opaque, after transparent, before and
  after post-processing, overlay) where games and systems add passes, with a context holding the camera, targets and
  depth. `DebugDraw` moves onto them. Tests.

**Done when:** the sandbox renders through HDR, bloom, grading and fog, and a game pass draws at a chosen stage.

**Notes:**

---

## M6 – Rendering: PBR and transparency

Design: metallic-roughness PBR, as glTF defines it, with GGX shading. Ambient light comes from the sky through
image-based lighting. Materials are Opaque, Cutout (alpha test) or Transparent (blended, sorted back to front, no
depth writes). Vertices gain tangents.

- [ ] **M6.1 Tangents.** `MeshVertex` gets tangents, which glTF provides or are generated (MikkTSpace). `MeshData`
  builders generate them. Bump the mesh and model formats and importers. Tests.
- [ ] **M6.2 PBR materials.** Metallic, roughness, normal map with scale, metallic-roughness map, occlusion map, and
  emissive color and map. Bump the `.material` format and its importer, which reads the old fields. The glTF importer
  reads every PBR input, and the material inspector and thumbnails follow. Remove specular and shininess, and
  migrate the sandbox's materials.
- [ ] **M6.3 Shading.** Cook-Torrance with GGX, Smith and Schlick, inverse-square falloff windowed by range, and light
  intensities in documented units.
- [ ] **M6.4 Sky lighting.** Cube textures and rendering into their faces in the graphics API. Render the sky into a
  cube map when it changes, prefilter it for specular, and take irradiance as spherical harmonics. GPU tests.
- [ ] **M6.5 Alpha modes.** Cutout and transparent pipelines, sorting, cutout shadows, and glTF `alphaMode` import.
- [ ] **M6.6 Golden-image tests.** GPU tests render reference scenes and compare them to stored images within a
  tolerance, so rendering changes are seen before they ship.

**Done when:** glTF sample models (such as DamagedHelmet and a transparent model) look as they do in reference
viewers, and the golden-image tests cover them.

**Notes:**

---

## M7 – Rendering: scale

- [ ] **M7.1 Culling.** Frustum culling in every pass, a per-camera culling mask by entity layer (M1.1), and visible
  and culled counts in the profiler.
- [ ] **M7.2 Instancing.** Batch draws by mesh, material and pipeline, with per-instance data in a storage buffer,
  automatically in `Renderer3D`.
- [ ] **M7.3 Clustered lights.** Assign lights to a froxel grid on the CPU and send it in storage buffers, raising
  `MaxLights` from 16 to hundreds. Shadowed lights keep their budget. Tests.
- [ ] **M7.4 Sorting.** Sort opaque draws by pipeline, material and depth to minimize state changes and overdraw.
- [ ] **M7.5 Shadow quality.** Soft PCF, stable cascades (texel snapping), blending between cascades, and caster
  culling per cascade.
- [ ] **M7.6 Benchmark.** A benchmark scene with 10,000 meshes and 200 lights. Record its Release frame times in Notes
  and check them when rendering changes.

**Done when:** the benchmark holds 60 FPS in Release on the development machine.

**Notes:**

---

## M8 – Model hierarchy and prefabs

Design: models keep their node hierarchy, and instantiating one makes one entity per node. A prefab is an asset
holding an entity tree. An instance stores the prefab's id and only its overrides: field changes as JSON pointers per
prefab-local entity id, plus added and removed components and entities. Loading applies the overrides onto the
prefab's current content, so prefab changes reach every instance. Prefabs nest.

- [ ] **M8.1 Model nodes.** Model import keeps nodes, with their names and transforms, a mesh per node, and
  primitives as submeshes. `Model` exposes the node tree, and `Renderer3D::DrawModel()` keeps working. Bump the model
  format. Tests.
- [ ] **M8.2 Model instances.** `Scene::Instantiate()` of a model creates an entity per node, and `MeshRenderer` can
  draw one submesh. Models dropped in the editor create that hierarchy.
- [ ] **M8.3 Prefab assets.** A `.prefab` source in the scene format, its importer, `AssetType::Prefab`, and
  `Scene::Instantiate(AssetRef<Prefab>)` at runtime. Tests.
- [ ] **M8.4 Instances and overrides.** Prefab-local entity ids, instances saved as a prefab id plus overrides in
  scene files, and overrides applied on load. Tests: propagation, overrides kept across prefab edits, removed targets.
- [ ] **M8.5 Prefab editing.** Create a prefab by dragging entities into the asset browser, edit it in prefab mode
  (a `.prefab` opened as a document), see overrides in the inspector, Apply or Revert per field, component or
  instance, and Unpack.
- [ ] **M8.6 Nesting and variants.** Prefabs inside prefabs, and variants that override another prefab.
- [ ] **M8.7 Live updates.** Saving a prefab updates the instances in open scenes, keeping undo valid.

**Done when:** the sandbox's crates and pickups are prefabs, and a change to the prefab reaches every instance while
each keeps its own overrides.

**Notes:**

---

## M9 – Skeletal animation

Design: skeletons and clips come from glTF. An `AnimationSystem` evaluates poses into contiguous arrays rather than
one entity per bone, and only bones that are asked for appear in the scene (`BoneAttachment`). Model instances
(M8.2) create entities for the nodes that are not joints. Skinning runs on the GPU from a second vertex stream.
Controllers are assets with parameters, transitions with cross-fades, blend trees and events.

- [ ] **M9.1 Import.** Skins (joints, inverse bind matrices), animations (translation, rotation and scale channels;
  step, linear and cubic), and vertex joints and weights. `Skeleton` and `AnimationClip` assets, with clips as
  sub-assets of models or from animation-only files. Bump the importers. Tests.
- [ ] **M9.2 Pose math.** Sampling, blending, local to model space, and skinning palettes. Pure CPU, unit-tested.
- [ ] **M9.3 GPU skinning.** `SkinnedMeshRenderer`, a skinned pipeline variant and its shadow pass, and padded bounds.
- [ ] **M9.4 Animator.** An `Animator` component with `Play()`, `CrossFade()`, speed and loop, evaluated by the
  `AnimationSystem`, and previewed in the editor in edit mode.
- [ ] **M9.5 Controllers.** An `AnimatorController` asset with float, int, bool and trigger parameters, states,
  transitions (conditions, exit time, blend duration, interruption) and any-state transitions. A runtime evaluator,
  and `SetFloat`, `SetBool` and `SetTrigger`. Tests.
- [ ] **M9.6 Blend trees.** 1D and 2D (freeform directional) blend states.
- [ ] **M9.7 Events and root motion.** Clip events delivered to `OnAnimationEvent()`, and root motion applied to the
  transform or to a `CharacterController`.
- [ ] **M9.8 Layers and masks.** Override and additive layers with bone masks, such as for the upper body.
- [ ] **M9.9 Editor.** A clip preview with a timeline in the inspector, a controller graph editor, and live
  parameters during Play.
- [ ] **M9.10 Bone attachments.** `BoneAttachment` places an entity on a bone, such as a weapon in a hand.

**Done when:** a sandbox character walks, runs and jumps through a controller with a blend tree, its feet follow root
motion, and it holds an attached prop.

**Notes:**

---

## M10 – Gameplay toolkit

- [ ] **M10.1 Time.** A global time scale, unscaled time for tools and UI, physics that follows the scale, and a clamp
  on the frame delta if one is missing. Tests.
- [ ] **M10.2 Timers.** `Invoke(delay, fn)`, repeating invokes and cancellation on components, in scaled time,
  stopped when the component goes. Tests.
- [ ] **M10.3 Tweens.** Tweens of values, transforms and colors with easing functions and sequences, run by a
  `TweenSystem`. Tests.
- [ ] **M10.4 Coroutines.** A C++20 coroutine `Task` that awaits the next frame, seconds, a condition or another task,
  owned by a component and cancelled with it. Tests.
- [ ] **M10.5 Signals.** `Signal<Args...>` with scoped connections that disconnect themselves, for gameplay messages
  and UI callbacks. Tests.
- [ ] **M10.6 Typed references.** `ComponentRef<T>` fields, serialized by entity id and drawn in the inspector with
  drag and drop.
- [ ] **M10.7 Save data.** Save files in the user data folder written with `Archive`, with versioning. Tests.

**Done when:** the sandbox's timed and animated behaviors use these instead of hand-written timers.

**Notes:**

---

## M11 – Developer tools

- [ ] **M11.1 Commands and cvars.** A `ConsoleCommand` registry and `CVar<T>` (name, default, description, flags
  such as cheat or saved), with parsing, completion and history. The engine registers its rendering, physics and
  audio switches. Tests.
- [ ] **M11.2 In-game console.** An overlay drawn with `Renderer2D` that takes input through M2.6. The editor's console
  panel gets the command line.
- [ ] **M11.3 Stats overlay.** FPS, frame time, draw calls, triangles, bodies, voices and memory.
- [ ] **M11.4 Profiler panel.** A timeline of recent frames from M0.4 in the editor, with averages per scope and
  pausing. It covers games running in their own process too, through the channel the editor already reads their
  logs from.
- [ ] **M11.5 Captures.** Screenshot command, and frame captures with RenderDoc when it is present.

**Done when:** a frame spike in the sandbox can be found from the profiler panel and toggled away from the console.

**Notes:**

---

## M12 – Runtime UI and text

Design: UI elements are entities. A `Canvas` component roots a UI tree, a `UiRect` component (anchors, pivot,
offsets, size) lays out each element, and widgets are components. That way UI gets serialization, prefabs, the
hierarchy, the inspector and undo without anything new. M12.2 decides how `UiRect` relates to the entity's
`Transform` and records it in Notes. UI draws with `Renderer2D` after post-processing.

- [ ] **M12.1 Text.** SDF or MSDF glyphs, so text stays sharp at any size (bump the font importer), and a
  `TextRenderer` component for text in the world, optionally facing the camera.
- [ ] **M12.2 Canvas and layout.** `Canvas`, `UiRect`, scaling modes (constant pixel size, or scaling with a
  reference resolution), the layout pass and draw order. Tests.
- [ ] **M12.3 Widgets.** `UiImage` (nine-slice, tint), `UiText` (alignment, wrapping, overflow), `UiButton` (states,
  an `OnClick` signal), `UiToggle`, `UiSlider`, `UiInputField` (M2.2) and `UiScrollView` with clipping.
- [ ] **M12.4 Layout groups.** Horizontal, vertical and grid layouts, and size fitters.
- [ ] **M12.5 UI input.** Pointer hit testing, focus and navigation with keyboard and gamepad, and UI that consumes
  the input it handles (M2.6). Tests.
- [ ] **M12.6 Editor.** Rect handles and anchor presets in the scene view, a widget creation menu, and canvas
  preview.
- [ ] **M12.7 Sandbox.** The sandbox's HUD and a pause menu are rebuilt with it.

**Done when:** the sandbox's pause menu works with mouse, keyboard and gamepad, and is authored in the editor.

**Notes:**

---

## M13 – Particles

- [ ] **M13.1 Curves and gradients.** `Curve` and `Gradient` types that are serialized and edited in the inspector.
- [ ] **M13.2 Simulation.** A `ParticleSystem` component with emission rate and bursts, shapes (point, sphere, cone,
  box), start ranges, values over lifetime, gravity and local or world space, simulated on the CPU by a system.
  Tests.
- [ ] **M13.3 Rendering.** Instanced billboards that face the camera or stretch with velocity, additive or alpha
  blending, sorting, soft particles from depth, and flipbook textures.
- [ ] **M13.4 Editor.** Edit-mode simulation of the selected system, and restart and seek controls.

**Done when:** the sandbox's impacts and the player's actions emit particles.

**Notes:**

---

## M14 – 2D

Only when a 2D game is planned; it depends on M1 for the shape of its API.

- [ ] **M14.1 2D physics.** A Box2D v3 backend with `Rigidbody2D` and box, circle, capsule and polygon colliders,
  with the same events and queries as 3D.
- [ ] **M14.2 Sprite sheets.** A sprite atlas importer and sprite animation.
- [ ] **M14.3 Tilemaps.** Tilemap component, painting in the editor, and colliders from tiles.
- [ ] **M14.4 Pixel-perfect camera.** An option for pixel art.

**Notes:**

---

## Backlog

Not scheduled. Each comes in when a game needs it, and gets a milestone of its own then.

- Navigation: navmesh baking in the tools (Recast) and a `NavAgent` component (Detour).
- Async asset loading and streaming, a job system, LODs.
- SSAO, depth of field, motion blur, TAA, decals, reflection probes, terrain.
- Custom shaders for materials, with parameters the inspector shows.
- Audio effects (filters, reverb zones) and HRTF.
- IK (two-bone, look-at) and ragdolls.
- Networking (transport, replication, RPCs).
- A web target, once SDL_GPU supports WebGPU.
- Localization.
- A scripting language, only if M4 does not make C++ iteration fast enough.
