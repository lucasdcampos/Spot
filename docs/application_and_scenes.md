# Application and scenes

`Application` (`engine/runtime/application.h`) owns the window, the graphics and audio devices, the asset manager, the
input state and the scenes, and runs the frame loop. `Scene` (`engine/scene/scene.h`) owns a hierarchy of entities
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

1. Begins a profiler frame and waits for the frame rate cap (`Application::Wait`).
2. Polls events and dispatches them to the input state, `OnEvent()` and the scenes (`DeliversToScenes()` filters
   them).
3. Applies the scene changes queued since the last frame (`LoadScene()`, `LoadSceneAdditive()`, `UnloadScene()`).
4. Calls `OnUpdate()`, then updates the persistent scene and the loaded scenes in load order, unless they are paused
   (`SetScenesPaused()`, `StepScenes()`).
5. On frames that can be drawn: runs each scene's `PrepareToRender()`, their `OnRender()`, draws them with a
   `SceneRenderer` (unless `ApplicationConfig::renderScenes` is off), calls `OnRender()` and submits the frame.
6. Ages the shapes queued with `DebugDraw` by the frame's time.

Scene changes are applied between frames, so a scene that asks to be replaced is never destroyed while it runs.

## Scenes

A scene is usually derived per level, and builds its entities in `OnLoad()`; scenes saved by the editor load with
`Scene::LoadFromFile()` or as assets. The application also keeps a **persistent scene** for its whole run:
`Application::MakePersistent()` moves an entity there so it survives scene changes.

Scenes also work on their own, without an application, by calling `Update()` directly, which is how most tests run.

### What `Scene::Update()` does

1. `OnStart()` for components that have not started, then `OnEnable()`.
2. The systems' `OnBeforeFixedSteps()`.
3. The fixed steps due since the last update. Each runs every active component's `OnFixedUpdate()`, then every
   system's `OnFixedUpdate()`, which is where physics steps.
4. The systems' `OnAfterFixedSteps()`.
5. The scene's own `OnUpdate()`.
6. Every active component's `OnUpdate()`, then every system's.
7. Every active component's `OnLateUpdate()`, then every system's.

Entities go in creation order, components in the order they were added, and systems by their order.

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

A `SceneSystem` (`engine/scene/scene_system.h`) is logic that runs over a whole scene rather than per entity. It has a
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
