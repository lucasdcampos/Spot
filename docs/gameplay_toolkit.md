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

Tweens, coroutines, signals, typed component references and versioned save data follow in M10.3–M10.7.
