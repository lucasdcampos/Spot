# Application and scenes

`Application` (`engine/spot/runtime/application.h`) owns the window, the graphics and audio devices, the asset manager, the
input state and one active scene, and runs the frame loop. `Scene` (`engine/spot/scene/scene.h`) owns a hierarchy of entities
and drives their components and its systems.

Both are the default model, not a requirement: a game can run a loop of its own on the layers below them (see
[Games without scenes](index.md#games-without-scenes)).

## Running a game

A game's executable runs an application with the modules of the systems it uses:

```cpp
#include <spot.h>

int main() {
    Spot::Application application({.game = Spot::GameSettings::LoadBesideExecutable()});
#ifndef NDEBUG
    application.AddModule<Spot::DevToolsModule>();
#endif
    application.AddModule<Spot::UiModule>();
    application.AddModule<Spot::RenderModule>();
    application.AddModule<Spot::AudioModule>();
    application.AddModule<Spot::PhysicsModule>();
    application.AddModule<Spot::Physics2DModule>();
    application.AddModule<Spot::AnimationModule>();
    application.AddModule<Spot::ParticlesModule>();
    application.AddModule<Spot::ScriptingModule>();
    application.AddModule(Spot::CreateGameModule());
    return application.Run();
}
```

`ApplicationConfig::game` is the only way game settings reach an application. `GameSettings::LoadBesideExecutable()`
reads the `game.json` the tools write beside a game's executable, as games and the player do in `main()`; nothing
else looks for it. `Run()` applies the game's input actions, the player's rebinds and console settings, loads the
startup scene it names and loads assets from the import directory it names. Applications can also
derive from `Application` and override `OnStart()`, `OnUpdate()`, `OnEvent()`, `OnRender()` and `OnStop()`, and
configure it with `ApplicationConfig` (window, graphics, frame rate cap, log file, crash reports).

The game's code is a `Module`, which registers its components in `OnStart()`. The code names it once with
`SPOT_GAME_MODULE(Type)`; without a type it is a `GameCodeModule`, which registers the components marked with
`SPOT_COMPONENT()` (see [Entities and components](entities_and_components.md)), and games with more to do derive from
it. Built into the executable, `main()` adds it with `CreateGameModule()`; built into a shared library, the
macro exports the function that makes it, and the editor starts it when it loads the library (see
[Editor](editor.md)).

## Startup

The constructor only keeps the configuration; `Run()` does the work, as a sequence read top to bottom, each step
needing only the ones before it:

1. The log file and crash reports, when configured (`ApplicationConfig::logFile`, `crashReportDirectory`).
2. The platform and the window (`StartWindow()`); `Run()` fails if it cannot be opened.
3. The graphics device, unless graphics are disabled.
4. The asset manager, on the import directory, with scene assets registered.
5. The input: the input state, the game's actions and the player's rebinds, and the frame clock.
6. The modules, in the order they were added, then the console settings they registered are loaded.
7. `OnStart()`.
8. The game's start scene, unless `OnStart()` loaded a scene.

Stopping runs the other way: `OnStop()`, the modules in reverse, the scene, then the assets, devices and log.


`Time::SetTimeScale(scale)` controls simulation time globally: zero pauses time, values below one slow it down,
and values above one speed it up. Negative and non-finite values are rejected. `Time::GetDeltaTime()` and
`GetTime()` are scaled; `GetUnscaledDeltaTime()` and `GetUnscaledTime()` keep advancing for tools and UI. The
frame clock clamps stalls to 250 ms before scaling. Application updates and scene physics receive scaled seconds;
standalone `Scene::Update(seconds)` already takes simulation seconds and does not scale them again. Fixed physics
keeps its step size and changes how many steps are due. The editor's UI and backup clock use unscaled time.

Each iteration of `Application::Run()`:

1. Begins a profiler frame, waits for the frame rate cap (`Application::Wait`) and makes the assets whose background
   loads finished reading (`AssetManager::Update()`).
2. Polls events and dispatches them to the modules, the input state, `OnEvent()` and the active scene
   (`DeliversToScenes()` filters them). A module that consumes an event keeps it from the rest but `OnEvent()`.
3. Applies the scene replacement queued since the last frame with `LoadScene()`, then finishes ready chunks.
4. Calls `OnUpdate()`, then updates the active scene unless paused (`SetScenesPaused()`, `StepScenes()`), then the
   modules' `OnUpdate()`.
5. On frames that can be drawn: calls the modules' `OnBeginRender()`, runs the scene's `PrepareToRender()` and
   `OnRender()`, calls the modules' `OnRender()` (where `RenderModule` draws the scene), then `OnRender()`, then the modules' `OnEndRender()` in reverse, and submits the frame.
6. Ages the shapes queued with `DebugDraw` by the frame's time.

Scene changes are applied between frames, so a scene that asks to be replaced is never destroyed while it runs.

## Modules

A `Module` (`engine/spot/runtime/module.h`) is a part of an application that hooks into its frame, so a system
joins the loop without `Application` knowing it. Applications add them before `Run()` with `AddModule<T>(args...)`
and find them with `GetModule<T>()`, by exact type. `Run()` starts them in the order they were added, after the
window, the graphics device and the assets, and before `OnStart()`; it stops them in reverse after `OnStop()`, while
those still exist. Only `Application` knows modules: no system's own API depends on one.

Each system has its module in its `scene/<system>/` folder, which registers its components and asset loaders when it
starts, so `runtime/` includes no system:

| Module | What it does |
|---|---|
| `RenderModule` | Registers the graphics and render loaders and the rendering components; draws the active scene with a `SceneRenderer` made from the application's rendering settings (`RenderModuleConfig::drawScene`, which the editor turns off) |
| `AudioModule` | Owns the `AudioDevice` (`AudioConfig`), applies the game's audio section, registers sounds and audio components, and pauses the scene's sounds while the scene is paused |
| `UiModule` | Registers the UI components, routes events to the scene's canvases before the game sees them, and starts text input for focused fields; canvases draw with the scene, inside `SceneRenderer`, so the editor's game view shows them too |
| `PhysicsModule`, `Physics2DModule` | Register the physics components; 3D adds the `physics.*` console settings and counts bodies |
| `AnimationModule`, `ParticlesModule`, `ScriptingModule` | Register their components, and animation and script loaders |
| `DevToolsModule` (`runtime/`) | The console and statistics overlays, screenshots, RenderDoc captures and profile streaming; development builds and the editor add it first (see [Developer tools](developer_tools.md)) |

The console settings of a system (`render.enabled`, `audio.volume`, `physics.gravity`…) come with its module, and the
statistics overlay reads the bodies and voices the modules count for the profiler.

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
`Scene::GetOrAddSystem<PhysicsWorld>()` when a collider or rigidbody asks for it.

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

Scene systems receive immediate `OnComponentAdded`, `OnComponentRemoved`, `OnEntityLayerChanged` and
`OnEntityDestroyed` notifications, including changes made before the first update. Moving a component within its
entity does not add it again. `OnEntityMoved(entity, source, destination)` runs in the source before transfer and in
the destination afterward; the entity belongs to the receiving scene during each call. Notifications use the same
exception guard as update hooks. Systems added during a notification receive subsequent notifications.

Physics follows these notifications to rebuild bodies after structural changes, so entities themselves have no
physics dependencies. Audio transfers its playing sources through the move notification.

Scene systems are indexed by their exact concrete C++ type. Use `GetOrAddSystem<T>(args...)` to create one on demand,
or `GetSystem<T>()` to query without creating it. Base-type queries do not search derived systems. Systems added
inside a system hook are queryable immediately and join execution at the next hook boundary.

The scene core does not route UI events or own render callbacks. `UiSystem::DrawUi()` lays out canvases and invokes
`UiDrawListener` components, and `UiSystem::ScreenCamera()` supplies the pixel camera. Give `UiInputSystem` the input
state that owns the view; `HandleUiEvent(event, viewSize, input)` updates that binding and consumes handled input.
Render callbacks belong to `scene.GetOrAddSystem<Spot::RenderHookSystem>()`. Removing a callback during dispatch
prevents a later call in that dispatch; newly added callbacks wait for the next dispatch.

A destination's move notification runs after the entity's children have transferred. Move hooks may destroy entities
or reparent children; destroyed entities cancel their pending transfer, and children moved elsewhere stay there.
A nested request to move an entity already transferring is ignored. Audio transfers source registrations only once
the move reaches its destination, so a move canceled by destruction cannot leave stale audio registrations.
