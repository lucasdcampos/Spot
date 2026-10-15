# Diagnostics

What helps find out what a game does and why it went wrong: logs, crash reports, contained failures, the profiler
and debug drawing. A failure is never silent: it is logged with its context, contained to the smallest unit that
owns it, and only takes the frame down when nothing else is possible.

## Logging

`Spot::Log` (`engine/core/log.h`) writes formatted messages at a level (trace, debug, info, warning, error):

```cpp
Spot::Log::Info("Loaded {} props", count);
```

`Log::Info()` and the others write to the game's logger; the engine writes to `Log::GetEngineLogger()`, and tools have
loggers of their own. Every logger writes to the same sinks (`LogSink`): the console by default, a file when
`ApplicationConfig::logFile` is set (games built by the tools log to `game.log` beside the executable, keeping the
previous run's log beside it), and the editor's console panel.

## Crash reports

`InstallCrashHandler()` (`engine/core/crash_handler.h`), which applications call, makes crashes leave a trace: on a
bad memory access, an uncaught exception, an abort or a stack overflow, the crash and its stack are logged to every
sink, the sinks are flushed and a report is written (in a `crashes` folder beside the log by default) before the
process ends. Tools also show a message saying where the report is.

## Contained failures

Every component hook, and every scene system hook, runs under a guard. When one throws, the error is logged once
with the entity, the type and the hook, and that component or system is **faulted**: it stops running until its
scene is reloaded, and the rest of the scene goes on. A faulted component still gets `OnDisable()` and
`OnDestroy()` to release what it holds. `Component::IsFaulted()` and `SceneSystem::IsFaulted()` tell tools, and the
editor's inspector marks faulted components. Assertions (`SPOT_ASSERT`) are for programming errors that cannot be
contained, and stop the process with a report.

## Profiler

`SPOT_PROFILE_SCOPE("Name")` (`engine/core/profiler.h`) times the rest of the enclosing scope, on any thread:

```cpp
void Spawner::OnUpdate(float deltaTime) {
    SPOT_PROFILE_SCOPE("Spawner::OnUpdate");
    // ...
}
```

The profiler keeps the last `Profiler::MaxFrames` frames in a ring. `Profiler::GetFrames()` returns them oldest first;
each `ProfileFrame` has its duration and its samples, in the order their scopes began, each with its depth, so they
form the tree of nested scopes. The application begins and ends a profiler frame around each iteration of its loop,
and the engine times its own work: the application's stages (`Application::Wait`, `Application::Events`,
`Application::Render` and the rest), `Scene::Update` and its stages (`Components::OnUpdate`, `Systems::OnFixedUpdate`
and so on), each system under its type's name (`Spot::PhysicsWorld`), the renderers and the audio device's calls.

Scopes on other threads, such as the job system's workers, record into a buffer of their thread while a frame is
recorded, and join the frame when it ends, after the frame thread's own, each with its `thread` (0 for the thread that
begins frames, then 1, 2… in the order threads first record a scope). Their depths describe a tree per thread, and
the editor's profiler panel draws a lane per thread. Scopes still open when the frame ends end with it; counters are
only kept on the frame's thread.

Scope names are string literals, or names made at run time and kept with `Profiler::Intern()`. Scopes outside of
frames are not recorded. Frames reuse their memory, so the profiler stays on in Release;
`Profiler::SetEnabled(false)` turns it off. The sandbox HUD samples unscaled FPS; the `stats` developer overlay
adds frame time, rendering, physics, audio and resident-memory counters.

Frames also count things: `Profiler::Count("Name", amount)` adds to the frame's counter of that name, which starts at
0 each frame, so code that runs several times a frame adds up, and `Profiler::GetCounter()` reads it back. `Renderer3D`
counts the meshes it drew in its views (`Visible meshes`) and those it left out because they were outside them
(`Culled meshes`).

The editor's **Profiler** panel copies recent frames for paused inspection, plots a scope timeline and computes
inclusive averages per frame. It can also read managed game processes over their existing log pipe. See
[Developer tools](developer_tools.md) for the consoles, profiler workflow and screenshot/RenderDoc commands.

## Debug drawing

`Spot::DebugDraw` (`engine/graphics/debug_draw.h`) lets any code draw lines, arrows, crosses, boxes, circles, spheres
and capsules in world space, for this frame or for a duration, tested against depth or over everything:

```cpp
Spot::DebugDraw::Line(origin, hit, {.color = Spot::Color::Red});
Spot::DebugDraw::Sphere(hit, 0.1f, {.duration = 2.0f, .depthTest = false});
```

Shapes become lines as they are queued, into a `DebugLines` list: `DebugDraw` keeps the one every view draws, and
tools keep their own for what one view alone shows. The `SceneRenderer` and the editor's views draw the queue with a
`LineRenderer`, over the meshes, and the application ages it once each frame was drawn
(`DebugDraw::Update()`); code that runs its own loop calls that itself. Physics draws its debug view this way (see
[Physics](physics.md)).
