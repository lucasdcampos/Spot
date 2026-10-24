# Gameplay toolkit

The sandbox applies these APIs to component timers, weapon recoil, aiming, flash fades and coroutine-driven
camera bob. Its F9/F10 checkpoint uses `SaveData` and expanded scene trees; see [the sandbox](sandbox.md).

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

## Signals

`Signal<Args...>` delivers synchronous gameplay messages without a scene or global event bus. `Connect(callback)`
returns a move-only `SignalConnection`; keep it as a member or local for the subscriber's lifetime. Its destructor
disconnects, and `Disconnect()` can end the subscription earlier. `IsConnected()` also detects signal destruction
or `DisconnectAll()`. Empty callbacks yield a disconnected connection.

```cpp
Spot::Signal<int> healthChanged;
auto connection = healthChanged.Connect([](int health) { UpdateHealthBar(health); });
healthChanged.Emit(80);
```

Callbacks run in connection order. Disconnecting another listener skips it even during an emission; newly connected
listeners wait until the next emission. Destroying the signal stops the current emission and invalidates outstanding
connections safely. Reference arguments preserve references; value arguments are copied for each listener.
Exceptions propagate to the emitter, stopping that emission. Signals are intended for the game thread; cross-thread
messages should be marshalled to it first.

## Typed component references

`ComponentRef<T>` stores an `EntityId`, and `Get(scene)` resolves the entity's first component of type `T` each time.
`IsValid(scene)` tests resolution. Construct it from a component pointer or an entity id, assign with `Set()` or
`SetEntityId()`, and clear it with `Reset()`. Missing entities and missing or removed components resolve to null.
Replacing a component of the same type on that entity makes the reference resolve to the replacement. The scene
argument prevents looking up a target in another scene accidentally.

```cpp
Spot::ComponentRef<Spot::PointLight> lamp;

void Controller::Serialize(Spot::Archive& archive) {
    archive.Field("lamp", lamp);
}

void Controller::OnUpdate(float) {
    if (auto* light = lamp.Get(GetScene())) {
        light->SetIntensity(2.0f);
    }
}
```

Archive writes the reference as an entity id, so scene copies and prefab instances remap it through the same rules
as `EntityId` fields. The inspector shows the expected component type, filters its entity picker, and accepts only
matching entities on drag-and-drop. A saved mismatched or missing target remains editable and resolves to null.
References identify an entity and a type, not a particular component instance; use `ComponentId` where instance
identity matters. Forward-declared types can be referenced, but must be complete when resolved or serialized.

## Save data

`SaveData` writes Archive fields as a versioned JSON save. Give it `Application::GetUserDataDirectory()` for a game's
persistent storage, or an explicit directory for tools and tests. Save names are plain UTF-8 file names, not paths;
the store creates its directory as needed.

```cpp
Spot::SaveData saves(Spot::Application::Get().GetUserDataDirectory());
const auto serialize = [&](Spot::Archive& archive, std::uint32_t version) {
    archive.Field("score", score);
    // When loading an older version, migrate its fields here.
};
auto result = saves.Save("progress.json", 2, serialize);
result = saves.Load("progress.json", 2, serialize);
```

The callback receives the written version on save or the file's actual version on load. Files newer than the
supported version are rejected before the callback runs. Older versions can read their original field names and
migrate them, while missing fields keep their defaults through Archive's usual rules. `SaveDataResult` reports
success, missing or malformed files, unsupported versions, invalid names, I/O failures and serialization errors,
with the version and an explanatory message. The store does not log errors automatically, so the game chooses
how to present them.

Serialization finishes in memory before the file is touched. A temporary file is written and closed before replacing
the slot. Where standard rename cannot replace a file (Windows), the previous file is moved to a backup and restored
if replacement fails; a failed rollback reports the backup's location. Successful writes remove the backup and
temporary files. This is a single-writer store; on Windows, a process interruption during the rename sequence can
leave a backup for recovery. Loading callbacks should read into temporary game data before applying changes if
they need transactional changes to live objects.
