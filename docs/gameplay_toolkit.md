# Gameplay toolkit

Small reusable scheduling APIs replace per-component countdowns. They receive simulation seconds from the scene,
so the application's global `Time::SetTimeScale()` controls their speed and pauses them at zero. Tools and UI can
use `Time::GetUnscaledDeltaTime()` instead. See [Application and scenes](application_and_scenes.md) for the clock.

## Timers

`TimerQueue` works without a scene. Schedule `Invoke(delay, callback)` or `InvokeRepeating(delay, interval, callback)`,
keep the returned `InvokeId`, and pass elapsed simulation seconds to `Update()`. Cancel one with `Cancel(id)` or
all with `CancelAll()`. Invalid callbacks, negative or non-finite delays, and non-positive repeat intervals return
`InvokeId::Invalid`. `IsPending()` and `GetTimeLeft()` query a timer without advancing it.

Components expose the same scheduling through `Invoke()`, `InvokeRepeating()`, `CancelInvoke()` and `CancelInvokes()`:

```cpp
void Explosive::OnStart() {
    Invoke(3.0f, [this] { Explode(); });
}
```

The scene advances every active component's timers after fixed steps and before `OnUpdate()`. Disabled components
and inactive entities pause their timers; removing the component, destroying its entity or clearing its scene
releases them. A callback that throws faults its component, cancels its remaining timers and leaves other
components running, as lifecycle hooks do.

Zero-delay callbacks run on the next positive-time update. A timer scheduled during its queue's update waits for
that queue's next update. Callbacks can cancel timers, schedule more or destroy their owner safely. Repeating timers
preserve their interval phase but coalesce missed repetitions into one callback per update, avoiding a burst after
a stall. Reentrant updates do nothing. A standalone queue propagates callback exceptions to its caller.

## Tweens

`Tween` describes an animation and `TweenPlayer` advances it without a scene. `Tween::Value(duration, from, to,
setter, ease)` interpolates floats, vectors, linear colors or rotations (spherical interpolation for quaternions).
`Tween::Sequence()` combines tweens and `Tween::Delay()` inserts a pause. A custom `Tween(duration, sample, ease)`
receives progress from zero to one. Negative and non-finite durations are rejected.

`TweenPlayer::Play()` returns a `TweenId` for querying and cancellation; an optional `TweenOptions::onComplete`
runs once when the sequence ends. Updates consume overshoot across sequence boundaries. `Ease` provides linear,
quadratic, cubic and sine curves. Descriptions capture their starting values when constructed and can be reused;
each playback has its own cursor. Values should have finite endpoints.

For a scene, use `TweenSystem::Get(scene)`, which creates the system on demand:

```cpp
auto& tweens = Spot::TweenSystem::Get(GetScene());
tweens.Play(*this, Spot::TweenSystem::MoveTo(GetEntity(), {0.0f, 2.0f, 0.0f}, 0.5f, Spot::Ease::QuadOut));
```

The system advances after component updates in scaled time. `MoveTo()`, `RotateTo()` and `ScaleTo()` animate local
transforms and resolve their target by entity id on each sample. Passing a component to `Play()` pauses playback
with its activity and cancels it when that component is removed or faulted. Such a tween may safely capture its
owner in setters; unowned setters must manage their own target lifetime. Scene teardown releases all playbacks.

Setters and completion callbacks can cancel or schedule tweens. New playbacks wait until the next player update,
and reentrant updates do nothing. A throwing callback cancels that playback. Standalone players propagate the
exception unless given an error handler; scene systems log it and keep other tweens running.

## Coroutines

`Task` is a lazy, move-only C++20 coroutine. `TaskScheduler::Start()` starts it synchronously up to its first
suspension; `Update(simulationSeconds)` resumes ready tasks. Inside a task, await `NextFrame{}`,
`WaitForSeconds{seconds}`, `WaitUntil{predicate}`, or another `Task`. Each suspension resumes at most once per
update; elapsed time is not carried into a new wait. Zero-second waits still yield a frame. Only these awaiters
participate in the scheduler.

Components own tasks through `StartCoroutine()`, `CancelCoroutine()`, `CancelCoroutines()` and
`IsCoroutineRunning()`. Prefer a member coroutine over a temporary capturing coroutine lambda: the lambda's
closure must outlive its coroutine.

```cpp
Spot::Task Explosive::Fuse() {
    co_await Spot::WaitForSeconds{3.0f};
    Explode();
}

void Explosive::OnStart() {
    StartCoroutine(Fuse());
}
```

Component tasks advance after timers and before `OnUpdate()` in scaled time. Starting one during a scene update
never resumes its first wait in that same update. Inactive components pause, and zero simulation time pauses all
waits, including conditions and next-frame waits. Removal, destruction and faults cancel the owner's tasks at
once. Cancellation destroys suspended coroutine frames, including nested children, so local RAII objects release
their resources. Cancelling a currently executing task defers frame destruction until it suspends or finishes;
ordinary C++ execution between suspension points continues.

Exceptions from a child task or a condition can be caught by the coroutine awaiting it. An uncaught exception
cancels the task; a standalone scheduler propagates it, while a component faults and cancels its other tasks.
`Scene::GetUpdateCount()` identifies scene updates; the scheduler's explicit-frame overloads use that identity to
avoid resuming tasks created during a hook twice in one frame.

Signals, typed component references and versioned save data follow in M10.5–M10.7.
