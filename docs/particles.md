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

## Rendering

`ParticleBatch` converts particles into billboard instances and sorts alpha particles globally, back
to front along the camera axis, with stable ties. Additive particles follow. Velocity stretching
projects velocity onto the camera plane; stationary or head-on particles retain a valid billboard.
Local particles inherit emitter position and the largest scale axis. Rotation turns normal billboards.
Flipbooks use row-major cells, normalized lifetime and an optional number of cycles.

`ParticleRenderer` has a code-only `Begin/Draw/End` API and `DrawScene()`. It uploads one instance per
particle and merges consecutive draws that share texture and blend mode. Buffers and staging vectors
retain capacity. No texture produces an analytic soft disc; an assigned imported texture provides its
own coverage. Alpha and additive colors remain linear HDR through bloom and tonemapping.

The game renderer and editor viewport draw particles before post processing. Camera layer masks apply.
Particles never write depth; the fragment shader compares against readable opaque depth and uses
`softDistance` in world units to fade intersections. Perspective and orthographic depth are supported.
Single-sample views reuse camera depth directly. Multisampled views ask `Renderer3D::RenderCameraDepth()`
for an optional single-sample depth pass only while live particles exist. That pass reuses submitted
transforms, culling and submeshes, includes cutouts and mirrored/double-sided meshes, and ignores
transparent surfaces. It includes meshes regardless of whether they cast shadows. Custom renderers
can supply their own readable camera depth. Without it, particles render without depth occlusion.

Particles are sorted together across emitters, but form a separate pass after transparent meshes.
Interleaving particles with transparent mesh surfaces is not supported. Multisampled camera depth
adds geometry work; use single-sample views when that cost outweighs MSAA.

## Editor preview

Select an entity with a ParticleSystem to simulate that emitter in edit mode. The inspector offers
Play/Pause, Restart and a bounded 0–60 second seek control, plus the live particle count. A different
ParticleSystem on the same entity can take the preview with its Preview button. Curves show sampled
plots and gradients show color strips above their editable key arrays. Confirm key times with Enter
to reorder them after editing. Curves and gradients are recorded as whole-field changes so reordering
also preserves multi-selection edits and prefab overrides.

The document owns preview state by entity id, component id and scene revision. Selection changes
clear the old emitter, removals resolve safely, and reloads restart the preview. Only the selected
emitter advances, with real editor time; the scene's gameplay and physics never run. Starting play
clears edit-mode preview state. Preview controls change no serialized fields and create no undo
entries. Configuration edits still use normal undo and prefab overrides.

## Sandbox effects

The playground's ParticleEffects component holds serialized definitions for muzzle glow, metal sparks,
concrete and wood dust, explosion fire and smoke, debris and the physics-gun beam. Each definition
contains ParticleSettings, a count and an optional imported texture. Defaults use procedural discs,
so effects need no GPU-loaded assets to simulate. Tune definitions on the Game entity in the editor.

WeaponHolder emits at the muzzle and raycast hit; surface markers select the impact. Explosions
emit fire, smoke and debris beside their existing light/sound/impulse. Spawnable props carry a separate
ImpactParticles component with a startup grace period and contact cooldown; it works without audio.
The physics gun emits along its full held beam with a fractional, configurable rate accumulator.

ParticleEffectPool owns at most 128 transient emitters by entity id. Each is capped at 512 particles
and five seconds of lifetime; expired emitters become inactive slots for reuse. Full pools drop new
effects and expose a counter. Particle storage and per-slot configuration vectors retain capacity
across resets; reusing configurations avoids recurring copies that allocate on beam updates. A new
slot or a larger authored key array can still grow capacity. Transient effects have no world-save
ownership and never enter spawning undo. They do not add physics bodies.
