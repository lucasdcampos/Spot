# Particles

`Curve` evaluates cubic Hermite segments, with independent incoming and outgoing derivatives.
`Curve::Linear(start, end)` makes a linear ramp over 0–1. `Gradient` interpolates linear HDR RGBA
colors, including alpha. Both clamp outside the keys and use identity values (1 or white) when empty.

Use `SetKeys()` to author keys. Times are sorted; the last duplicate wins and nonfinite keys are
removed. Evaluation allocates nothing and locates its segment with binary search. Keys can use any
time domain; particles use normalized age.

Archive fields save these types as nested `keys` arrays. The inspector can add/remove keys and edit
times, values, tangents and HDR colors with the normal undo and prefab override support.

## CPU simulation

`ParticleSimulation` is the code-only API. Configure it with `ParticleSettings`, call `Advance()`
with scaled game time, and read `GetParticles()`. `Restart()` restores the seed and clock; `Stop()`
stops emission while live particles drain, or clears them when requested. `Emit()` adds an immediate
burst, independent of automatic emission. `Seek()` replays from the seed at one emitter pose.

`ParticleSystem` wraps this API for entities. Configure it with `SetSettings()`; add a texture with
`SetTexture()`. A lazy `ParticleSceneSystem` advances active components after gameplay updates.
Disabling a component freezes it. Runtime particle state is transient; scenes save configuration
and the texture asset id. Editing configuration resets its simulation; merely inspecting or saving
does not. No entity or component pointers are cached by the system.

Simulation uses a 120 Hz accumulator, retaining substep time across frames. Each emitter has its own
xorshift seed. Sphere sampling is uniform in volume; cones emit from a disc toward a solid-angle
cone around +Y; boxes use full extents. Rate births and timed bursts are aged from their actual
emission times. Bursts at zero fire once initially and on each loop; end-time bursts fire at the end.

Start ranges set lifetime, speed, size and rotation. Curves multiply start size and launch speed;
rotation is an additive angle over normalized lifetime. A gradient multiplies start HDR color and
alpha. Gravity accumulates independently of the speed curve. World-space particles bake the emitter
transform when born; local-space particles follow the current transform when rendered. Gravity is
expressed in the simulation's space.

Storage is contiguous, reserved to the configured capacity and reused through swap removal. Excess
births are counted by `GetDroppedCount()`; each emitter is capped at 100,000 particles. Updates accept
at most one second and drop excess stalled-frame time; seek is bounded to 60 seconds. Settings clamp
invalid/nonfinite data and bound bursts, flipbooks and ranges. Evaluation and steady simulation
allocate no memory. Profiling records the simulation scope.
