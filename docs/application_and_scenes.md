# Application and scenes

`Application` (`engine/spot/runtime/application.h`) owns the window, the graphics and audio devices, the asset manager, the
input state and one active scene, and runs the frame loop. `Scene` (`engine/spot/scene/scene.h`) owns a hierarchy of entities
and drives their components and its systems.

Both are the default model, not a requirement: a game can run a loop of its own on the layers below them (see
[Games without scenes](index.md#games-without-scenes)).

## Running a game

A game's executable needs nothing but running an application:

```cpp
#include <spot.h>

int main() {
    return Spot::Application().Run();
}
```

Without a configured game, the application reads `game.json` beside the executable (`GameSettings`, written by the
tools), loads the startup scene it names and loads assets from the import directory it names. Applications can also
derive from `Application` and override `OnStart()`, `OnUpdate()`, `OnEvent()`, `OnRender()` and `OnStop()`, and
configure it with `ApplicationConfig` (window, graphics, audio, frame rate cap, log file, crash reports).

The game's code marks itself once with `SPOT_GAME_MODULE()` and registers its components with `SPOT_COMPONENT()`
(see [Entities and components](entities_and_components.md)). Built into the executable, the registrations run when
`Run()` starts; built into a shared library, they run when the editor loads it (see [Editor](editor.md)).

## The frame

`Time::SetTimeScale(scale)` controls simulation time globally: zero pauses time, values below one slow it down,
and values above one speed it up. Negative and non-finite values are rejected. `Time::GetDeltaTime()` and
`GetTime()` are scaled; `GetUnscaledDeltaTime()` and `GetUnscaledTime()` keep advancing for tools and UI. The
frame clock clamps stalls to 250 ms before scaling. Application updates and scene physics receive scaled seconds;
standalone `Scene::Update(seconds)` already takes simulation seconds and does not scale them again. Fixed physics
keeps its step size and changes how many steps are due. The editor's UI and backup clock use unscaled time.

Each iteration of `Application::Run()`:

1. Begins a profiler frame, waits for the frame rate cap (`Application::Wait`) and makes the assets whose background
   loads finished reading (`AssetManager::Update()`).
2. Polls events and dispatches them to the input state, `OnEvent()` and the active scene (`DeliversToScenes()`
   filters them).
3. Applies the scene replacement queued since the last frame with `LoadScene()`, then finishes ready chunks.
4. Calls `OnUpdate()`, then updates the active scene unless paused (`SetScenesPaused()`, `StepScenes()`).
5. On frames that can be drawn: runs the scene's `PrepareToRender()` and `OnRender()`, draws it with a
   `SceneRenderer` (unless `ApplicationConfig::renderScenes` is off), calls `OnRender()` and submits the frame.
6. Ages the shapes queued with `DebugDraw` by the frame's time.

Scene changes are applied between frames, so a scene that asks to be replaced is never destroyed while it runs.

## Scenes

A scene is usually derived per level, and builds its entities in `OnLoad()`; scenes saved by the editor load with
`Scene::LoadFromFile()` or as assets. `Application::GetScene()` returns the active scene, or null. `LoadScene()`
queues a replacement; the last request before the next frame wins. Requests from `OnLoad()` or `OnUnload()` wait
for the following frame. `LoadScene(nullptr)` unloads the scene. Shutdown calls `OnStop()` before unloading it.

`entity.SetKeepOnSceneChange()` retains an entity and its descendants across replacements, keeping their ids,
component instances and state. After the old scene's `OnUnload()`, retained subtrees move into the next scene,
before its `OnLoad()`. A retained child detaches from an ordinary parent with its world pose. Nested retained
entities move once, with their retained ancestor. `SetKeepOnSceneChange(false)` clears the entity's own flag.
Explicit unloading and shutdown destroy all entities, including retained ones. The editor's Play session uses
these same transitions; stopping Play unloads the game, so retained entities never leak into another session.

A scene also works without an application, by calling `Update()` directly. Stream large worlds into it with
chunks (below): all loaded entities share one hierarchy, one set of systems and one physics simulation.

### What `Scene::Update()` does

1. Finishes ready chunks, then `OnStart()` for components that have not started, then `OnEnable()`.
2. The systems' `OnBeforeFixedSteps()`.
3. The fixed steps due since the last update. Each runs every active component's `OnFixedUpdate()`, then every
   system's `OnFixedUpdate()`, which is where physics steps.
4. The systems' `OnAfterFixedSteps()`.
5. The scene's own `OnUpdate()`.
6. Every active component's `OnUpdate()`, then every system's.
7. Every active component's `OnLateUpdate()`, then every system's.

Components go in the order they were added to the scene, and systems by their order; for entities made with their
components, that is the order the entities were made in.

Each stage only visits the components it concerns. The scene keeps lists of the components not started yet, of
those with invokes or coroutines, and of those whose type overrides each update hook: Component's own empty
`OnFixedUpdate()`, `OnUpdate()` and `OnLateUpdate()` mark their type the first time they are called, and the scene
skips that hook for the type from then on. So a large scene whose components are mostly data costs an update only
for the components that do something, and an override must never call those empty hooks of `Component`.

### Fixed steps

The scene keeps a fixed-step clock: `Scene::SetFixedTimeStep()` (1/60 s by default) sets how much time each step
simulates. An update runs as many steps as the time since the last one covers, at most
`Scene::MaxFixedStepsPerUpdate`; the time beyond that is dropped, so a slow frame slows the simulation down instead
of making the next frames slower still. Code that acts on physics, such as applying forces, goes in
`OnFixedUpdate()`, so it behaves the same at any frame rate.

### Destruction and removal

Entities and components can be created, destroyed and removed from anywhere, including from inside hooks. While the
scene walks its entities (in `Update()`, `DrawUi()` or a `ForEach`), removals are deferred until it is done, so
references stay valid until then; outside of those they happen at once.

## Scene systems

A `SceneSystem` (`engine/spot/scene/scene_system.h`) is logic that runs over a whole scene rather than per entity. It has a
hook for each point of the update above, plus `OnBeforeRender()`, called by `Scene::PrepareToRender()` on frames that
are drawn. Games add theirs with `Scene::AddSystem<T>()` and find them with `Scene::GetSystem<T>()`:

```cpp
class Spawner final : public Spot::SceneSystem {
protected:
    void OnUpdate(float deltaTime) override { /* ... */ }
};

scene.AddSystem<Spawner>();
```

A scene has at most one system of each type. Systems run by their order (`SystemOrder`, given to the `SceneSystem`
constructor; lower first, ties in the order they were added). Systems added while systems run join before the next
hook. The engine's systems are created when a scene first needs them: `PhysicsWorld` is the first, created by
`Scene::GetPhysics()` when a collider or rigidbody asks for it.

A system whose hook throws is faulted and skipped from then on, as components are (see
[Diagnostics](diagnostics.md)).

## Settings sections

`GameSettings::sections` (a `SettingsSections`) holds the settings of the engine's features, one typed section each:
`physics` (`PhysicsSettings`: gravity, the fixed time step and the layers) and `input` (`InputSettings`: the game's
actions; see [Input](input.md)). Each section has a `SectionName` and a `Serialize()`, is listed in
`SettingsSections::ForEach()`, and is applied where it matters: physics to every scene, input to the application's
action map.

- Projects store them in their `.spot` file, under `"sections"`, and the editor's Project Settings panel edits them.
- `spot cook` and `spot build` write them into `game.json`.
- The application applies them, and the editor plays scenes with the project's.

Sections a build does not know are kept as they are (`SettingsSections::unknown`), so an older build never loses a
newer one's settings. A new section is a struct with those members, a field in `SettingsSections` and a line in its
`ForEach()`.

### Streaming chunks

`scene.LoadChunkAsync(assets, sceneId)` reads a scene asset and preloads its dependencies on worker threads.
`Scene::Update()` finishes ready requests before starting components. Applications also finish requests while
paused. Entity creation and GPU asset creation happen on the main thread; they can still take part of a frame.
The asset manager must outlive the scene and its background reads.

The returned `ChunkLoad` reports completion with `IsDone()`. `GetRoot()` returns the new root entity, or null on
failure, after the root is destroyed, or after the scene is cleared. The imported scene's roots become children
of that root, with fresh ids and remapped internal references. Destroy the root to unload the whole chunk.
Chunks share the host scene's systems, including its physics world. Clearing a scene cancels unfinished chunks.
