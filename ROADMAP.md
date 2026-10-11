# Spot Roadmap

The plan for taking Spot from its current foundation to a complete, professional engine: what to build, in which
order, and how each piece is designed. It was written on 2026-10-09 after comparing Spot with its legacy C# version
(`Spot_Legacy_CSharp`), which had far more features but grew them as workarounds: a 3,000-line editor class, undo
that fell back to whole-scene snapshots, half-finished PBR, prefabs without overrides, a renderer stuck on OpenGL.
The goal is the same breadth, built on the architecture Spot already has.

The roadmap ends with a game: the sandbox becomes a first-person physics sandbox inspired by Garry's Mod (see
[The sandbox game](#the-sandbox-game)), which puts nearly every system of the engine to work.

**Current focus:** M13 - Particles (requested out of order; M9 and M12 remain pending)

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

## The sandbox game

Milestone by milestone, the sandbox (`sandbox/`) grows into a complete first-person physics sandbox inspired by
Garry's Mod. It is a game to play and also the engine's integration test: it uses nearly every system, so a
regression shows up as something broken in the game. Each milestone ends with **Sandbox** steps that add the features
it makes possible, and [M15](#m15--the-sandbox-game) finishes the game.

When the roadmap ends, the game has:

- **Movement.** First-person walking, running, crouching, jumping and noclip flight, on a playground level with
  ramps, stairs, platforms, a building to walk into, and a day cycle.
- **Weapons.** A pistol, an automatic rifle, a shotgun and a rocket launcher, with aiming down the sights, recoil,
  spread, ammo and reloading. They have view-model animations and muzzle flashes, hits push props, and explosions
  push everything around them.
- **Holding objects.** E picks up a light prop, carries it in front of the camera, turns it and throws it.
- **Physics gun.** It grabs any prop from afar with a beam, and moves it with the mouse. The wheel pulls and pushes
  the prop, and the gun can also turn it, freeze it in the air and unfreeze everything.
- **Tool gun.** Weld, rope, remover, a color and material tool, and lamps to place.
- **Spawn menu.** Q opens a menu of thumbnails of the props and lamps, which spawn where the player looks. Z undoes
  the last spawn, and a cleanup removes them all.
- **Generated assets.** Crates, barrels, explosive barrels, planks, beams, balls, cones, ramps and wheels, a target
  dummy, and a simple rigged robot that walks and falls as a ragdoll when shot. Their PBR textures, the weapon
  models and the sounds are generated too.
- **Sound.** 3D weapon sounds, impacts by surface and speed, footsteps, and ambience and music on their own buses.
- **Interface.** A HUD (crosshair, ammo, weapon) and weapon selection with 1–9 or the wheel. A pause menu holds the
  settings: sensitivity, field of view, volumes, bindings and graphics quality.
- **Saving.** Saving and loading the world, with its props, welds, ropes and frozen props.
- **Console.** `gravity`, `timescale`, `noclip`, `cleanup`, `spawn <prop>`, and a stress test that spawns hundreds of
  props.

How it is built:

- **Generated assets are source assets.** `sandbox/generator/` is a small C++ program (`sandbox_generator`, built
  with the engine) that writes glTF models (with cgltf's writer), PNG textures and WAV sounds into
  `sandbox/assets/generated/`. Fixed seeds keep its output the same from run to run, and it only rewrites files whose
  content changed. The files are committed and imported like any artist's assets. Their `.meta` files come from the
  import, and the generator never touches them.
- **The game's code is the reference for users.** Components each have one job. Weapons, props and tools are
  described as data (definitions and prefabs), not hard-coded. Only the public API is used. Reading the sandbox shows
  how to make a game with Spot.

| Feature | Where it is built |
|---|---|
| Generated props and textures | M0.7, then M3.6 (sounds), M6.7 (PBR sets, weapons), M9.12 (robot, arms) |
| Movement and level collision | M1.10, M2.7 |
| Holding objects and physics gun | M1.11, M5.9 (beam), M13.5 (beam particles) |
| Weapons and explosions | M1.12, M3.6, M5.9, M9.12, M13.5 |
| Tool gun: weld, rope, remover | M1.13, M6.7 (color and material), M7.7 (lamps) |
| Spawning and undo | M1.12, M8.8 (prefabs), M12.7 (spawn menu) |
| Robot and ragdolls | M9.11, M9.12 |
| HUD, menus and settings | M12.7 |
| Saving the world | M10.8 |
| Console commands and stress test | M11.6 |
| Finished level, polish, performance, release | M15 |

## Milestones

| | Milestone | Delivers |
|---|---|---|
| [x] | [M0 – Foundations](#m0--foundations) | Contained failures, scene systems, settings sections, profiler, debug drawing, docs |
| [x] | [M1 – Physics for gameplay](#m1--physics-for-gameplay) | Layers, triggers, collision events, queries, character controller, mesh colliders, joints |
| [x] | [M2 – Input actions and gamepads](#m2--input-actions-and-gamepads) | Gamepads, text input, actions, rebinding, input ownership |
| [x] | [M3 – Audio in scenes](#m3--audio-in-scenes) | Buses, voice management, streaming, 3D sources and listener, mixer panel |
| [x] | [M4 – Fast game-code iteration](#m4--fast-game-code-iteration) | A one-file change in game code reloads in seconds |
| [x] | [M5 – Rendering: HDR and post-processing](#m5--rendering-hdr-and-post-processing) | HDR, tonemapping, bloom, AA, grading, fog, render hooks |
| [x] | [M6 – Rendering: PBR and transparency](#m6--rendering-pbr-and-transparency) | Metallic-roughness PBR, normal maps, sky lighting, alpha modes, golden-image tests |
| [x] | [M7 – Rendering: scale](#m7--rendering-scale) | Culling, instancing, clustered lights, better shadows, benchmarks |
| [x] | [M8 – Model hierarchy and prefabs](#m8--model-hierarchy-and-prefabs) | Model node trees, prefab assets, overrides, prefab editing |
| [ ] | [M9 – Skeletal animation](#m9--skeletal-animation) | Skins, clips, GPU skinning, Animator, controllers, blend trees, events, root motion |
| [x] | [M10 – Gameplay toolkit](#m10--gameplay-toolkit) | Timers, time scale, tweens, coroutines, signals, typed references, save data |
| [x] | [M11 – Developer tools](#m11--developer-tools) | Console commands and cvars, overlays, profiler panel, captures |
| [ ] | [M12 – Runtime UI and text](#m12--runtime-ui-and-text) | World text, SDF fonts, entity-based UI, widgets, layout, navigation |
| [ ] | [M13 – Particles](#m13--particles) | Particle systems, curves and gradients, particle rendering |
| [ ] | [M14 – 2D](#m14--2d) | 2D physics, sprite animation, tilemaps |
| [ ] | [M15 – The sandbox game](#m15--the-sandbox-game) | The finished level, polish, performance and a release build of the game |

---

## M0 – Foundations

Cross-cutting pieces that every later milestone builds on.

- [x] **M0.1 Contain component failures.** Every component hook (the lifecycle hooks now, and the physics, animation
  and UI callbacks later) runs inside one guard. The guard catches exceptions, logs the entity, component type and
  hook once, and marks the component as faulted, which disables it until its scene is reloaded. `IsFaulted()` tells
  tools, and the inspector shows it. Tests: a throwing component stops, the others keep running, and the error is
  logged once.
- [x] **M0.2 Scene systems.** Add `SceneSystem`, logic that runs over a whole scene at fixed points of
  `Scene::Update()` (before fixed steps, after fixed steps, update, late update, before render), in a declared order.
  The engine's systems are created when a scene first needs them, as `PhysicsWorld` is now, and games add their own
  with `Scene::AddSystem<T>()`. `PhysicsWorld` becomes the first system and `Scene::GetPhysics()` stays. Systems run
  under the M0.1 guard. Tests: order, lazy creation, a failing system is skipped.
- [x] **M0.3 Game settings sections.** `GameSettings` gets typed sections that later milestones add (physics, input,
  audio, rendering), each with its `Serialize()`. Projects store them, `spot build` and `spot cook` write them to
  `game.json`, and `Application` applies them at start. The editor's project settings panel draws each section with
  the inspector backend. Tests: round trip, and sections that one build does not know are kept.
- [x] **M0.4 Profiler.** `SPOT_PROFILE_SCOPE("Name")` records nested CPU timings on the game thread into a ring of the
  last frames (`Profiler::GetFrames()`), cheap enough to stay on in Release. The application, scenes, systems,
  renderers and the mixer's game-side calls are instrumented. Tests: nesting, frame boundaries, ring wrap.
- [x] **M0.5 Debug drawing.** `DebugDraw` lets any code queue lines, boxes, spheres, capsules, arrows and crosses for
  this frame or for a duration, with or without depth testing. `LineRenderer` draws them on the public graphics API,
  and both `SceneRenderer` and the editor's views draw them. Tests: queue lifetimes (unit) and drawing (GPU).
- [x] **M0.6 Documentation.** Create `docs/` with an index and one page per existing system: application and scenes,
  entities and components, assets and importing, rendering, physics, audio, input, editor, tools and the command
  line. Add a rule to `AGENTS.md` that features update their page.
- [x] **M0.7 Sandbox: asset generator.** Add `sandbox/generator/` (see [The sandbox game](#the-sandbox-game)), with a
  glTF writer for `MeshData` and a PNG writer. The first output is the props (crate, barrel, explosive barrel, plank,
  beam, ball, cone, ramp, wheel) and their base textures, all imported into the sandbox. The playground scene uses
  them.

**Done when:** a throwing sandbox component is reported and contained, the sandbox's frame timings can be read from the
profiler, debug lines show in both the game and the editor, and the generated props are in the playground.

**Notes:**
- M0.1: a faulted component still gets OnDisable() (when it was active) and OnDestroy() (when it started), each
  under the guard, so it can release what it holds, such as subscriptions. Failures in those after the fault are
  logged too; they run at most once, so nothing repeats every frame. OnValidate() runs under the guard as well, and
  a component that faults there never starts. The fault is not saved: reloading the scene creates new components.
- M0.2: the fixed-step clock moved from `PhysicsWorld` to the scene (`Scene::SetFixedTimeStep()`,
  `Scene::MaxFixedStepsPerUpdate`), since components' `OnFixedUpdate()` runs in scenes without physics too, and
  physics is now created only when needed. Systems have a hook per point of `Scene::Update()`, `OnFixedUpdate()`
  running on each step after the components'; the "before render" point is `Scene::PrepareToRender()`, which the
  application calls for its scenes on frames it draws. Orders are integers (`SystemOrder::Physics` is -1000), with
  ties kept in the order systems were added, and a scene has one system of each type.
- M0.3: sections live in `SettingsSections` (`GameSettings::sections` and `ProjectSettings::sections`), saved
  under `"sections"` in both files; each section is a struct with a `SectionName`, `Serialize()` and `Apply(Scene&)`,
  listed in `SettingsSections::ForEach()`. Unknown sections are kept as JSON text. The application applies the
  sections to every scene it loads, rather than once at start, since settings like the fixed step belong to scenes.
  The first section is physics (gravity and the fixed time step). `Application::SetGameSettings()` lets the editor
  play scenes with the project's settings.
- M0.4: `Profiler` is a namespace of functions over one process-wide ring (`MaxFrames` = 240), since there is one
  game thread; the thread that calls `Profiler::BeginFrame()` is the one recorded. Scope names are string literals
  or `Profiler::Intern()`ed, so samples hold views without copying; systems are timed under their type's name. A
  frame spans the whole loop iteration, waiting for the frame limiter included (`Application::Wait`). The sandbox's
  HUD shows the average update and render times read from it.
- M0.5: `DebugDraw` is one process-wide queue of lines, since shapes are world-space and every view draws them;
  shapes become lines as they are queued. `DebugDraw::Update()` ages them with the frame's real time once the
  frame was drawn, and the queue is capped so loops that never call it cannot grow it forever. Lines are one pixel
  wide (`PrimitiveType::LineList`); `LineRenderer` draws depth-tested lines first, then the rest over everything. The
  sandbox's `PhysicsDebugView` (B) draws every collider with it.
- M0.6: pages are named in snake_case like every file, and the index is `docs/index.md`. Logging, crash reports,
  contained failures, the profiler and debug drawing share a page of their own, `docs/diagnostics.md`, which later
  developer tools (M11) extend. Pages end with what is not there yet, pointing at the milestone that adds it, so
  updating them is part of each milestone.
- M0.7: the PNG writer already existed (`Image::EncodePng()`), so the generator only adds the glTF writer, kept in
  the generator since only it writes models. Each prop is a `.gltf` with a `.bin` beside it, whose image refers to
  `../textures/<name>.png`, so the model's import makes its textured material. Round props share a texture layout
  (side above, ends as discs below). Textures are painted with arithmetic and square roots only, so they come out the
  same on every machine; meshes use `sin`/`cos`, so their bytes may differ across compilers. Until mesh colliders
  (M1), barrels and cones collide as boxes, wheels as spheres, and each ramp through a box laid along its slope on a
  child entity. `tests/sandbox_generator_tests.cpp` loads every prop back and checks winding, centering and that a
  second run writes nothing.
- Done 2026-10-09. Acceptance: faults are reported and contained (`component_fault_tests.cpp`, and the inspector
  marks them); the profiler records the sandbox's frames, and its HUD shows update and render times from it; debug
  lines draw through `LineRenderer` in the `SceneRenderer` (GPU test) and in the editor's viewports, and the sandbox
  draws its colliders with B; the generated props are in the playground. MSVC and GCC Debug build without warnings
  and pass every test.

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
- [x] **M1.1 Layers.** Add `Entity::GetLayer()`/`SetLayer()`, layer names and a symmetric collision matrix in the
  physics settings section, and Jolt object layers and filters in the backend. The editor gets a layer dropdown on
  the entity and a matrix editor in project settings. Layers are serialized. Tests.
- [x] **M1.2 Triggers.** Add `Collider::SetTrigger()` and sensor bodies, which detect overlaps but never push. Tests.
- [x] **M1.3 Collision and trigger events.** Contact buffering in the backend contract, pairing in `PhysicsWorld`, and
  the hooks `OnCollisionEnter/Stay/Exit(const Collision&)` (other entity, contact points, normal, relative velocity,
  impulse) and `OnTriggerEnter/Exit(const TriggerContact&)`. Disabling or destroying a collider sends its exits.
  Tests: event sequences, destroying entities from inside callbacks, disabled colliders.
- [x] **M1.4 Queries.** `Raycast`, `RaycastAll`, `SphereCast`, `OverlapSphere` and `OverlapBox` with `QueryFilter`.
  Queries see the bodies of the last step plus pending rebuilds. Tests.
- [x] **M1.5 More rigidbody control.** `AddForceAtPosition`, `AddImpulseAtPosition`, `AddTorque`, freezing rotation
  axes, continuous collision detection for fast bodies, and sleep control. Tests.
- [x] **M1.6 Character controller.** A `CharacterController` component on Jolt's `CharacterVirtual`: `Move()`,
  `IsGrounded()`, the ground normal and velocity, slope limit, step height, pushing dynamic bodies, collision events,
  and layers. The sandbox player moves with it. Tests.
- [x] **M1.7 Mesh colliders.** A `MeshCollider` from a mesh asset or from runtime `MeshData` (`SetMesh()`): a static
  triangle mesh, or a convex hull for dynamic bodies. Built shapes are cached per mesh. Tests.
- [x] **M1.8 Joints.** `FixedJoint`, `HingeJoint` (limits, motor), `DistanceJoint`, `SpringJoint` and
  `SwingTwistJoint` (for ragdolls), each connecting to another entity by `EntityId` or to the world. Joints can
  break past a force. Tests.
- [x] **M1.9 Debug view and editor.** Colliders, contacts and queries drawn with `DebugDraw`, a Colliders toggle in the
  editor's views and at runtime, and **Fit to Mesh** for box, sphere and capsule colliders.
- [x] **M1.10 Sandbox: movement and level.** A first-person player on the `CharacterController` that walks, runs,
  crouches, jumps and flies in noclip. The level's geometry uses mesh colliders, with ramps, stairs and platforms.
- [x] **M1.11 Sandbox: holding and the physics gun.** E picks up props below a mass limit, carries them in front of
  the camera, turns them and throws them. The physics gun grabs any prop the player aims at, drives it toward a
  target in front of the camera with a spring, and changes the distance with the wheel. It turns the prop, freezes
  it in place (as a kinematic body) and unfreezes all props.
- [x] **M1.12 Sandbox: weapons and spawning.** Weapons described as data. Hitscan pistol, rifle and shotgun push
  props at the hit point with impulses. The rocket launcher fires projectiles that explode with `OverlapSphere` and
  a radial impulse, and explosive barrels blow up when shot. Keys spawn props where the player looks, with undo (Z)
  and cleanup.
- [x] **M1.13 Sandbox: tool gun.** Weld (`FixedJoint`), rope (`DistanceJoint`, drawn with `DebugDraw` for now) and
  remover, with undo.

**Done when:** in the sandbox, the player walks the level, carries and throws props, builds something with the
physics gun and the tool gun, and shoots and blows it apart.

**Notes:**
- M1.1: layer names and the matrix are in `PhysicsSettings` (`layerNames`, `layerCollisions`), and `PhysicsWorld`
  keeps a copy that games can change at run time. The settings mark both fields `FieldOptions::hidden`, a new option
  that keeps a field out of the inspector, since the Project Settings panel draws them its own way. Applying default
  settings still leaves physics uncreated. In Jolt, object layer = layer * 2 + 1 for moving bodies, so the static
  and moving broad phase layers stay as they were. The entity's layer is saved under `"layer"`, read as 0 from older
  scenes.
- M1.2: `PhysicsWorld` keeps a solid body and a sensor body per entity, either of which may be missing. Sensors of
  entities with a rigidbody are kinematic and follow the entity a step behind its dynamic body; others are static.
  A sensor overlaps its own entity's solid body, which the events of M1.3 must leave out.
- M1.3: the backend keeps every touching sub-shape pair (`PhysicsSimulation::GetContacts()`), updated by Jolt's
  added and persisted callbacks. Jolt reports a contact removed when a body falls asleep, so its removal callback is
  not used: after a step, a contact Jolt did not find again ends unless one of its bodies sleeps, and destroying a
  body forgets its contacts. Jolt gives no impulse before solving, so `Collision::impulse` is estimated from the
  approach speed and the bodies' inverse masses. Events go to active components only. There is no
  `OnTriggerStay()`, as the plan lists none.
- M1.4: hits name the `Collider` as well as the entity: `PhysicsWorld` keeps, per body, the colliders its shapes came
  from, and the backend turns Jolt's sub-shape ids back into shape indices. A pointer to a component is not kept by
  the caller across frames; the entity id is what lasts. Entities moved since the last step are seen where they
  were, as the plan says; `OverlapBox()` takes a full size, like `BoxCollider` and `DebugDraw::Box()`.
- M1.5: rotation locks are Jolt's allowed degrees of freedom (`RotationLocks`, saved as `"rotationLocks"`),
  continuous collision is Jolt's linear cast motion quality, and sleep control maps to activating and deactivating
  the body. Before the body exists, `AddImpulseAtPosition()` only adds to the starting velocity, and forces and
  torques are ignored, as `AddForce()` already was.
- M1.6: `Move()` takes a velocity and a time, which Jolt's `CharacterVirtual::ExtendedUpdate()` needs for stairs and
  sticking to the floor; the step height is both how high it climbs and how far down it sticks. Gravity is the
  game's to add, as with Unity's controller. The character's contacts from its last move join the pairs of the next
  step, and count as active only if it moved since. The inner body has no `Collider`, so hits on it name the entity
  with a null collider. The sandbox gained `spot_sandbox_tests` (`tests/sandbox/`), which runs its game code on the
  real playground with simulated keys; it is where each Sandbox step is now tested.
- M1.7: physics needs a mesh's triangles on the CPU, while `Mesh` assets live on the GPU, so `MeshData` became a
  loadable asset type of its own (`AssetManager::LoadMeshData()`, cached apart from the GPU mesh of the same asset,
  and loaded by `Load<MeshData>()`). `MeshCollider` holds an `AssetRef<MeshData>`, so the inspector picks mesh assets
  for it. The Jolt backend's `MeshShapeCache` keys shapes by mesh and drops them once the mesh is gone; scale goes on
  through a `ScaledShape`, so one built shape serves every scale. Before M1.7 the Jolt backend was split into files
  by topic, as it had reached 800 lines.
- M1.8: the entity's own body is Jolt's second body, and what it is joined to (or the world) the first, because
  Jolt measures hinge angles and drives motors as the second body relative to the first. `SpringJoint` is a distance
  constraint with min = max = rest length and a spring. A joint is made again whenever it or one of its bodies
  changes, as Jolt's constraints cannot outlive their bodies, so a hinge counts its angle from where it was last
  made; motors change in place so they do not reset it. `PhysicsWorld` keeps joints by component pointer, which is
  safe because a joint leaves the moment it is disabled, before it can be freed.
- M1.9: `DebugDraw`'s queue became an instance of a new `DebugLines` class, so the editor's scene views draw colliders
  into lists of their own, seen in that view only, rather than in every view through the shared queue. Colliders
  are drawn from components (`DrawColliders()`), which works in edit scenes whose physics never ran; contacts and
  queries come from `PhysicsWorld`, drawn in `OnBeforeRender()`. Queries are recorded only while their flag is on.
  Fit to Mesh is `Collider::FitTo(BoundingBox)` (with `MeshData::GetBoundingBox()`), applied as one undoable edit.
  The sandbox's `PhysicsDebugView` now only switches the engine's view.
- M1.10: movement moved out of `PlayerController` into `PlayerMovement` (walk, run, crouch, jump, noclip, falling
  back to the start), leaving looking, shooting and the flashlight. Crouching shrinks the capsule toward the feet
  and stands up only after a sphere cast finds room. Noclip disables the `CharacterController` and moves the entity
  directly; enabling the controller again makes the character where flying ended. The generator gained stairs, and
  the ramps and stairs collide through `MeshCollider`s of their own meshes; the platform, walls and steps stay boxes.
  Queries that ignore an entity now also ignore its character's inner body. `spot_sandbox_tests` loads the assets
  the build cooks beside the sandbox, so mesh colliders get their meshes there as in the game.
- M1.11: `PropCarrier` (E) and `PhysicsGun` (tool 2) drive props through their rigidbodies' velocities
  (`PropDrive::Toward()`), closing a fifth of the gap each step, so they settle without overshooting and still
  collide on the way; held props keep their rotation relative to where the player faces. Freezing makes the prop
  kinematic and tags it "Frozen", which U undoes for all. Until weapons are data (M1.12), `PlayerController` picks
  the tool in hand with 1 (the ball launcher) and 2, and yields the mouse while a prop is being turned and the click
  while one is carried. The physics gun's beam is a `DebugDraw` line until M5.9 gives it a look.
- M1.12: until prefabs (M8), "data" means components in the scene: each weapon is an entity below the player with a
  `Weapon` component (kind, fire rate, pellets, spread, impulse, magazine, reload, projectile and explosion), and the
  spawnable props are a list on `PropSpawner`. `WeaponHolder` selects the physics gun (1) or a weapon (2-5, or the
  wheel), and replaced the ball launcher; R reloads, so restarting the scene moved to Backspace. Explosions are an
  `OverlapSphere` with impulses falling off to the radius and a little lift, setting off `ExplosiveBarrel`s on a short
  fuse so they chain. Tracers and explosion flashes are `DebugDraw` shapes until view models (M9.12) and particles
  (M13). While a prop is carried, the carrier has the click for that frame (`PropCarrier::HasClick()`), which input
  ownership (M2) will replace.
- M1.13: the tool gun is the last item in hand (6), with R cycling weld, rope and remover. Undo became an
  `UndoList` on the player that spawning and the tool gun share: undoing a joint removes the component at the place
  it was added, and undoing a removal re-instantiates the entity saved before it went, with its id
  (`InstantiateOptions::keepIds`), so joints to it are made again. Ropes are `DistanceJoint`s drawn with
  `DebugDraw` lines.
- Done 2026-10-09. Acceptance: `spot_sandbox_tests` walks the player through the level (stairs and ramps of mesh
  colliders, crouching, noclip), carries and throws props, grabs, turns and freezes them with the physics gun,
  welds and ropes them with the tool gun, and shoots and blows them apart, with undo. MSVC and GCC Debug build
  without warnings (GCC's `-Wmissing-field-initializers` asked for default member initializers in a few structs
  initialized by designators) and pass every test, the module tests included.

---

## M2 – Input actions and gamepads

Design: actions are data in the input settings, not code. Each action has a type (button, axis, 2D vector) and
bindings with processing (dead zone, invert, scale). A player's rebinds are saved in the game's user data folder.

- [x] **M2.1 Gamepads.** SDL3 gamepad events become engine events (connected, disconnected, button, axis), and
  `InputState` tracks each gamepad. Add `Input::IsGamepadButtonDown/Pressed/Released`, `GetGamepadAxis`, rumble, and
  a choice between one gamepad and any gamepad. Tests with synthetic events.
- [x] **M2.2 Text input.** A `TextInputEvent` from SDL text events, `Window::StartTextInput()`/`StopTextInput()` with
  the IME area, and `Input::GetTypedText()`. Tests.
- [x] **M2.3 Actions.** An `InputActionMap` of actions and bindings: keys, mouse buttons, mouse delta, scroll, gamepad
  buttons and axes, and composites (four buttons into a vector). Values and press/release edges per action, each
  frame. Add `Input::IsActionDown/Pressed/Released`, `GetActionValue` and `GetActionVector2`. It also works from code
  alone. Tests.
- [x] **M2.4 Action settings.** The input settings section, its project settings editor (action list, binding capture
  by pressing the input), and default actions in the project templates (Move, Look, Jump, Fire, Pause).
- [x] **M2.5 Rebinding.** Runtime rebinding, saving and loading per user, and reset to defaults. Tests.
- [x] **M2.6 Input ownership.** A capture stack, so overlays (console, UI text fields, the editor) can withhold input
  from the game without changing the game's requests: the cursor lock comes back afterwards, and the mouse delta is
  zero on each hand-over. The editor's play view uses it. Tests.
- [x] **M2.7 Sandbox: controls.** The sandbox and the templates use actions. Weapons are selected with 1–9 and the
  wheel, aiming down the sights uses the right button or the left trigger, and the whole game plays with a gamepad.

**Done when:** the sandbox plays the same with keyboard and mouse or a gamepad, and a rebind survives a restart.

**Notes:**
- M2.1: "one gamepad or any gamepad" is an index argument, `Input::AnyGamepad` (-1) by default; gamepads are
  numbered in the order they connected, and events from a gamepad not yet said to connect add it. The SDL backend
  opens gamepads as they are added and flips the sticks' Y so up is positive; axes are raw, as dead zones are the
  actions' processing (M2.3). Rumble is `Platform::RumbleGamepad()`.
- M2.2: `TextInputEvent`, `Window::StartTextInput()` and the IME area already existed (from the editor's text
  fields); this step added `Input::GetTypedText()`, input method compositions (`TextCompositionEvent`) and the
  cursor offset in `SetTextInputArea()`.
- M2.3: a binding's value is a vector processed by its dead zone (radial for vectors), inversions and scale; the
  action takes the binding farthest from rest, and button actions are down from `pressPoint`. Presses also come
  from a bound button's press this frame, so taps within a frame are not lost. The application owns the map
  (`Application::GetInputActions()`) and updates it right after polling events, from whichever state is current, so
  a game played in the editor reads the play session's input. Maps have a `SetGamepad()` for local multiplayer.
- M2.4: `InputSettings` (section "input") holds the actions; it is empty unless a project sets it, so older games
  have no actions, and new projects get `InputSettings::MakeDefault()`. The application applies it to its map at
  start, and `SetGameSettings()` empties the map first. The panel captures from the editor's own input
  (`Application::GetInputState()`, through `EditorContext::GetInput()`) with `FindPressedControl()`, starting on the
  mouse press so neither the click nor keyboard navigation is captured; Escape cancels. Look's right stick is
  scaled by 10 to match a mouse's pixels per frame.
- M2.5: actions keep `defaultBindings` (runtime only, set by `InputActionMap::SetDefaults()`); rebinds files hold
  only the actions that differ, so new defaults reach the rest. The game's user data folder is new:
  `ApplicationConfig::userDataDirectory`, else the platform's "Spot Games/<game name>" (an organization setting can
  come with save data, M10.7). Only games with actions read rebinds at start, so other applications never create the
  folder; `SetGameSettings()` (tools playing games) uses the project's bindings.
- M2.6: the capture stack is in `InputState`, the state the game polls: while captured, input events no longer reach
  it (the cursor position still does), the first capture releases what was held, and the applied lock is
  `IsCursorLockApplied()`, which the application and the editor give the window. Mouse deltas are dropped for the
  rest of the frame and the next on each hand-over and on each cursor lock change, when the jump arrives. Events
  are still delivered, so overlays read them. The play session's "Editor" capture replaced its own focus handling.
- M2.7: the sandbox's 38 actions are in `Sandbox.spot`: Move (WASD, left stick), Look (mouse, right stick), Jump
  (Space, South), Crouch (Ctrl, East), Sprint (Shift, left stick press), Fire (left button, right trigger), Aim
  (right button, left trigger), Use (E, North), Reload (R, West), Switch Weapon (wheel, shoulders), Weapon 1-9,
  Flashlight (F, D-pad up), Noclip (V, down), Undo (Z, left), Unfreeze (U, right), Spawn (Q, right stick press: the
  next prop each time, as gamepads have no buttons to spare), Spawn 1-8 (F1-F8), Clean Up (X), Pause (Escape,
  Start), Restart (Backspace, Back), and the debug keys. Switch Weapon is an axis with a low press point: a notch is
  a step and a shoulder a tenth of one each frame, which the physics gun uses to push and pull. Aiming magnifies the
  head's camera by the weapon's `aimZoom`, slows the look as much and narrows the spread (`aimSpread`); with the
  physics gun Aim freezes, and with a carried prop it turns it. Firing rumbles the gamepad. Engine changes it took:
  `InputAxes` for mouse and wheel bindings, and edges for a release and a press of a held control within a frame.
  The Code3D template's Rotator turns faster with Move. `spot_sandbox_tests` reads the cooked `game.json`'s actions
  and plays with a gamepad.
- Done 2026-10-10. Acceptance: `spot_sandbox_tests` plays the playground through the same actions with keyboard and
  mouse (every earlier test, unchanged) and with a gamepad (walking, turning, jumping, switching, firing, reloading
  and aiming), and the application tests save a rebind and load it on the next run, from `input_rebinds.json` in the
  game's user data folder; the sandbox gets a controls menu to rebind from with the runtime UI (M12). MSVC and GCC
  Debug build without warnings (GCC's `-Wmissing-field-initializers` asked for a few more default member
  initializers) and pass every test, the module tests included.

---

## M3 – Audio in scenes

Design: the mixer stays engine code (`Detail::Mixer`). Spatial audio is computed on the game thread each frame from
the listener and the sources, and sent to the mixer as per-voice gains and pan. HRTF waits. Buses form a small graph
defined in the audio settings. Streamed sounds decode on a worker thread into a ring buffer the mixer reads.

- [x] **M3.1 Buses.** A bus graph in the mixer (Master plus user buses, each with volume, mute and a parent), voices
  routed to a bus, `AudioDevice` bus control, and peak meters per bus read without locks. Tests on `Mixer`, no device
  needed.
- [x] **M3.2 Voice management.** Voice limits, priorities and stealing, short fades on start and stop to avoid
  clicks, and ramped volume and pan changes. Tests.
- [x] **M3.3 Streaming.** Sounds marked "stream" in their import settings stay encoded and decode while they play.
  Bump the sound importer's version. Tests.
- [x] **M3.4 Sources and listener.** `AudioListener` and `AudioSource` components. A source has a sound, a bus,
  volume, pitch, loop, play on start, spatial on or off, min and max distance, a rolloff curve, and `Play()`,
  `Stop()` and `Pause()`. Add `Audio::PlayOneShot(sound, position, bus)`. An `AudioSystem` (M0.2) updates gains and
  pan from transforms. A scene's sounds stop when it unloads and pause when scenes are paused. Tests.
- [x] **M3.5 Mixer settings and panel.** The audio settings section (buses, default volumes) and an editor Audio Mixer
  panel with faders, meters, mute and solo, which take effect live during Play.
- [x] **M3.6 Sandbox: sound.** The generator gets a WAV writer and synthesizes the game's sounds: gunshots,
  explosions, impacts per surface, footsteps, the physics gun's hum, and UI clicks. Weapons and explosions sound
  from where they are. Impacts come from collision events (M1.3) and scale with speed and surface. Footsteps follow
  the ground under the player. Ambience and music play on their own buses.

**Done when:** sandbox sounds come from where their entities are, and the mixer panel changes them while playing.

**Notes:**
- M3.1: buses are 32 fixed slots whose index is the `AudioBusId` (Master = 0), reused after removal, so the audio
  thread never allocates and `GetBusPeak()` reads per-slot atomics without the lock; the peak is that of the last
  mix call, after the bus's volume. Mixing goes in chunks of 256 frames through per-bus scratch buffers made once,
  deepest buses first. Voices for missing buses play on the master one; removing a bus moves its voices and children
  to its parent. The master volume is the master bus's.
- M3.2: one global voice limit (64) with `PlayParams::priority` (steal the oldest of the lowest, or refuse when all
  rank above) and `maxInstances` per sound. Stops, pauses, resumes and steals fade over 5 ms; a fade in on start is
  only `PlayParams::fadeIn`, since sounds start at their first sample, which is where they were cut. Volume, pan and
  bus gains step toward their targets a frame at a time, a full-range change taking 5 ms. Stopped voices keep
  mixing while they fade but no longer match their instance or count as voices.
- M3.3: streamed sounds keep the source's bytes (WAV or Ogg; the sound importer is at version 2, with a stream flag
  in its format). Each playback owns a `Detail::SoundStream`: its own `SoundDecoder` (dr_wav or stb_vorbis, which
  live in sound.cpp) and a single-producer ring of 0.5 s, prefilled in `Play()` outside the mixer's lock. The
  mixer's stream thread starts with the first streamed sound and refills every 50 ms; `Mixer(rate, false)` and
  `UpdateStreams()` keep tests deterministic. A dry stream holds its position; a streamed voice reports its end a
  frame after a decoded one would.
- M3.4: `Audio::PlayOneShot()` takes the scene too, so one-shots belong to a scene's `AudioSystem`, pause and stop with
  it, and follow its listener. Buses are named by string on sources (resolved with `Audio::FindBus()` on play),
  since bus ids are the device's. The listener is the scene's first `AudioListener`, else its main camera; without
  either, spatial sounds play unchanged. Pan is the dot of the direction with the listener's right; no Doppler or
  occlusion yet. `Application::SetScenesPaused()` pauses every scene's `AudioSystem`; sources paused by the game
  stay paused on resume. `AudioConfig::manualMix` with `AudioDevice::Render()` lets tests play sounds without an
  output.
- M3.5: `AudioSettings` ("audio") holds the master volume and buses by name (parents by name too); `Apply()` adds
  missing buses first, then parents, so order does not matter, and never removes buses code added. Solo is the
  mixer's (`SetBusSolo()`): a voice is heard while any bus is soloed only if its bus or one above it is; voices on
  the master bus itself are silenced too. Solo is not saved; mute is. The Audio Mixer panel edits the project's
  settings, applies them to the editor's device at once (the play session shares it) and saves when an edit ends;
  it docks with the console and starts closed. New projects get Music, Effects, Ambience and Interface buses.
- M3.6: the generator synthesizes 19 mono WAVs (`sandbox/generator/sounds.cpp`) with arithmetic only (a polynomial
  sine, xorshift noise, one-pole filters, multiplicative decays), as the textures do; music and wind are 22 kHz
  loops whose metas stream them. Sounds are data: a `SoundBank` component (surfaces' impacts and footsteps,
  explosion, click), `Weapon::fireSound`/`reloadSound`, and a "Physics Gun Hum" child with an `AudioSource` that
  `PhysicsGun::hum` names. A `Surface` component (Concrete, the default, Wood, Metal) on props; `ImpactSounds` plays
  the entity's own surface on `OnCollisionEnter()` past 1 m/s, louder to 8 m/s, detuned ±8%, quiet for 0.08 s
  after each and 0.5 s after start. Footsteps every 0.8 m on the ground, from a raycast below the feet. Interface
  clicks go straight to the device's Interface bus, so they play while the scenes are paused. `OneShotParams`
  gained `priority` and `maxInstances` (explosions rank 10, shots 5). `spot_sandbox_tests` plays the playground
  through a manually mixed device and measures each bus.
- Done 2026-10-10. Acceptance: `spot_sandbox_tests` plays the playground with sound and checks that music and wind
  play on their buses, steps, shots, impacts and the physics gun's hum are heard, and an explosion to the player's
  right is heard on the right; the editor's Audio Mixer changes the buses of the device the play session sounds
  through, at once. MSVC and GCC Debug build without warnings (GCC asked for default member initializers in the
  mixer's voices) and pass every test, the module tests included.

---

## M4 – Fast game-code iteration

Game code is C++, so the time from saving a file to seeing it in the editor is the engine's "script reload" time. The
budget is a few seconds for a one-file change in Debug.

- [x] **M4.1 Measure.** The editor logs how long each module build phase takes (configure, compile, link, load).
  Record the baseline in Notes.
- [x] **M4.2 Precompiled headers.** `spot_add_game()` precompiles `<spot.h>` and the standard headers it pulls in.
  The engine's own targets use PCH where it pays off.
- [x] **M4.3 Compiler cache.** Use sccache or ccache as the compiler launcher when one is installed, for the engine
  and for games. Document it.
- [x] **M4.4 Header cost.** Profile the include cost of `spot.h` (`-ftime-trace`) and cut the top offenders with
  forward declarations and by moving heavy includes out of widely used headers.
- [x] **M4.5 Skip needless work.** Configure only when the CMake files changed, build only what changed, and record
  the result against the budget in Notes.

**Done when:** a one-line change in a sandbox component reloads in the editor within the budget.

**Notes:**
- M4.1: `CodeBuild::GetPhases()` times configure, compile and link (link starts at Ninja's or Make's "Linking" or
  the MSVC linker's "Creating library"; whatever follows, such as the executable, counts as link), and
  `CodeBuild::Wait()` now follows the output as it comes, so the command line times it too. The editor logs the
  module's load and the scenes' reload, with the time since the change. Baseline on this machine, MSVC Debug with
  the Visual Studio generator, the sandbox's 21 files of code: a clean build 51.7 s (configure 2.0, compile 46.7,
  link 3.0); no change 0.7-0.8 s (MSBuild's up-to-date checks); a one-line change to a component 3.6-3.9 s (compile
  3.0-3.3 for one file, link 0.5-0.6 for the module and the executable); loading the module 25 ms. On top: the
  editor looks for changes once a second, and builds the game's executable it does not need. The first build after
  a source file is added fails once with the Visual Studio generator, as the glob check regenerates the project
  while it builds (for M4.5).
- M4.2: `spot_add_game()` precompiles `<spot.h>` for `game_code` (not for the executable's single `main.cpp`, whose
  position-independence differs). The sandbox's clean build went from 51.7 s to 10.3 s (compile 46.7 to 5.4), and a
  one-line change from 3.6-3.9 s to 1.4-1.5 s (compile 0.8-0.9, link 0.5-0.7). In the engine's build,
  `spot_precompile_engine_header()` does the same for the sandbox, the generator, the tools, the editor (with
  ImGui) and the tests (with Catch2); a one-file rebuild there gained 1-2 s at most, as MSBuild's checks of the
  targets around it dominate. The engine library itself has no PCH: MSVC's export of all symbols from
  `spot_shared` fails on the PCH's own symbol.
- M4.3: `cmake/spot_compiler_cache.cmake` finds sccache, then ccache, and sets the compiler launchers for the
  engine's build and, through the package, for `spot_add_game()`. ccache runs with
  `CCACHE_SLOPPINESS=pch_defines,time_macros`, so files built with the precompiled header are cached; with MSVC,
  debug information goes in the objects (`/Z7`), as caches need. Neither tool is installed on this machine, and the
  default MSVC preset uses the Visual Studio generator, which runs no launcher, so the gain is unmeasured here; a
  stand-in launcher on PATH showed a game's build through Makefiles calling it for all 21 compilations.
- M4.4: no Clang here for `-ftime-trace`, so MSVC's `/d1reportTime` profiled a file holding only `#include
  <spot.h>`: 1.6 s of includes, of which `<filesystem>` takes 0.6 s, as MSVC's brings `<chrono>`, `<format>`,
  `<algorithm>` and the locale headers with it. Paths are API types in 23 public headers (loading, saving, dropped
  files), so nearly every engine header costs about 1 s on its own; keeping `<filesystem>` out would mean an engine
  path type throughout the API, not worth it while precompiled headers (M4.2) pay this once per target. Cut
  instead: `log_sinks.h` keeps its `std::ofstream` behind a pointer (no `<fstream>`), and `asset_id.h`,
  `game_settings.h` and `archive.h` drop `<functional>` and `<algorithm>` they did not need. The total moved
  within noise, as `<filesystem>` brings what they did.
- M4.5: `CodeBuild` stamps the code folder's file names and its CMake files' times after configuring
  (`spot_sources.txt`), and configures again first when they change, so an added file builds on the first try
  (0.2 s of configuring). `CodeBuild::Start(target)` builds one target: the editor's builds on change make
  `game_module` alone (about 1.0 s for a one-line change, against 1.2-1.3 s with the executable), while running the
  game and Ctrl+B build everything. The editor looks for changes every 250 ms instead of every second. Against the
  budget, in the editor on the sandbox, MSVC Debug: a one-line change to a component reloads 1.0-1.2 s after it is
  seen (build 0.9-1.0 s, module load 21-25 ms, scenes reloaded in 120-140 ms), so at most about 1.5 s after it is
  saved, down from about 5 s at the M4.1 baseline.
- Done 2026-10-10. Acceptance: in the editor, a one-line change to a sandbox component reloads within the budget,
  about a second after it is seen (M4.5), and the module tests build, reload and run projects' code with the
  precompiled header and the new configure and target rules. MSVC and GCC Debug build without warnings and pass
  every test, the module tests included.

---

## M5 – Rendering: HDR and post-processing

Design: scenes render into an HDR target, and a post-processing stack turns it into the output. Each effect is a pass
built on the public graphics API. The look comes from a `PostProcessing` component (the first active one, as with
`Sky`) over the defaults in the rendering settings. MSAA resolves before post-processing.

- [x] **M5.1 Graphics API.** Float color formats (`Rgba16Float`, and `R11G11B10Float` where supported) as render
  targets, MSAA resolve into sampled textures, and `FullscreenPass` (one triangle and a fragment shader). GPU tests.
- [x] **M5.2 HDR and tonemapping.** `SceneRenderer` draws into the HDR target and tonemaps it (ACES or AgX, with
  exposure) into the output. Sky exposure stays consistent, and the editor's views use the same path.
- [x] **M5.3 Bloom.** A downsample and upsample chain, with intensity and scatter.
- [x] **M5.4 FXAA.** Optional FXAA next to MSAA, chosen in the settings.
- [x] **M5.5 Grading.** A LUT (strip format), contrast, saturation, white balance and vignette.
- [x] **M5.6 Fog.** Distance and height fog in the mesh shader through a `Fog` component (color, start, density,
  height falloff), applied before tonemapping and matched to the sky at the horizon.
- [x] **M5.7 Settings and component.** The rendering settings section (defaults, MSAA samples, shadow sizes and
  distance, which feed `Renderer3DConfig`), the `PostProcessing` component, and their inspectors.
- [x] **M5.8 Render hooks.** Stages in `SceneRenderer` (before opaque, after opaque, after transparent, before and
  after post-processing, overlay) where games and systems add passes, with a context holding the camera, targets and
  depth. `DebugDraw` moves onto them. Tests.
- [x] **M5.9 Sandbox: look.** Muzzle flashes, explosions and emissive props glow through bloom, and the level gets
  fog and grading that follow the day cycle. The physics gun's beam and the ropes are drawn in a render hook.

**Done when:** the sandbox renders through HDR, bloom, grading and fog, and its beam draws at a chosen stage.

**Notes:**
- M5.1: `TextureFormat::Rgba16Float` (8-byte pixels) and `R11G11B10Float`, with `GetBytesPerPixel()` for uploads and
  downloads and `GraphicsDevice::SupportsRenderTarget()` (both render and sample on the test machine's Direct3D 12).
  Resolving MSAA into sampled textures already worked, single-sample render targets being sampled too; a GPU test
  now resolves a 4x float target with values above 1 and samples it. `FullscreenPass` owns an engine vertex shader
  (`fullscreen.vert.hlsl`, one triangle from `SV_VertexID`), the caller's fragment shader and the pipeline; its
  targets have no depth.
- M5.2: `Tonemapper` (on `FullscreenPass`) offers ACES (Stephen Hill's fit), AgX (Benjamin Wrensch's minimal fit) and
  Linear, with exposure and sRGB-space dithering. `SceneRenderer` draws meshes, debug lines and sprites into an
  `Rgba16Float` image (MSAA when supported, resolved into it), tonemaps it into the target in one pass that the
  interfaces then draw over in the target's format, with a second Renderer2D. The sky keeps its own curve and
  dithering into displayable targets (Renderer3D used directly, as in most GPU tests); with `Renderer3DConfig::hdr`
  it outputs its light times its exposure, the core of the sun unbounded, and the tonemapper dithers. The editor's
  views draw the same way (`ViewportTargets` gained the HDR image; the grid draws into it, the interfaces after the
  tonemapper), with `ViewportOptions::tonemapping`; thumbnails stay direct.
- M5.3: `Bloom` keeps two chains of up to 7 half-sized `Rgba16Float` levels: down with Jimenez's 13-tap filter, the
  first level soft-thresholded (knee of half the threshold) and clamped to 64 against fireflies; up with a 3x3 tent,
  each level lerped toward the spread level below by `scatter`. The tonemapper adds the half-sized result times
  `intensity` before exposure, or a 1x1 black texture without bloom. Defaults: intensity 0.3, threshold 1, scatter
  0.7, so only light above white glows.
- M5.4: `Fxaa` is Lottes' compact FXAA (edge direction from four diagonal lumas, two blends along it, the wider one
  dropped when it leaves the local range), on perceptual luma (square root of linear). With FXAA on, the tonemapper
  draws into a texture of the target's format, FXAA draws that into the target, and the interfaces go over it. Off by
  default; the rendering settings (M5.7) choose it.
- M5.5: grading lives in the tonemap pass, no pass of its own. Before the curve: a white balance scaling red and blue
  (temperature) or green (tint) by up to 30%, renormalized to keep luminance; contrast as a scale of stops around
  0.18; saturation around Rec. 709 luma. After it: a radial vignette from `vignetteStart` to the corners, and a
  strip LUT sampled in sRGB with blue interpolated between slices, blended by `lutContribution`. `GradingSettings`
  takes the LUT as a `const Texture*` until the `PostProcessing` component (M5.7) holds it as an asset.
  `MakeNeutralLut()` makes the identity strip.
- M5.6: the fog's optical depth is integrated in closed form along the ray from where it starts:
  `density * fogged * exp(-falloff * startHeight) * (1 - exp(-rise)) / rise`, with `rise = falloff * dir.y *
  fogged`. The mesh shader blends the lit color toward the fog color before tonemapping (uniforms at b3), and the
  sky shader applies the same to a ray 100 km long, which meets the fogged ground at the horizon. The base height is
  the `Fog` entity's world Y. Fog in an LDR target blends after the sky's own curve.
- M5.7: a `PostProcessing` component replaces the defaults whole (no per-field overrides); `SceneRenderer` and the
  editor's views look for the first active one in the scenes they draw each frame. `RenderingSettings` holds the
  defaults (without a LUT) and the quality, which `Apply()` puts in a `SceneRendererConfig` (which gained the
  shadow sizes and distance for its Renderer3D); the application makes its SceneRenderer from them, and again after
  `SetGameSettings()`. The editor's views take the project's look and FXAA, but keep their own MSAA and shadow
  quality. Textures gained the `srgb` import setting (texture importer 2, model importer 4, as both write
  `TextureFile`), so LUTs, and later normal maps, load as `Rgba8`. Inspectors come from the fields' `Serialize()`.
- M5.8: hooks live on scenes (`Scene::AddRenderHook()`, for components and systems, run wherever the scene draws) and
  on `SceneRenderer` (`AddHook()`, run first). The frame now clears in a pass of its own before `BeforeOpaque`, and
  every HDR pass keeps its samples and resolves, so a hook at any stage can draw more and the HDR image is always up
  to date for `BeforePostProcessing`; after post-processing, the target, the interfaces and the overlay get passes
  of their own. `DebugDraw` is the renderer's own `AfterOpaque` hook. The editor's views run the scene's hooks at
  the same stages (their own lines and grid stay built in). "Transparent" is the sprites until M6 brings
  transparency.
- M5.9: materials gained `emissive` (material importer 4, model importer 5); the lamps have bulbs of `bulb_*.material`,
  six times their light's color. `Flash` entities (a fading point light and an emissive sphere) mark shots (0.05 s)
  and explosions (0.35 s, replacing the debug sphere); tracers are drawn three times brighter than white. The sun's
  entity has a `Fog` and a `PostProcessing`, which `DayCycle` sets from the sun's height: pale haze by day, warmer
  and denser at sunset, dark blue and denser at night with a raised exposure and a cooler white balance. The
  physics gun's beam and the ropes are drawn by `AfterOpaque` render hooks through `HookLines` (a LineRenderer per
  stage's format and sample count, which needed `RenderStageContext::device`), the beam bright enough to bloom.
- Done 2026-10-10. Acceptance: the sandbox renders through an HDR image with bloom, grading, its day cycle's fog and
  ACES (seen in the editor's view, which takes the same path), and its beam and ropes draw in an `AfterOpaque` hook;
  GPU tests cover float targets, fullscreen passes, tonemapping, bloom, FXAA, grading, fog, the rendering settings
  and `PostProcessing`, and the stages of the hooks. MSVC and GCC Debug build without warnings and pass every test,
  the module tests included.

---

## M6 – Rendering: PBR and transparency

Design: metallic-roughness PBR, as glTF defines it, with GGX shading. Ambient light comes from the sky through
image-based lighting. Materials are Opaque, Cutout (alpha test) or Transparent (blended, sorted back to front, no
depth writes). Vertices gain tangents.

- [x] **M6.1 Tangents.** `MeshVertex` gets tangents, which glTF provides or are generated (MikkTSpace). `MeshData`
  builders generate them. Bump the mesh and model formats and importers. Tests.
- [x] **M6.2 PBR materials.** Metallic, roughness, normal map with scale, metallic-roughness map, occlusion map, and
  emissive color and map. Bump the `.material` format and its importer, which reads the old fields. The glTF importer
  reads every PBR input, and the material inspector and thumbnails follow. Remove specular and shininess, and
  migrate the sandbox's materials.
- [x] **M6.3 Shading.** Cook-Torrance with GGX, Smith and Schlick, inverse-square falloff windowed by range, and light
  intensities in documented units.
- [x] **M6.4 Sky lighting.** Cube textures and rendering into their faces in the graphics API. Render the sky into a
  cube map when it changes, prefilter it for specular, and take irradiance as spherical harmonics. GPU tests.
- [x] **M6.5 Alpha modes.** Cutout and transparent pipelines, sorting, cutout shadows, and glTF `alphaMode` import.
- [x] **M6.6 Golden-image tests.** GPU tests render reference scenes and compare them to stored images within a
  tolerance, so rendering changes are seen before they ship.
- [x] **M6.7 Sandbox: materials.** The generator makes PBR texture sets (wood, painted metal, rusty metal, concrete,
  rubber, glass) and low-poly weapon models. Props and the level use them, glass panes are transparent, and the tool
  gun gets the color and material tool.

**Done when:** glTF sample models (such as DamagedHelmet and a transparent model) look as they do in reference
viewers, and the golden-image tests cover them.

**Notes:**

- M6.1: MikkTSpace is fetched and compiled through `engine/graphics/mikktspace.cpp`, which includes its C source
  with warnings off. `GenerateTangents()` splits a vertex whose corners need different tangents (across mirrored or
  cut texture seams), and gives surfaces without usable texture coordinates any tangent perpendicular to the normal.
  glTF tangents are only used along the file's own normals; they turn with their node, and their sign flips under a
  mirroring node. The mesh format is the raw vertex array, so the model importer went to version 6 and models
  reimport.
- M6.2: Generated tangents feed MikkTSpace -v, so the bitangent points toward the texture's top, as glTF's normal maps
  (+Y up) need. Materials default to metallic 0 and roughness 1, matte as in glTF, so plain materials still show their
  color. Roughness only means something with a microfacet lobe, so the GGX specular term (height-correlated Smith,
  Schlick) came here, scaled by pi to match lights whose colors are what a white surface facing them scatters; M6.3
  adds the falloff and light units. Even matte dielectrics reflect about 1% of a light facing them, so the GPU tests'
  color tolerance went from 2 to 3. Old `.material` files get a roughness of (2 / (shininess + 2))^1/4, the width of
  their highlights, or 1 without them, and the sandbox's were migrated. Model images are sRGB unless only used as
  data. The `.material` importer is at version 5 and the model importer at 7.
- M6.3: Light units stay relative to exposure rather than photometric: 1 is the illuminance that makes a white matte
  surface show white at exposure 0, so the existing sun, sky and exposure settings keep working; point and spot
  intensities are that at one unit, falling off as 1/d^2 (clamped at a centimeter) times (1 - (d/range)^4)^2. Diffuse
  stays Lambert without a (1 - F) factor, as in Filament and Unreal. The sandbox's lamps and flashes were rescaled to
  be as bright as before at 40% of their range (intensity times (0.4 range)^2), and point and spot lights default to
  an intensity of 10.
- M6.4: The sky's light is captured in surface units (radiance times pi, multiple scattering included, scaled by
  `ambientIntensity`), so its irradiance matches what `GetSkyLighting()` computed on the CPU before; the ground below
  the horizon is its color lit by the ground light, and the sun's disc and fog are left out. Cubes are 64 texels, with
  5 roughness levels prefiltered by 64 GGX samples each that read blurrier levels where the lobe is wide. The
  irradiance's nine coefficients are computed on the GPU into a 9x1 `Rgba16Float` target, from the 16-texel level of
  each face, so nothing is read back. The environment's specular uses Karis's analytic fit of the split-sum BRDF
  instead of a lookup table, and ambient lights now reflect too, as a uniform environment. A capture happens whenever
  the `SkyData` differs from the last one (`SkyData` gained `operator==`).
- M6.5: Transparent surfaces blend premultiplied (the graphics API gained `BlendMode::Premultiplied`): what they
  scatter is weighted by alpha, while highlights, reflections and emission are added whole, so glass keeps its
  shine. They are sorted by the distance from the camera to the center of their bounds and cast no shadows. Cutout
  shadows go through a shadow pipeline whose fragment shader samples the base color texture by the mesh's UVs (the
  shadow vertex layout gained UVs), even for textures projected from world space. The `.material` importer is at
  version 6 and the model importer at 8.
- M6.6: Golden-image tests live in `tests/golden_tests.cpp`, in `spot_gpu_tests`, and render 160x120 sRGB images
  through `SceneRenderer` with one sample, no dithering and no bloom. DamagedHelmet and AlphaBlendModeTest are
  downloaded at configure time from glTF-Sample-Assets at commit edc7c9e, checked by SHA-256, rather than committed
  (7 MB); their test is skipped without them. `SPOT_UPDATE_GOLDEN=1` rewrites the images.
- M6.7: The generator paints each surface from a height field, so its normals match its relief, and packs occlusion,
  roughness and metallic into one ORM texture. Props keep their painted color textures and gain their surface's normal
  and ORM maps through their glTF materials. The weapons are glTF models of several parts, one per surface, which a
  `ViewModel` component on the player shows in front of the eyes as entities of their own, without shadows; each
  `Weapon` names its model, and the `ViewModel` names the physics and tool guns'. The tool gun's materials are asset
  references in the scene (`ToolGun::materials`), not paths, so they load with the scene; its Color mode copies the
  prop's material with the new color, keeping its alpha, and both new modes go on the `UndoList`. Glass panes stand
  in view of the start and along the platform's edge. The level projects concrete (8 m) underfoot and cyan painted
  metal (4 m) on its blocks.
- Done 2026-10-10. Acceptance: DamagedHelmet and AlphaBlendModeTest render with every PBR input (normal, ORM,
  emissive, occlusion) and their opaque, cutout and blended alpha as reference viewers show them, and the golden-image
  tests cover them with PBR spheres under the sky, cutout and transparent surfaces with shadows and a normal-mapped
  wall; GPU tests cover tangents, cube textures and their faces and mip levels, the sky's captured light, GGX
  highlights, inverse-square falloff and the alpha modes. The sandbox's props, level, weapons in hand and glass panes
  use the generated PBR surfaces, and the tool gun paints and gives materials. MSVC and GCC Debug build without
  warnings and pass every test, the module tests included.

---

## M7 – Rendering: scale

- [x] **M7.1 Culling.** Frustum culling in every pass, a per-camera culling mask by entity layer (M1.1), and visible
  and culled counts in the profiler.
- [x] **M7.2 Instancing.** Batch draws by mesh, material and pipeline, with per-instance data in a storage buffer,
  automatically in `Renderer3D`.
- [x] **M7.3 Clustered lights.** Assign lights to a froxel grid on the CPU and send it in storage buffers, raising
  `MaxLights` from 16 to hundreds. Shadowed lights keep their budget. Tests.
- [x] **M7.4 Sorting.** Sort opaque draws by pipeline, material and depth to minimize state changes and overdraw.
- [x] **M7.5 Shadow quality.** Soft PCF, stable cascades (texel snapping), blending between cascades, and caster
  culling per cascade.
- [x] **M7.6 Benchmark.** A benchmark scene with 10,000 meshes and 200 lights. Record its Release frame times in Notes
  and check them when rendering changes.
- [x] **M7.7 Sandbox: lamps.** The tool gun places lamps (point and spot lights, shadowed or not), and many of them
  light the level at once.

**Done when:** the benchmark holds 60 FPS in Release on the development machine, and so does the sandbox with dozens
of lamps.

**Notes:**

- M7.1: Shadow passes already culled casters by their views; the view's pass now tests each mesh's world bounding
  sphere against the camera's frustum in `End()`, so meshes drawn with `DrawMesh()` are culled too. The profiler
  gained per-frame counters (`Profiler::Count()`, `ProfileFrame::counters`) for the counts, since the editor has no
  profiler panel yet and the HUD and tests read frames directly. The culling mask is a field of `Camera3D`, which
  `DrawScene()` applies to mesh renderers, shadows included; lights, skies and sprites ignore it. `FieldOptions`
  gained `layerMask`, which the inspector draws as a combo of the project's named layers to tick.
- M7.2: The graphics API gained storage buffers (`BufferUsage::Storage`, bound per stage). The instance buffer holds
  each command's model and normal matrices in the sorted commands' order, uploaded once per `End()` with
  `UploadMode::Discard` so several views a frame keep their own; a batch is a run of commands in view that look the
  same (same mesh, textures and material values; for shadows, the same mesh and cutout), drawn with a pushed
  `firstInstance`, since `SV_InstanceID` does not include the draw's first instance on Direct3D. Material values are
  in the sort key, so copies of a material batch too. Transparent surfaces batch only when adjacent after their
  back-to-front sort, which keeps their order.
- M7.3: `MaxLights` is 256. The froxel grid is 16x9x24, its slices even in log depth from the near plane to the far
  plane or 500 units; the lights' tiles come from projecting the corners of their spheres' view-space boxes, and a
  sphere that crosses the near plane covers every tile. Lights, cluster ranges and index lists are fragment storage
  buffers (registers t9 to t11, after the nine samplers), uploaded per `End()` like the instances; the camera's
  forward axis and the grid's parameters moved into the lighting constant buffer, which no longer holds the lights.
  Directional lights come first in the light list and every pixel goes through them.
- M7.4: The opaque sort key is pipeline (sidedness and alpha mode), material (textures, then values), mesh, then
  depth of the bounds' center along the camera's forward axis, so a batch's instances draw front to back. Batches
  across materials are not reordered by depth, which would trade state changes for overdraw. The view's pass skips
  rebinding meshes, textures and material values that did not change, and `Renderer3DStats` counts pipeline and
  material changes.
- M7.5: Stable cascades (a sphere around each slice, rounded, its view snapped to whole texels), blending between
  cascades and culling casters per view came with M4's shadows; this step made the rest explicit. PCF went from a 3x3
  grid to a fixed 16-tap Poisson disk of 1.5 texels' radius, which stays inside the three-texel margin views draw
  with and is not rotated per pixel, since rotation trades banding for noise that only temporal antialiasing would
  hide. Pipelines clamped depth only by accident (SDL's rasterizer state left zeroed); `DepthState::clamp` now asks for
  it, shadow pipelines set it, and the others clip.
- M7.6: `spot_benchmark` (`tests/benchmark/`): 10,000 cubes and spheres in a 100x100 grid with 8 materials, 200 point
  and spot lights (8 casting shadows), the sun's 4 cascades and the sky, at 1600x900 without vsync, timing 600 frames
  after 120. Release on the development machine (Direct3D 12), 2026-10-10: mean 10.17 ms (98 FPS), median 10.10 ms,
  95th percentile 10.84 ms, worst 12.71 ms; 10 draw calls and 51 shadow draw calls for about 7,000 meshes in view.
  The first run took 20.1 ms, nearly all of it on the CPU in `Renderer3D::End()`, which led to: meshes in view sorted
  first, so culled ones do not split batches; shadow views reading their casters through a list of indices, kept in
  one frame-wide order, so batches survive each view's culling; that list uploaded once per `End()` (into a buffer
  first cycled to fresh memory and sized for every view, then filled without cycling, so the draws recorded meanwhile
  read it) rather than once per view, which alone cost 6 ms; frustum planes extracted once per view
  (`Detail::Frustum`); materials compared through a look id from a hash map instead of field by field; clusters built
  in flat arrays; and normal matrices from cofactors instead of full inverses. The benchmark prints the costliest
  profiler scopes, and `Renderer3D` profiles its stages and counts draw calls.
- M7.7: The tool gun's sixth mode places lamps: Aim goes through point and spot lamps, shadowed or not, and the
  palette's color tints them, brightened to full (black gives white). A lamp is a small glowing bulb tagged `Lamp`,
  facing out of the surface clicked, a child of the prop clicked so it moves with it; it has no collider, so the
  remover takes away the lamp nearest where it is clicked, within 0.35 m, before props. Lamps go on the `UndoList`.
  `spot_benchmark --scene` times a scene with lamps spread over it: the playground with 48 lamps (6 shadowed) ran at a
  mean of 3.04 ms (329 FPS) in Release.
- Done 2026-10-10. Acceptance: in Release on the development machine, the benchmark's 10,000 meshes and 200 lights
  hold 98 FPS (10.2 ms a frame, 95th percentile 10.8 ms), and the sandbox's playground with 48 lamps 329 FPS. GPU tests
  cover culling and culling masks, instancing in every pass, clustered lights past the old 16, sorting by pipeline and
  material, soft shadow edges and casters beyond the cascades; unit tests cover the light clusters and profiler
  counters, and sandbox tests the lamps. MSVC and GCC Debug build without warnings and pass every test, the module
  tests included.

---

## M8 – Model hierarchy and prefabs

Design: models keep their node hierarchy, and instantiating one makes one entity per node. A prefab is an asset
holding an entity tree. An instance stores the prefab's id and only its overrides: field changes as JSON pointers per
prefab-local entity id, plus added and removed components and entities. Loading applies the overrides onto the
prefab's current content, so prefab changes reach every instance. Prefabs nest.

- [x] **M8.1 Model nodes.** Model import keeps nodes, with their names and transforms, a mesh per node, and
  primitives as submeshes. `Model` exposes the node tree, and `Renderer3D::DrawModel()` keeps working. Bump the model
  format. Tests.
- [x] **M8.2 Model instances.** `Scene::Instantiate()` of a model creates an entity per node, and `MeshRenderer` can
  draw one submesh. Models dropped in the editor create that hierarchy.
- [x] **M8.3 Prefab assets.** A `.prefab` source in the scene format, its importer, `AssetType::Prefab`, and
  `Scene::Instantiate(AssetRef<Prefab>)` at runtime. Tests.
- [x] **M8.4 Instances and overrides.** Prefab-local entity ids, instances saved as a prefab id plus overrides in
  scene files, and overrides applied on load. Tests: propagation, overrides kept across prefab edits, removed targets.
- [x] **M8.5 Prefab editing.** Create a prefab by dragging entities into the asset browser, edit it in prefab mode
  (a `.prefab` opened as a document), see overrides in the inspector, Apply or Revert per field, component or
  instance, and Unpack.
- [x] **M8.6 Nesting and variants.** Prefabs inside prefabs, and variants that override another prefab.
- [x] **M8.7 Live updates.** Saving a prefab updates the instances in open scenes, keeping undo valid.
- [x] **M8.8 Sandbox: spawnables as prefabs.** Every prop, lamp and weapon is a prefab, and the spawn list is built
  from them, so a new spawnable is an asset rather than code.

**Done when:** the sandbox's spawnables are prefabs, and a change to one reaches every instance while each keeps its
own overrides.

**Notes:**

- M8.1: Models keep the default glTF scene's nodes in parent-before-child order, including empty nodes, with local
  matrices and references to shared meshes. Primitives become index ranges of one mesh, with a material per range;
  `Model::GetParts()` keeps a flattened draw list with accumulated node transforms for `DrawModel()`. Model importer
  version 9 stores the hierarchy and mesh ranges. Import scale applies to vertex positions and node translations.
  Draws, instancing and shadows use submesh ranges, and mirrored transforms reverse culling and tangent handedness.
  Thumbnails, the sandbox's view models and existing flat scene instances now place the parts through their nodes;
  M8.2 replaces scene instances with the hierarchy. The serialized `MeshRenderer` submesh selector was added
  here so these callers can retain separate primitive materials through the format change.
  Validation: MSVC and GCC Debug build without warnings and pass all 573 tests, including GPU, golden images,
  editor, module and sandbox tests.
- M8.2: `Scene::Instantiate(const Model&, name)` creates a named wrapper root and one entity per node, with a
  `MeshRenderer` per drawable submesh on that node. Parents are built before children, and only local matrices are
  decomposed, preserving world-space shear from scaled parents. Shared meshes and materials stay shared, while
  entities receive independent ids. `InstantiateModel()` delegates to the scene API, and model drops in the editor
  save that same tree as one undoable addition, including empty nodes. Picking and selection masks now draw each
  renderer's selected index range. Tests cover node identity and transforms, sharing, serialization without a GPU,
  material changes, empty models, editor undo and redo, GPU scene drawing and submesh selection.
  Validation: MSVC and GCC Debug build without warnings and pass all 579 tests, including GPU, golden images,
  editor, module and sandbox tests.
- Rendering benchmark after M8.2, 2026-10-10: MSVC Release builds without warnings. On the development machine
  (Direct3D 12, GTX 1660), the 10,000-mesh and 200-light benchmark at 1600x900, measured over 600 frames after 120
  warmup frames, ran at a mean of 11.58 ms (86 FPS), median 11.20 ms, 95th percentile 14.22 ms and worst 18.80 ms.
  It keeps 10 draw calls and 51 shadow draw calls, and remains within the 60 FPS budget on average and at the 95th
  percentile. M7's recorded mean was 10.17 ms; this run is about 14% slower.
- M8.3: `Prefab` stores validated scene-format text for exactly one entity tree, without constructing components
  during import or asset loading. Prefab importer version 1 normalizes an external root parent away and rejects
  empty trees, duplicate or invalid ids, multiple roots and cycles. `AssetType::Prefab` is appended to preserve
  existing binary type values. Runtime instantiation always creates new ids, remaps internal entity references,
  clears external ones and accepts parent, sibling index and asset manager options. Assets load and instantiate
  from imported files alone, without a GPU. The editor recognizes prefab sources and drops their trees as one
  undoable addition; authoring and prefab mode remain M8.5. Instances still serialize as ordinary trees until M8.4
  adds identity and overrides. Tests cover independent instances, reference mapping, lifecycle, asset sharing and
  reload, corrupt payloads, unavailable components and editor undo/redo.
  Validation: MSVC and GCC Debug build without warnings and pass all 586 tests, including GPU, golden images,
  editor, module and sandbox tests.

- M8.4: Scene format version 2 saves linked instances as asset ids, prefab-local entity mappings and JSON-pointer
  patches. Persistent component UUIDs keep overrides attached through reordering, additions and removal; legacy
  prefab components receive deterministic identities. Arrays are atomic values. Added entities remain scene nodes,
  while inherited entity removal follows its source subtree. Missing targets retain their patches, so restoring
  a source identity restores its overrides without affecting replacements. References distinguish prefab-local
  and scene identities, including references between instances. Expanded snapshots support independent copies and
  source authoring before nesting. Scene and prefab importer versions are 2; version 1 scenes remain readable.
  Unavailable assets and invalid instance metadata reject loading before changing the scene. Editor history uses
  the document's asset manager, and mixed-property comparisons exclude component identity.
  Validation: MSVC and GCC Debug build without warnings and pass all 594 tests, including GPU, golden images,
  editor, module and sandbox tests.

- M8.5: Hierarchy drags into asset folders create sources, with selected descendants included under their selected
  ancestor, free file names and database-generated meta ids. Prefab documents preserve source-local identities,
  show a Prefab view label and require one tree when saved. The inspector lists field and structural overrides,
  marks changed component fields and offers Apply/Revert per field, component or instance, plus Unpack. Prepared
  edits use public engine APIs and do not mutate entities; editor commands replay them through the asset manager,
  restoring both source files and scenes on Apply undo/redo. Applying additions assigns source-local ids and remaps
  internal references; sources clear external references. Unpack preserves scene ids and leaves other instances
  linked. Sources expand links until M8.6, and updates of other open documents remain M8.7.
  Validation: MSVC and GCC Debug build without warnings and pass all 603 tests, including GPU, golden images,
  editor, module and sandbox tests; headless UI tests drive entity drags, prefab documents and override menus.

- M8.6: Prefab sources retain nested instance records; a linked root saved as a source becomes a variant. Source
  documents and Apply preserve those links, while runtime instances expose the containing prefab's expanded local
  identities. Deterministic ids for inherited additions keep overrides and references stable through dependency
  edits. Prefabs expose direct dependencies, and the asset manager caches their edges for transitive invalidation
  even after temporary asset references expire. Prefab importer version 3 accepts nested sources. Expansion rejects
  unavailable dependencies, cycles, depth beyond 64 and disconnected trees before changing the destination scene.
  Tests cover nested propagation, variant and scene override layers, source edits, stable additions, references,
  dependency reload, malformed mappings and editor authoring without baking dependencies.
  Validation: MSVC and GCC Debug build without warnings and pass all 612 tests, including GPU, golden images,
  editor, module and sandbox tests.

- M8.7: Asset refresh stages the compact records of affected open documents against current prefabs before
  replacing entities, keeping instance overrides, ids, selections, history and saved-state markers. Nested and
  variant dependencies propagate through the same reload graph; broken dependencies retain the document until a
  repaired import. Clean prefab source documents follow Apply from other scenes, while unsaved authoring edits
  remain intact. Play copies keep their current instances. Component commands resolve persistent UUIDs, field
  replay changes only edited values, and missing history targets are skipped safely. Undoing inherited entity
  deletion restores its containing instance against the current source, preserving links and later additions.
  Scene revisions invalidate inspector caches and finish in-progress edits by component identity. Tests cover
  multiple documents, source saves, Apply undo/redo, nested variants, component insertion and removal, inherited
  deletion with cross-entity references, missing targets, failed reload recovery and inspector mixed values.
  Validation: MSVC and GCC Debug build without warnings and pass all 622 tests, including GPU, golden images,
  editor, module and sandbox tests.

- M8.8: The sandbox has 15 catalog prefabs for its props, lamps and four firearms, plus point and spot lamp prefabs
  used by the tool gun. The playground's 56 instances retain the placements and component settings of the original
  80 entities as overrides, including static lamps, cone colliders and held firearm component removals. Spawned
  firearms keep their full model hierarchy and physics. `Spawnable` supplies category, order and placement
  clearance; `PropSpawner` discovers marked roots below its configured folder through the runtime manifest. F1–F8
  retain their original props, and Spawn cycles through every entry. Adding a marked prefab requires no code.
  `AssetManager::GetAssetIds()` queries by type and recursive directory without sources or asset loading, excludes
  built-ins and sorts deterministically. Tests cover shipped catalogs, manifest refresh, asset-only additions,
  instance links and overrides, every spawned tree, cleanup, undo and real sandbox prefab propagation.
  Validation and M8 acceptance: MSVC and GCC Debug build without warnings and pass all 627 tests, including GPU,
  unchanged golden images, editor, module and sandbox tests. All original playground entity properties and
  component data were checked against the preceding scene. The M9 steps remain unstarted.

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
- [ ] **M9.11 Ragdolls.** A `Ragdoll` component builds bodies and `SwingTwistJoint`s (M1.8) from a skeleton, with
  limits per bone, and switches a character from animation to physics and back. Tests.
- [ ] **M9.12 Sandbox: robot and view models.** The generator makes a simple rigged robot with idle, walk and run
  clips, and first-person arms with fire, reload and switch clips for each weapon. Robots wander through a
  controller with a blend tree, and turn into ragdolls when shot or blown up. Weapons are held by the animated arms.

**Done when:** sandbox robots walk through a blend tree, fall as ragdolls when shot, and the weapons fire and reload
with view-model animations.

**Notes:**

---

## M10 – Gameplay toolkit

- [x] **M10.1 Time.** A global time scale, unscaled time for tools and UI, physics that follows the scale, and a clamp
  on the frame delta if one is missing. Tests.
- [x] **M10.2 Timers.** `Invoke(delay, fn)`, repeating invokes and cancellation on components, in scaled time,
  stopped when the component goes. Tests.
- [x] **M10.3 Tweens.** Tweens of values, transforms and colors with easing functions and sequences, run by a
  `TweenSystem`. Tests.
- [x] **M10.4 Coroutines.** A C++20 coroutine `Task` that awaits the next frame, seconds, a condition or another task,
  owned by a component and cancelled with it. Tests.
- [x] **M10.5 Signals.** `Signal<Args...>` with scoped connections that disconnect themselves, for gameplay messages
  and UI callbacks. Tests.
- [x] **M10.6 Typed references.** `ComponentRef<T>` fields, serialized by entity id and drawn in the inspector with
  drag and drop.
- [x] **M10.7 Save data.** Save files in the user data folder written with `Archive`, with versioning. Tests.
- [x] **M10.8 Sandbox: saving and feel.** Saving and loading the world (props, frozen state, welds, ropes, lamps).
  Weapon cooldowns, recoil, camera bob and fuse timers use timers, tweens and coroutines.

**Done when:** a sandbox world saved and loaded comes back as it was, and the game has no hand-written timers left.

**Notes:**
- M10 and M11 are implemented before M9 at the user's request; skeletal animation remains deferred.
- M10.1: `Time::SetTimeScale()` changes simulation deltas, including fixed physics, while the editor's UI and
  backups use unscaled time. The frame clock clamps stalls before applying the scale. Standalone scenes receive
  simulation seconds directly; fixed stepping limits catch-up without converting an unbounded step count to an int.
  Headless gameplay tests cover pause/resume, invalid scales, physics and extreme deltas; the editor builds on MSVC.
- M10.2: `TimerQueue` is the code-only scheduler; components allocate a queue only when used. Timers advance in
  scaled time after fixed steps and before component updates. Inactive owners pause, removed owners cancel, and
  throwing callbacks fault only their owner. Repeats preserve phase and coalesce missed ticks to prevent bursts.
- M10.3: reusable `Tween` descriptions and a code-only `TweenPlayer` support easing, colors, quaternion rotations
  and nested sequences with overshoot. The lazy scene `TweenSystem` resolves transform targets and optional owners
  by entity/component ids. Missing owners cancel, inactive owners pause, and callback failures cancel only one tween.
- M10.4: lazy move-only C++20 `Task` coroutines run through a code-only `TaskScheduler` or an owning component.
  Frame, seconds, condition and nested-task waits pause with simulation time; scene update ids prevent a task
  started in a hook from consuming that update's delta. Cancellation unwinds all nested frames and defers destruction
  of executing frames until suspension. Child and predicate exceptions enter the awaiting coroutine normally.
- M10.5: `Signal<Args...>` dispatches synchronously in connection order. Move-only `SignalConnection` subscriptions
  disconnect at scope exit and use weak ownership, so either side may go away first. Emission snapshots identities,
  supporting connection changes and signal destruction during a callback without invalidating the dispatch loop.
- M10.6: `ComponentRef<T>` stores an entity id and resolves the first matching component within a supplied scene.
  Archive uses its existing entity-id remapping for scene copies and prefabs. Field metadata filters the inspector's
  picker and drag-and-drop without backend or editor dependencies in the engine; missing or mismatched targets
  remain editable. Headless tests cover resolution, replacement, serialization, copied trees and inspector selection.
  The picker now owns delivered payload bytes: ImGui clears them in `EndDragDropTarget()`, so the previous pointer
  result lost entity and asset drops before fields consumed them. Both drop paths have headless regression coverage.
- M10.7: code-only `SaveData` takes the application's user data directory or an explicit folder and uses Archive
  callbacks with a file version. It rejects newer versions before loading, exposes older versions for migration,
  reports structured errors and writes a temporary file before replacing the slot, with a recoverable backup where
  rename cannot overwrite. Headless tests cover round trips, replacement, migration, corrupt files, names and failures.
- M10.8: F9/F10 checkpoint the editable root props and mounted lamps in `world.json`, retaining frozen state,
  velocities, painted materials, welds, ropes, ammo and remaining timers. Loading validates the checkpoint before
  replacing props, remaps internal references, preserves references to the level, clears undo and restores spawned
  ownership for cleanup. The player, level and ambience remain live; transient projectiles, flashes and view models
  are excluded. Prefabs are expanded intentionally so a checkpoint stays independent of later source edits.
  Inline materials now serialize their PBR values and texture asset references; the binary scene/prefab containers
  and importer encodings are unchanged. Timers replace weapon, fuse, projectile and impact countdowns; tweens drive
  flashes, aiming and recoil, and a coroutine sequences camera bob. Continuous movement, day-cycle phase and
  distance-based footsteps remain simulation integration. HUD timing is unscaled. Headless sandbox tests cover the
  bindings, restored world, corrupt-file protection, cleanup ownership, remaining fuses, recoil and bob.
- Done 2026-10-10. Acceptance: 573 headless tests pass with MSVC and GCC, and both builds have no compiler warnings
  after the GCC tween-test initializer fixes. The sandbox restores an edited world and resumes its remaining fuses;
  scaled waits, tweens and coroutines pause together. No GPU tests were needed: this milestone changes simulation,
  serialization and headless inspector behavior, without changing shaders or rendering pipelines.

---

## M11 – Developer tools

- [x] **M11.1 Commands and cvars.** A `ConsoleCommand` registry and `CVar<T>` (name, default, description, flags
  such as cheat or saved), with parsing, completion and history. The engine registers its rendering, physics and
  audio switches. Tests.
- [x] **M11.2 In-game console.** An overlay drawn with `Renderer2D` that takes input through M2.6. The editor's console
  panel gets the command line.
- [x] **M11.3 Stats overlay.** FPS, frame time, draw calls, triangles, bodies, voices and memory.
- [x] **M11.4 Profiler panel.** A timeline of recent frames from M0.4 in the editor, with averages per scope and
  pausing. It covers games running in their own process too, through the channel the editor already reads their
  logs from.
- [x] **M11.5 Captures.** Screenshot command, and frame captures with RenderDoc when it is present.
- [x] **M11.6 Sandbox: commands.** `gravity`, `timescale`, `noclip`, `cleanup`, `spawn <prop>`, `give <weapon>`, and
  `stress <count>`, which spawns props in a pile to measure physics and rendering.

**Done when:** a frame spike in the sandbox can be found from the profiler panel and toggled away from the console.

**Notes:**

- M11.1: code-only `Console` supports scoped registrations, quoted/escaped arguments, sorted completion, bounded
  history/output and contained command failures. Typed cvars own values or bind public engine settings, validate
  complete finite input, gate cheat commands and persist saved values through Archive. Applications register
  time, gravity, physics visualization, rendering, frame cap, audio gain and profiler switches during `Run()`;
  saved values round-trip through `console.json`. Scene bindings resolve the current scene on each use. Seven
  headless tests (72 assertions) cover parsing, lifetime, mutation, flags, validation, history and persistence,
  including application startup/shutdown. MSVC builds without compiler warnings.
- M11.2: `ConsoleInput` consumes events before gameplay through M2.6 captures, releases held controls, restores
  requested cursor locking, toggles platform text input and edits UTF-8 with completion/history. Escape closes the
  console without activating gameplay Pause. `DrawConsoleOverlay()` uses public Renderer2D after the game's UI;
  games supply their imported UI font, and the sandbox supplies its HUD font. The editor has its own command field,
  tested through actual ImGui typing, Tab, Enter and history navigation. `CreateOffscreen()` and
  `BeginOffscreenFrame()` were added to validate GPU drawing/readback without creating desktop windows. Eleven
  console/input tests, two headless editor tests and all 27 sandbox cases pass, plus one targeted windowless GPU
  test (19 assertions). No broader GPU suite ran; MSVC builds without compiler warnings.
- M11.3: saved `stats` toggles the Renderer2D overlay with unscaled FPS/frame duration, combined 3D/shadow/2D draws,
  main-view and 2D triangles, live bodies and voices, and CPU resident memory. Platform memory queries stay in the
  platform backend. Counters accompany profiler frames; disabling recording falls back to the unscaled clock
  instead of displaying stale draws. Fourteen headless console/statistics cases (134 assertions) pass, including
  paused simulation, live physics and audio; the windowless overlay GPU test passes 20 assertions. MSVC compiles
  without warnings. No desktop windows or broad GPU suite were used.
- M11.4: the dockable Profiler shows a 240-frame histogram, click-to-pause selection, nested scope timeline,
  inclusive per-frame averages and dropped samples. It owns copies, so pausing survives ring rotation. Managed
  games opt into versioned profile records over the existing log pipe; GameRunner filters those from normal logs
  and keeps bounded remote history. Child environment overrides never mutate the parent; platform reads return
  owned values. Validation covers malformed records, first/final application frames, actual child output, ring
  rotation/restarts and ImGui mouse selection. Seventeen engine cases, one process case and six editor cases pass
  (199 assertions), without GPU/windows. MSVC builds without warnings; Windows memory header prerequisites are
  preserved across clang-format.
- M11.4 follow-up: scope totals advance with frames entering/leaving the retained ring, so drawing averages no
  longer scans every sample of the history during stress profiling. Expired names and totals are removed, and
  process restarts clear them. All seven headless editor tools cases pass (37 assertions) with MSVC.
- M11.5: `screenshot [name.png]` queues a complete frame (including custom rendering/UI) into a readable matching
  target and presents it through public frame-target redirection/blitting; no second scene rendering or swapchain
  readback is needed. Submitted textures save as PNG with RGBA/BGRA conversion and sRGB preserved. `capture [name]`
  uses optional RenderDoc API 1.6, connected before graphics, with backend-private SDK types and no external UI.
  Missing runtimes are silent; Metal capture is unavailable through RenderDoc. Twenty headless cases pass
  (175 assertions), including SDK ABI fixture and unavailable capture commands. Two windowless GPU cases pass
  131 assertions across RGBA/BGRA/sRGB targets, pixel presentation, PNG reload and rendering counters. Actual
  RenderDoc hardware capture was not exercised. MSVC compiles without warnings; no visible GPU windows were used.
- M11.6: seven cheat-gated commands belong to enabled GameControls and resolve scene components on execution.
  Catalog names accept spaces/quotes, `give` selects/refills existing weapons or strips world components before
  adding missing held weapons, and `stress` creates a bounded crate pile tracked by normal spawning/undo.
  `cleanup` removes spawned props and clears their undo entries. Four real-scene sandbox cases pass 4,580
  assertions, including validation, registration lifetime, inventory reuse and physics scopes/body counts before
  and after stress cleanup. A regression found that validation/queries can create bodies before OnStart();
  component removal and entity destruction now invalidate those bodies independently of OnDisable(), covered by
  eight additional assertions.
- M11 completed on 2026-10-11: full MSVC and GCC builds have no compiler warnings, and all 606 headless tests pass
  on each compiler. Acceptance is covered by real sandbox stress/cleanup, profile transport and editor interaction
  tests. Only the two targeted windowless GPU cases ran (131 assertions); larger interactive stress measurements
  and actual RenderDoc hardware capture were not exercised. Work stops after M11; M12 is untouched and M9 remains
  deferred by request.

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
- [ ] **M12.7 Sandbox: interface.** A HUD (crosshair, ammo, current weapon, the tool's mode), and a spawn menu (Q)
  with tabs and thumbnails of the spawnable prefabs, rendered into textures. The pause menu holds the settings
  (sensitivity, field of view, volumes, bindings, graphics quality), and they are saved per user.

**Done when:** the sandbox's menus work with mouse, keyboard and gamepad, and are authored in the editor.

**Notes:**

---

## M13 – Particles

- [x] **M13.1 Curves and gradients.** `Curve` and `Gradient` types that are serialized and edited in the inspector.
- [x] **M13.2 Simulation.** A `ParticleSystem` component with emission rate and bursts, shapes (point, sphere, cone,
  box), start ranges, values over lifetime, gravity and local or world space, simulated on the CPU by a system.
  Tests.
- [x] **M13.3 Rendering.** Instanced billboards that face the camera or stretch with velocity, additive or alpha
  blending, sorting, soft particles from depth, and flipbook textures.
- [x] **M13.4 Editor.** Edit-mode simulation of the selected system, and restart and seek controls.
- [x] **M13.5 Sandbox: effects.** Muzzle flashes, sparks and dust per surface, explosion fire and smoke, debris, and
  the physics gun's beam particles.

**Done when:** weapons, impacts and explosions in the sandbox emit particles.

**Notes:**
- M13.5: authored scene definitions drive muzzle, surface-specific impacts, explosion fire/smoke/debris
  and beam particles. Props have a separate audio-independent collision effect component. A bounded
  pool reuses entity ids, particle storage and configuration capacity; fractional beam emission is
  independent of update rate. Five sandbox cases pass 4,481 assertions, including real shots, collisions,
  explosions, expiry, pool bounds/reuse and physics-gun release. A configuration/storage regression
  brings engine particle coverage to thirteen cases and 708 assertions. Full MSVC Debug builds have
  no compiler warnings; milestone-wide MSVC/GCC regression checks are in progress.

- M13.4: document-owned previews resolve entity/component ids and scene revisions, with real-time
  selected-emitter simulation, pause, restart and seek. No gameplay, serialized edits or undo entries
  are produced. Inspector curves and gradients have visual previews plus normal key editing. Two
  headless preview cases pass 17 assertions, inspector interaction passes 15, and a windowless editor
  GPU case passes 10 through the actual viewport/MSAA path. All twelve particle cases still pass 703
  assertions after adding specialized archive hooks; MSVC compiles without warnings.
  Follow-up: key times commit on Enter before reordering, and curve/gradient edits record the whole
  field for multi-selection and prefab overrides. Seventeen inspector cases pass 245 assertions,
  including an interaction regression for both kinds of keys. The particle guide uses UTF-8.

- M13.3: globally sorted alpha and additive billboard instances draw through public graphics APIs in
  both game and editor views. CPU tests cover sorting, transforms, stretch, rotation and flipbooks.
  Two windowless GPU cases pass 46 assertions for pixels, alpha/additive blending, ordering, flipbooks,
  hard/soft depth, MSAA and opaque/cutout/transparent depth. Twelve particle cases pass 703 assertions;
  fifteen existing inspector cases pass 214 assertions. MSVC builds without compiler warnings after
  fixing a depth literal conversion. MSAA uses an optional readable camera-depth pass; particles remain
  a separate pass after transparent meshes.

- M13.2: a code-only bounded simulator underlies the component and lazy scene system. Fixed internal
  steps, seeded sampling and timestamped births make replay deterministic. Configuration edits reset
  runtime state; disabling freezes it. MSVC compiles without warnings and ten cases pass 693 assertions
  across curves, emission, loops, shapes, modules, spaces, replay, validation and scene lifecycle.

- M13.1: Hermite curves and linear HDR gradients use sorted, finite keys, last-key duplicate resolution
  and clamped endpoints. They share the archive and inspector key-array editing, including undo and
  prefab overrides. MSVC builds without warnings; three tests pass (20 assertions).
- M13 is implemented out of order by request; it does not require M9 or M12.


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

## M15 – The sandbox game

Finishes the game described in [The sandbox game](#the-sandbox-game). It does not wait for M14, which can come
before or after it.

- [ ] **M15.1 The level.** The playground authored in the editor: open ground, ramps, stairs, platforms, a building
  with lit interiors and glass, and spawn areas, with the day cycle.
- [ ] **M15.2 Feature check.** Every item of [The sandbox game](#the-sandbox-game) works together. What is missing or
  broken becomes a step here.
- [ ] **M15.3 Polish.** Weapon feel (recoil, spread, view-model sway and bob), physics gun feel, sound mix, HUD
  readability and menu flow.
- [ ] **M15.4 Performance.** In Release, 60 FPS with 500 props and 50 lamps on the development machine. Profile, fix
  and record the numbers in Notes.
- [ ] **M15.5 Release.** `spot export` builds of the game for Windows, Linux and macOS that run with nothing else
  installed. `docs/sandbox.md` describes the controls and a play-test checklist covering every system the game uses.

**Done when:** an exported build of the sandbox plays every feature of [The sandbox game](#the-sandbox-game) on the
three platforms.

**Notes:**

---

## Backlog

Not scheduled. Each comes in when a game needs it, and gets a milestone of its own then.

- Navigation: navmesh baking in the tools (Recast) and a `NavAgent` component (Detour).
- Async asset loading and streaming, a job system, LODs.
- SSAO, depth of field, motion blur, TAA, decals, reflection probes, terrain.
- Custom shaders for materials, with parameters the inspector shows.
- Audio effects (filters, reverb zones) and HRTF.
- IK (two-bone, look-at).
- Networking (transport, replication, RPCs).
- A web target, once SDL_GPU supports WebGPU.
- Localization.
- A scripting language, only if M4 does not make C++ iteration fast enough.
