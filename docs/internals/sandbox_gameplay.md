# Sandbox gameplay framework

`sandbox/src/gameplay/` holds the sandbox's gameplay as a framework of independent systems that any game can take,
whole or in parts, and change. The framework knows nothing of the sandbox: the sandbox's own code, in `sandbox/src/`,
builds on it, as another game would. The sections below describe what each system does, what it needs and how a game
extends or replaces it.

## Layout and rules

- **One folder per system**, such as `gameplay/core/` or `gameplay/vehicles/`, in namespace `Gameplay`. Components
  register with `SPOT_COMPONENT()` written inside the namespace, so they save under their plain names.
- **It builds with the sandbox's code**, wherever that builds: the engine's `sandbox` executable, the project's game
  code that the editor loads (`sandbox/src/CMakeLists.txt`) and the tests. `sandbox/src/` is an include directory of
  all three, and files include the framework by its path from there: `#include "gameplay/core/pawn.h"`.
- **Sources include only** the engine (`<spot.h>`, `<spot/...>`), the standard library, their own folder and the
  folders it needs. `gameplay/check_includes.cmake` lists what each folder needs (`GAMEPLAY_NEEDS`) and fails on any
  other include; the headless tests run it. A folder that comes to need another adds it there.
- **Assets and input actions are fields**, never names in the code, with the sandbox's as defaults, so a game that
  takes a system brings its own.

These are direct folder dependencies; copy their dependencies too. HUD elements live in the system they describe,
so those systems also bring `hud` even when a particular game does not author their elements.

| Folder | Direct dependencies |
|---|---|
| `core`, `effects`, `persistence` | Engine only |
| `camera` | `core` |
| `hud` | `core` |
| `health` | `core`, `hud` |
| `character` | `camera`, `core`, `effects`, `health`, `hud`, `persistence` |
| `inventory` | `core`, `hud`, `menus` |
| `weapons` | `camera`, `core`, `effects`, `health`, `inventory` |
| `vehicles` | `core`, `environment`, `health`, `hud`, `persistence`, `weapons` |
| `environment` | `core`, `effects`, `hud` |
| `menus` | `core`, `hud` |
| `game` | `core`, `health`, `hud`, `menus`, `persistence` |

## Core

`gameplay/core/` needs only the engine. It has the mechanisms the other systems meet through, so none includes another
to switch it off or read its internals.

- **Possession.** A `Pawn` is anything that can be controlled; a `Controller` controls one at a time, from an entity of
  its own, so it outlives a pawn and stays the same from one to the next. `Controller::pawn` names the pawn it
  possesses as it starts; `Possess()` takes a pawn from any other controller. `PlayerController`, the local player's,
  turns on the possessed pawn's `viewCamera` and off the previous one's. `Players` finds a scene's players and their
  pawns, for what serves a player rather than a pawn (HUD, menus, console commands). `GetBody()` and
  `FindComponent<T>()` follow the occupant while a player drives another pawn, and take the local player index;
  consumers never search the scene for a player component.
- **Intents.** `PlayerController` calls the `PlayerInputListener`s on its current pawn and its children, stopping at
  other pawns. These adapters translate actions through their input contexts into `CharacterIntent`, `VehicleInput`
  and `ItemInput`; movement, simulation and item behavior consume those intents. On transfer the listeners clear
  held controls and release their input. Other controllers and tests use `CharacterMovement::SetIntent()`,
  `Vehicle::SetInput()` and `Inventory::Use()` directly.
  Movement and vehicle adapters return true from `IsInputBeforeFixedSteps()`: a core scene system dispatches their
  input before the first fixed step, so a press or release affects that frame's physics. Other adapters run from the
  controller's update, after the view has turned, keeping item aim aligned with the camera.
- **View point.** `Pawn::GetViewPoint()` is where the pawn looks from and along: its `eye` entity's position and
  forward, or its own. It is virtual, for pawns that look otherwise.
- **States.** `Pawn::SetState()` and `HasState()` are names set by whoever knows them and read by whoever cares ("Dead",
  "Seated"), so a rule such as "nothing is used while dead" is a list of names on the part that follows it
  (`Interactor::blockedBy`). They are transient.
- **Input contexts.** A part reads its actions through an `InputContext`, a name, a priority and the actions it takes,
  activated on its pawn's controller (`Pawn::GetControllerOf(entity)`). An action an active context of a higher
  priority takes does not reach those below it, so who has a button is settled by priorities, whatever order parts
  update in. A context deactivated keeps taking its actions until the frame ends, at the `PlayerController`'s late
  update, so the press that closed it reaches nothing below; `Consume()` keeps a press from every context after, as
  the `Interactor` does with the press it used. Contexts read nothing without a `PlayerController`, so a pawn nobody
  possesses reads no input. Menus keep the engine's `Input::PushCapture()`.
- **Interaction.** An `Interactor` on a pawn casts along its view point within `reach` and finds an `Interactable` on
  the entity hit or above it; failing that, an `InteractionHandler` on its own entity that would use what was hit. It
  publishes the focus and its prompt (`GetFocus()`, `GetPrompt()`) and uses it on its `action`, unless a context above
  its own takes that action or the pawn has a state of `blockedBy`.
- **Trace volumes.** A `TraceVolume` is a volume without a collider that shots and blasts still meet, as water.
  `Traces::Raycast()` finds where a ray enters the nearest; `Traces::Blast()` tells every volume of a blast, which
  answers whether it went off at its surface or inside it. What shoots or explodes knows no kind of volume by name.

The sandbox's player is a `Character` possessed by the Player Controller entity. Its parts read input through contexts with
these priorities: movement, look, the interactor and the spawn keys at 0; the equipment, the physics gun and the tool
gun at 10; the open weapon wheel at 20 (Look, Fire, Aim); a carried prop at 30 (Fire, Aim, Use, and Look while Aim is
held); the physics gun holding a prop at 30 (Fire, Aim, Use, Switch Weapon, and Look while turning); placing from the
spawn menu at 40 (Fire, Aim); and the possessed vehicle at 50 (its controls and Use). The carrier is an interaction
handler, and a vehicle's seat is an `Interactable`.

Extending it: a new controller (an AI's) derives `Controller` and overrides `OnPossess()` and `OnUnpossess()`; a pawn
that looks from elsewhere overrides `GetViewPoint()`; something usable derives `Interactable` and implements
`Interact()`; a tool that claims what it looks at implements `InteractionHandler`; a new modal tool owns a context with
a priority above what it takes buttons from. Tests: `tests/sandbox/gameplay/core_tests.cpp`.


## Camera

`gameplay/camera/` needs `core`. `CameraShake`, on a pawn, shakes the view by up to `maxAngle`, the stronger of
overlapping shakes winning, for the view to add after where it looks (`GetRotation()`); the aim stays steady.
`CameraShake::Add(scene, center, strength, radius)` shakes every view within the radius, less and shorter the farther
its pawn's view point is, so a blast knows no viewer. `FirstPersonView` turns the head and combines the pawn's zoom,
running state, recoil and camera shake; its aim remains independent of shake. `ChaseCamera` follows any pawn from
behind, with orbit, recentering, wall avoidance and a wider view at speed. Possession enables the pawn's camera;
the camera entity stays active so its follow component can update when enabled. `followView` instead follows
the pawn's eye position, yaw and pitch directly, leaving Look to the first-person view. `lookHeight` offsets its
pivot above the eyes, and `shoulderOffset` moves it laterally (negative for the left shoulder). A sphere sweep
shortens the complete offset boom at walls. The camera converges on the eye ray's first hit, so its reticle shows
what the existing interaction and weapon rays target even at close range. The vehicle orbit mode is unchanged. The first-person head uses
world rotation, so its aim remains independent of a seat's inherited rotation.

## Character

`gameplay/character/` needs `core`, `camera`, `effects`, `health`, `hud` and `persistence`. `Character` is a pawn on
foot: its view point comes from `FirstPersonView`, its motion describes the walk cycle, and `Place()` restores its
facing and velocity when it leaves a seat. `CharacterMovement` consumes a `CharacterIntent`, including analog motion,
jump, crouch, sprint and noclip; action names live in its `controls`. The controller supplies these intents for a
local player. Tests or another controller can supply them without input bindings.

Movement follows states: `sprintBlockedBy` prevents running, `blockedBy` prevents walking and jumping, and
`inactiveWhen` suspends the physics body immediately, as "Seated" does. Landings emit `Character::OnLanded`, connected
to `FallDamage`; footsteps emit `OnFootstep`, which `Footsteps` sounds through the scene's `SurfaceLibrary`.
`Flashlight` switches its referenced light. `Carrier` handles an interactor's light bodies, sets "HandsBusy" while
holding one, uses `core/PhysicsDrive` to carry and turn it, and releases it on death, disable or a checkpoint load.
Its `releaseWhen` states also let another action take the hands without naming the carrier: the playground adds
"Placing", so the spawner only sets its own state. Rotation sensitivity comes through `UserSettingsListener`.
`CrosshairElement` shows the aim and movement spread. Movement owns no spawn point or death timer; the session
calls `Pawn::ResetMotion()`, implemented by `Character` through `CharacterMovement::SetVelocity()`, when returning a
player to the start. The view direction, crouch and noclip settings remain as they were.
`Character::thirdPersonCamera` optionally supplies an inspection view. `SetThirdPerson()` switches the local
camera and the ThirdPerson state, leaving `GetViewPoint()` unchanged for movement, interaction and shooting.
Possession uses the selected camera on return from a vehicle; changing the selection while seated leaves the
vehicle camera in control. The console commands themselves belong to the sandbox.

`CharacterVisual` presents an authored `modelRoot` with an animator reference and configurable speed parameter,
seated/crouching layer names and offsets. It follows aim yaw on foot and the root's full rotation while seated.
Additional `stateLayers` map arbitrary pawn states to authored pose layers, including seat-specific poses.
The animator controller must use in-place locomotion; presentation never drives the physics body. Skeletons,
bone names, masks, clips, culling layers and the model asset remain prefab/scene data. Tests exercise an unrelated
one-joint rig and custom parameter/layer names.
Tests: `character_tests.cpp`.

## Inventory and weapons

`gameplay/inventory/` needs `core`, `hud` and `menus` (to close the wheel before a menu pauses time). `Inventory`
discovers child entities with a `HeldItem` in sibling order, equips the selected one and sends it an `ItemInput`.
Slot actions, cycling and the wheel know no item kinds. `stowedBy` makes the intent inactive; `blockedBy` prevents
use. `AddFromPrefab()` makes a held instance without its world rigidbodies, colliders or renderers, preserving the
selection. The sandbox's `give` command uses it. The physics gun and tool gun are sandbox `HeldItem` subclasses on
child entities; their model and icon belong to the item, as firearms' do.

An item describes its `ItemStatus` (name, icon, count, detail, progress and hints) and `ItemPose` (reload, empty and
working). `ItemPanelElement`, `WheelElement` and `ViewModel` consume these contracts, so adding another behavior
means one `HeldItem` subclass. `ViewModel` draws the item's model, with its named nodes (Muzzle, Eye, Hip, Slide,
Magazine, Rocket, Prongs), and takes the walk cycle and states from its pawn. The wheel restores the previous time
scale on closing, unless another system has changed it. Tests: `inventory_tests.cpp`, including a new item subclass
shown by the existing HUD.

`gameplay/weapons/` needs `core`, `inventory`, `health`, `effects` and `camera`. `Firearm` is a held item: its data
describes cadence, spread, ammunition, aiming and effects. Without a `projectile` prefab it hits along rays, through
`Traces` and `Damage`; with one it launches that prefab. Games override `FireShot()` to change behavior. Its timers
keep reloads and cooldowns with the item. `Projectile` handles flight, collision, trace volumes and lifetime;
`Explosive` provides the blast and `IgniteOnDamage` its delayed fuse. Rockets and explosive barrels compose these
parts in their prefabs. No damage source names the sandbox's NPCs or barrels. Tests: `weapons_tests.cpp`.

`WorldModel` presents the same selected item in the world. Give it an entity id for its mount; that entity can
use the engine's `BoneAttachment`, or any authored transform. An optional animator reference and `poseLayer`
control the weight of an existing holding layer. Skeletons, bone names, masks, clips and grip offsets belong to
the character prefab; no humanoid or sandbox model is assumed. Models are instances below the mount and cast
ordinary world shadows. Switching items replaces the visual; hidden pawn states, a disabled inventory or a
missing mount clear it. `renderLayer` optionally assigns every world-model node to a presentation layer,
so the local camera can exclude its own world body and equipment without changing external views. The configurable `muzzleNode` supplies its world muzzle, used by Firearm when no active
first-person ViewModel exists. Runtime references are serialized with expanded world saves to avoid duplicates.
Tests use an unrelated one-bone rig and arbitrary item subclasses, without sandbox assets.

## Vehicles

`gameplay/vehicles/` needs `core`, `environment`, `health`, `hud`, `persistence` and `weapons`.
`Vehicle` is a pawn driven by `VehicleInput`; its `controls` map a player's actions, and other controllers call `SetInput()`. A `VehicleSeat` gets an existing pawn
in, marks it "Seated", transfers possession and keeps it at the seat's position and rotation. An optional `poseState` is set on entry and cleared on exit,
so character presentation can select the seat's authored pose without a vehicle or skeleton dependency. Exiting finds room at authored
exits or around the body's boxes, then restores possession and places the pawn facing and moving with the vehicle.
Death and checkpoint loading get it out too. The vehicle exposes its occupant through the pawn contract so the HUD
can keep showing that occupant's health.

`WheeledVehicle` discovers `VehicleWheel`s below it. Each wheel's entity stays at its rest position; a visual child
moves with suspension, steering and spin. Per-wheel drive and brake shares, axle ids, steering limits and suspension
settings replace the four fixed wheel slots. `Drivetrain` is serialized data on the vehicle. The tire, gearbox and
suspension simulation preserves the car's math, distributing resting loads across its wheels. Separate
`VehicleEngineSound` and `VehicleTireSound` use the vehicle's public telemetry. `VehicleElement` shows its gauges
and controls; `ChaseCamera` follows its pawn. `ImpactDamage` is authored on the car prefab rather than built into the
vehicle simulation.

To make a different car, copy the car prefab, replace its model, arrange its wheels, tune shares, drivetrain and seat,
and mark it spawnable. A new vehicle behavior derives `Vehicle`, implementing its simulation and telemetry; the same
seat, camera, HUD and save listener serve it. Tests: `vehicle_tests.cpp` builds three-wheel and six-wheel layouts on
flat ground, and a separate vehicle subclass with the existing seat, chase camera, HUD, damage and checkpoint load.
`tests/sandbox/car_tests.cpp` retains the playground car's five behavioral checks.

Every new drivable vehicle derives `Vehicle`; shared possession, input routing, seats, camera, HUD and checkpoint
handling stay in that base and its companion components. `VehicleInput::lift` adds a vertical axis, read from the
optional `VehicleControls::ascend` and `descend` actions. `VehicleInput::strafe` reads the optional `strafe` action
for lateral intent, which aircraft use to bank. Unassigned actions are omitted from the vehicle's input
context, so ground vehicles keep their original controls.

`Helicopter` is a second simulation using this contract. Its Rigidbody receives rotor lift, horizontal drag and
stabilizing torque through public physics APIs. Forward/backward and lateral intent tilt the rotor plane, with
combined tilt limited to `maxTilt`; steering commands yaw rate, and lift commands climb rate. Neutral input levels
it and damps vertical velocity. Lift compensates for
tilt and scales with squared rotor speed during spin-up and coast-down. Without a driver or while dead, the engine
spools down and gravity brings it down. It does not lock the body's transform or bypass collisions. The authored
`mainRotor` and `tailRotor` entity references animate around local Y and X; their meshes are centered on those pivots.
Its gauges and hints override the base descriptions, and `VehicleEngineSound` uses its RPM and throttle queries.
Its separate `FlightPitch`, `FlightRoll`, `FlightYaw`, `FlightAscend` and `FlightDescend` actions keep the keyboard's
W/S pitch, A/D yaw and Space/Ctrl lift, while the gamepad's left stick pitches and banks, LB/RB command yaw and
RT/LT command proportional climb/descent. The pitch and roll axes each use a 0.2 dead zone. Axis key composites
appear as paired keys in HUD hints. Flight, neutral hover, banking, yaw, descent, shutdown and input routing have
coverage in `tests/sandbox/gameplay/helicopter_tests.cpp`.

`HelicopterDownwash` is an optional companion on the helicopter prefab. It traces from the main rotor along its
downward axis to the nearest enabled water within `range`, emitting at most one ring per frame at `interval`.
Squared rotor speed and distance falloff control opacity, including spin-up and coast-down. Flight physics needs
no water or effect library. The companion uses `Water::Ripple()` and the bounded effect pool, leaving impact
spray and splash sounds to collisions. Headless tests cover placement, fading, range, cadence and shutdown; the
effects GPU test captures `helicopter_downwash.png` from the authored helicopter over the pond.

`VehicleHealth` extends `Health`: the authored cars, coupe and helicopter start with 300 health and no
regeneration. Bullets and nearby blasts use the existing damage receivers; collisions above `safeImpactSpeed`
(default 8 m/s) take `(closingSpeed - safeImpactSpeed) * damagePerSpeed` health. At zero health it marks the pawn
"Destroyed", clears its input, ejects the driver and unpossesses any remaining controller. Seats refuse dead or
destroyed vehicles, and the drivetrain, flight controls and engine audio require `Vehicle::IsOperational()`.
The rigidbody and colliders remain, so the wreck still interacts with physics.

Its own configurable `blast` has a 12-meter radius, 600 damage at the center and a 4,000 Ns impulse. It uses the
static `Explosive::Explode()` to damage and push surroundings, play the existing explosion particles at 2.4 times
the base scale, and shake cameras, without deleting the wreck. The damage kills a nearby full-health player,
including the ejected driver even with the wreck shielding part of the blast.
A separate off-center impulse adds `wreckLaunchSpeed` (6 m/s by default) upward while preserving existing momentum
and turning the body. It is applied once and is not repeated on checkpoint load. Destroyed wheeled vehicles release
their parking brakes; destroyed helicopters stop lift, stabilization and flight drag while their rotors coast down.
Every descendant mesh receives `burnedMaterial`, including glass, lights and wheels; the sandbox authors `materials/vehicle_burned.material`, a
dark, rough, non-emissive soot material with object-space texture projection. A plain charred fallback works in
games without that asset. Health and the destroyed flag survive world saves; loading a wreck reapplies its state
and material without another blast. `Health::Restore()` alone does not make a destroyed vehicle usable.
Tests: `vehicle_health_tests.cpp`, the authored prefabs in `car_tests.cpp`, and the burned-car offscreen capture.

## Health

`gameplay/health/` needs `core` and, for its elements, `hud`. Every source of damage calls `Damage::Apply()` with a
`DamageInfo` (amount, a type the game names, point, impulse, instigator and source), or `Damage::ApplyRadial()` for a
blast; neither knows its targets. `Apply()` gives the damage to the `DamageReceiver`s of the entity hit or of its
nearest ancestor that has some, or of the ragdoll whose body it is, and pushes the body hit unless it is a ragdoll's,
which its receivers push. `ApplyRadial()` gives each receiver within the radius a share once, less with distance,
characters by the nearest point of their capsules and less behind cover, and pushes the bodies within it away. Each
damage that reached something is told by `DamageSystem::OnDamageApplied`, as the hit marker listens.

- **`Health`** is a receiver: it takes what `AdjustDamage()` leaves of damage, heals a while after the last hurt, and
  at none sets its pawn's "Dead" state and emits `OnDied`. It stays dead until the game calls `Restore()`, which
  restores full health, clears "Dead" and emits `OnRestored`. Parts that react to death listen to `OnDied` or read
  "Dead"; health owns no respawn policy or timer.
- **`FallDamage`** hurts its entity's Health as landing at a speed does, between a safe and a lethal speed; whatever
  moves the entity calls `Land()`. **`ImpactDamage`** damages what its body runs into, by closing speed, without
  pushing it again. Both the closing speed and the source body's velocity toward the target must reach `minSpeed`;
  walking into a parked or slowly moving vehicle does not hurt. The source velocity is sampled before each physics
  step, so a collision that stops the source still deals damage.
- **HUD elements**: `HealthElement` (number, icon and a bar easing toward it, in the warning color when low),
  `DamageFlashElement` (the edges of the screen as the pawn is hurt) and `HitMarkerElement` (when the pawn damages
  something of its `damageTypes`). The death screen belongs to `game/`, which owns its countdown.

The sandbox's player has `Health` and `FallDamage`; its movement is a receiver that throws it back from "Explosion"
damage, its NPCs and explosive barrels are receivers, and the car prefab has `ImpactDamage`. A game changes how
damage hurts by subclassing `Health` (armor overriding `AdjustDamage()`) or with a receiver of its own in its place.
Tests: `health_tests.cpp`.

## Effects

`gameplay/effects/` needs only the engine. The scene's `EffectLibrary` holds named `EffectDefinition`s: particle
layers (`ParticleLayer`, each with its own scale and offset in the effect), a light, played as a `Flash`, and a sound.
`EffectLibrary::Play(scene, name, position, direction, options)` plays one, scaled and with more or fewer particles;
`PlayAlong()` spreads an effect's particles along a segment, as a beam, and `PlayTo()` sends them flying to a point,
as a tracer. The `EffectPool` reuses a fixed number of emitters, and dims lit layers by the light it is given
(`SetLight()`), which the day cycle sets.

Surfaces: the scene's `SurfaceLibrary` lists kinds of surfaces, each with impact and footstep sounds and an impact
effect; a `Surface` names its entity's by its index there, the first being that of entities without one. A new kind,
such as glass, is a new entry. `ImpactSounds` and `ImpactParticles` play the surface's on collisions. Effects the
library lacks play nothing, so a game that takes the effects brings its own library. Tests: `effects_tests.cpp`.

## Environment

`gameplay/environment/` needs `core`, `effects` and `hud`. `DayCycle` turns the sun through the day, with the sky, fog
and exposure, and lights the effects as the day is; its `RegisterScriptBindings()`, which the sandbox's module calls,
gives scripts the same API as before. `NightLight` lights its lamp as the daylight fades. `Water` is a `TraceVolume`:
shots go `shotDepth` under its surface, splashing it, and blasts at or under it throw up its water; it slows bodies in
it and splashes what falls in, bodies and any character controller. Its splashes play the effects named after
`splashEffect` ("splash", ".ripples", ".mist", ".plume") with its own sounds. `Ripple()` emits only the ring layers,
projected onto the covered surface with caller-controlled opacity and scale. It rejects inactive water, uncovered
points and invalid strength/scale. `ClockElement` shows the day's time.
Tests: `environment_tests.cpp`.

## Persistence

`gameplay/persistence/` needs only the engine. `WorldSave::Save()` checkpoints the root entities that are
`Persistent` and not `Transient`, with what is below them; `Load()` replaces them with those saved, telling every
`SaveListener` before, so it lets go of what will be destroyed, and after, with the roots made. The sandbox marks
what the player can make, move or break: its props, NPCs and spawners, and placed lamps; rockets are Transient. The
vehicle's seat, the carrier and the physics gun let go, the undo list clears and the spawner finds its props again by
their "Spawned" tag. Tests: `persistence_tests.cpp`.

## HUD

`gameplay/hud/` needs `core`. The `Hud` of a player shows, each frame, the `HudElement`s below it whose conditions
hold: all the pawn's states of `showWhen`, none of `hideWhen`, and their own `IsWanted()`, which is asked every frame
so an element can keep track meanwhile, as a panel an action toggles. An element's entity is active while any element
on it shows, so two can take turns at the same widgets: the `ItemPanelElement` hides "InVehicle" and its
`VehicleElement` shows on it, in the same place. Without elements below it, the Hud makes its `interface` prefab.
The HUD collects elements into one flat frame-local list, avoiding a separate allocation for each entity's element
list and retaining the visibility pass before widget refresh. Elements live with the systems they show; `hud/` has the generic `PromptElement` (what the Interactor would do, which
a game's subclass extends, as the sandbox's for placing and carrying) and `StatisticsElement`. `InputPrompts` writes
controls as the player has them bound ("[E]  Drive"), keyboard or gamepad, so hints follow rebinding.

## Menus

`gameplay/menus/` needs `core` and `hud`. A `Menu`, on an entity that stays active, shows its canvas while open; its
toggle action opens it while no other menu is open and closes it, and the canvas's cancel closes it. It eases in and
draws its controls' states in its `MenuStyle`, may pause the game, and tells `MenuSystem::OnOpening` first, so a tool
that slows time, as the weapon wheel, closes before. `MenuPart`s on its entity take part: `PauseMenu` has
`SettingsMenu`, which loads, applies and saves the player's `UserSettings`, and `BindingsMenu`, which rebinds controls.
`PauseMenu::mainMenuButton` optionally loads `mainMenuScene`, closing the menu first to restore simulation time.
An unassigned button leaves games without a main menu unaffected.
`UserSettings::Apply()` reaches the `UserSettingsListener`s of each player's controller, pawn and its children,
including the occupant while driving, stopping at nested pawns. The view takes sensitivity and field of view; the
carrier and held physics gun take rotation sensitivity without reading the view. `HelpPanel` is the HUD's controls panel. The sandbox's
spawn menu is a `Menu` of its own. Each menu has its own always-active host entity, with only that menu's parts,
so initialization and callbacks are registered once.

## Session and game rules

`gameplay/game/` needs `core`, `health`, `hud`, `menus` and `persistence`. One `GameMode` in the scene remembers each
local player's initial pawn and world position, using `Players` and entity ids. It possesses unpossessed starting
pawns, listens to their `Health::OnDied` and schedules a return after `respawnTime`. The timer belongs to the mode,
runs in game time, and is canceled when the mode is disabled or a tracked player disappears. Players joining later
are tracked on update. A pawn below `fallHeight` returns immediately without healing; seated pawns are excluded.
Returning changes the world position and calls `Pawn::ResetMotion()`, preserving the player's view direction.

`RespawnPlayer()` restores health and possession after returning the pawn. A game subclasses `GameMode` and overrides
that hook to change its respawn rule. `restartScene` selects what `Restart()` loads. `DeathScreenElement`, still
serialized with the same component name on the interface prefab, shows the mode's countdown for its HUD's player.
Without a mode, health stays dead and the health system works alone.

`SessionControls` locks the cursor on start and handles configurable save, load, pause, restart and capture actions.
Quick save and load use `WorldSave` with `checkpoint` under the application's user data directory; tests can supply a
directory with `SetSaveDirectory()`. The pause menu handles Pause when present; otherwise Pause releases the cursor,
and pressing it again quits. Restart calls the mode. The capture click runs in late update, after gameplay has seen
the unlocked cursor, and never captures clicks on an open menu. Its sound is an authored `clickSound` reference.

The playground's Controls entity has `GameMode`, `SessionControls` and the sandbox's `GameControls`. Only console
commands and cheats, fallback NPC population and the developer tools' font remain in `GameControls`. The spawn
menu uses its `playerIndex` to resolve the player's spawner and undo list through `Players`, while sandbox commands
and the smoke test use the first local player, following that player's body while driving. Tests:
`tests/sandbox/gameplay/session_tests.cpp`; the playground tests retain their quick-save and death behavior checks.

## Recipes

### A different car, with any number of wheels

Copy `sandbox/assets/prefabs/spawnables/props/car.prefab` through the editor, keeping a collider and `Rigidbody` on
the `WheeledVehicle` root. The origin is at ground level and forward is -Z. Replace the body model and arrange
children with `VehicleWheel` at each wheel's resting center. A visual child, referenced by `visual`, moves with
suspension, steering and spin; the entity with `VehicleWheel` stays at rest.

Set each wheel's radius, suspension, `maxSteer`, `driveShare`, `brakeShare`, `handbrake` and `axle`. Shares are relative
weights; wheels on an axle share the anti-roll bar. Tune the root's `drivetrain` and mass for the new layout rather
than adding wheel slots in C++. Keep `VehicleSeat` on the collider root so the interactor finds it; its `seat` field
references the seat-position child, and optional `exits` reference ground positions to try when leaving. Assign the
camera entity to the pawn's `viewCamera`, with `ChaseCamera` beneath the vehicle. Engine and tire sounds and
`ImpactDamage` remain separate components. Keep `Spawnable` to expose the prefab in the sandbox's catalog.

Example: [vehicle_tests.cpp](../../tests/sandbox/gameplay/vehicle_tests.cpp), "Three and six wheel vehicles rest and
drive from the same simulation", builds both layouts from data. [car_tests.cpp](../../tests/sandbox/car_tests.cpp)
covers the authored four-wheel car's suspension, possession, drivetrain, steering and death while driving.

### A different kind of vehicle

Derive `Gameplay::Vehicle`, implement `GetForwardSpeed()` and its simulation using `GetInput()`, normally in
`OnFixedUpdate()`. Override `DescribeTelemetry()` for its gauges and, if needed, `DescribeHints()` for additional
controls. A player's controller translates the authored `controls`; another controller calls `SetInput()`.
Register the subclass with `SPOT_COMPONENT()` and author a prefab with its body, a `VehicleSeat` on a collider root,
a referenced camera and `ChaseCamera`. Add `VehicleHealth` for cumulative damage, a blast and a persistent burned wreck, or another `DamageReceiver`, and
`Persistent` if a checkpoint should save it. Entering, exiting, the camera, HUD and checkpoint ejection use the
existing contracts and require no branch for the new type. Optional engine and tire sound components consume the
virtual RPM, throttle and slip queries.

Example: `TestVehicle` and "A new vehicle type uses the existing seat camera HUD damage and save" in
[vehicle_tests.cpp](../../tests/sandbox/gameplay/vehicle_tests.cpp). This test uses a vehicle without wheels or a
drivetrain and the existing systems around it.

### A different weapon or held item

For another firearm, copy a weapon prefab, retain `Firearm`, and author its `displayName`, `icon`, held `model`,
cadence, spread, pellets, ammunition, reload, recoil, zoom, sounds and effect names. Without a `projectile` reference
it fires hitscan rays. For a launcher, reference a prefab with `Projectile` and `Explosive`; its flight, collision,
blast and lifetime belong to that prefab. Add the held instance beneath an entity with `Inventory`, or call
`Inventory::AddFromPrefab()`. The sandbox's `give` command uses the same API. The wheel, HUD and view model discover
items through `HeldItem`, with no list of weapon types to edit.

For a different item behavior, derive `HeldItem`, pass its action names to the base constructor, implement
`OnUse(const ItemInput&)`, and describe what it shows with `DescribeStatus()` and `DescribePose()`. Use `OnEquip()`
and `OnUnequip()` for resources that last while equipped. Honor `active` and `canUse`; input comes from the inventory
or a supplied intent. Serialize any new fields and call `HeldItem::Serialize()` from the override. For a firearm
that shoots differently but keeps ammunition and reloads, override `Firearm::FireShot()` instead.

Examples: [weapons_tests.cpp](../../tests/sandbox/gameplay/weapons_tests.cpp), "A firearm fires into arbitrary damage
receivers from an item intent" and "A projectile prefab launches with its own blast and expires without a firearm";
`CounterItem` in [inventory_tests.cpp](../../tests/sandbox/gameplay/inventory_tests.cpp), tested by "An inventory equips
arbitrary item subclasses and routes intents", "The item panel shows a new item's status without a type branch"
and "Adding a held prefab preserves the selection and strips its world body".

### A different HUD widget or situation

Derive `HudElement` and implement `Refresh(const HudContext&)`, writing into authored UI widgets through entity ids.
Put it below the player's `Hud`, either directly or inside the referenced interface prefab. `showWhen` requires
all named pawn states; `hideWhen` excludes any. Override `IsWanted()` for another condition or background bookkeeping.
The HUD calls it even while the element is hidden, but calls `Refresh()` only while it shows. Configure
`Hud::playerIndex` for the local player it serves; use `context.pawn`, `context.occupant` and `context.Find<T>()`
instead of searching the scene. A situation is another pawn state plus elements authored to show in that state.
Register a saved subclass and call `HudElement::Serialize()` when serializing widget references or new fields.

Example: `StateElement` in [hud_tests.cpp](../../tests/sandbox/gameplay/hud_tests.cpp), "A new HUD element follows
authored states and the HUD's local player". It adds a state and widget without changing the HUD. The sandbox's
`SandboxPromptElement` is a production example extending the generic interaction prompt with placement hints.

### Changing or replacing damage handling

Derive `Health` and override `AdjustDamage()` to change how much a damage type takes, as armor would. Keep
`Health::Serialize()` in an override that adds fields. Existing damage sources, the health HUD and `GameMode` still
use the base contract. To replace health entirely, implement `DamageReceiver` on a component and handle `OnDamage()`;
attach it to the target entity or an ancestor of its colliders. Sources still call `Damage::Apply()` or
`ApplyRadial()` with the same `DamageInfo`. That receiver supplies its own death or state policy; health-specific
regeneration, HUD and respawn require `Health` or a replacement for those consumers too.

Examples: `ArmoredHealth` and "A subclass of Health changes how damage hurts" in
[health_tests.cpp](../../tests/sandbox/gameplay/health_tests.cpp); its `Recorder` and "Damage finds receivers above
what it hits, pushes bodies and tells who listens" demonstrate a receiver that has no health. `Target` in
[weapons_tests.cpp](../../tests/sandbox/gameplay/weapons_tests.cpp) takes firearm hits without `Health`.

### Taking a system into another project

Copy the selected folder under the other game's `src/gameplay/`, plus the dependency closure in the table above.
For example, vehicles bring `camera`, `core`, `effects`, `environment`, `health`, `hud`, `inventory`, `menus`,
`persistence` and `weapons` through their dependency closure; effects need only `effects`. Preserve the
`gameplay/` include prefix and make `src/` an include directory. The game builds these sources and links Spot using
its normal setup; [sandbox/src/CMakeLists.txt](../../sandbox/src/CMakeLists.txt) shows recursive sources with
`spot_add_game()` and `target_include_directories(game_code PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})`.

Register the components as usual and author the new game's input actions, models, camera, effects, surfaces and UI.
Keep the destination game's module entry point (`SPOT_GAME_MODULE()` in its `game.cpp`) and executable entry point;
the copied components use that module's registration list, not the sandbox's module.
Copy any desired example assets through the editor so their metadata and references follow. Nothing in the framework
names a sandbox asset path. No sandbox source file, importer library or project API is required by these folders.
Keep `check_includes.cmake` with the copied code if the game wants to enforce the same boundary.

The registered headless check "The sandbox's gameplay framework includes only the engine and the folders it needs"
in [tests/CMakeLists.txt](../../tests/CMakeLists.txt) enforces this source boundary. The isolated
[world_tests.cpp](../../tests/sandbox/gameplay/world_tests.cpp) and each system's tests construct scenes without
sandbox assets or components; the vehicle example above exercises the copied dependency set's contracts.

## Sandbox boundary review

The remaining files in `sandbox/src/` implement the game, its authoring helpers or its development tools. Their
framework dependencies serve those responsibilities:

| Sandbox code | Responsibility and framework boundary |
|---|---|
| `game`, `game_controls` | Module and day-cycle script registration; console rules, NPC population and developer font |
| `physics_gun`, `tool_gun`, `hook_lines` | Sandbox held items, their physics operations, undo and rendering; item status and pose describe their presentation |
| `prop_spawner`, `spawnable`, `spawn_menu`, `menu_thumbnails`, `undo_list` | Catalog, placement, previews and editing history; player lookup uses `Players`, placement publishes "Placing" |
| `sandbox_hud`, `interface_palette`, `sound_bank` | Sandbox prompts, authored UI styling and click sound; generic HUD and menus stay in the framework |
| `npc`, `npc_spawner` | Sandbox patrols, population and ragdoll damage receiver |
| `grass_visibility`, `shadow_toggle`, `physics_debug_view` | Level culling and developer switches |
| `sandbox_commands`, `smoke_test`, `main` | Explicit game composition and entry points; commands and the smoke driver resolve player parts through `Players` |

The spawner does not call the carrier, and tools do not read camera tuning. Their coordination uses pawn states,
input contexts, public item presentation and `UserSettingsListener`. Checkpoint release uses `SaveListener`.
Framework include restrictions are recorded in `GAMEPLAY_NEEDS`; source changes must keep the dependency table
above in agreement with it.

## Tests

Each system's tests are in `tests/sandbox/gameplay/`, built into `spot_sandbox_tests` and tagged `[gameplay]`. They
run in a `GameplayTests::World` (`gameplay_world.h`): a bare ground whose top is at y = 0, input the test presses on,
read through actions it defines, and the game's components registered. A system's tests add its components and their
documented dependencies, without sandbox components or assets. The sandbox's tests in `tests/sandbox/` go on testing the
playground, framework and all.

## Taking it out

A game takes the framework, or some of its systems, by copying `gameplay/` or those folders, with the folders they
need, into its own `src/`, and making `src/` an include directory of its game code as `sandbox/src/CMakeLists.txt`
does. The include check guarantees that nothing else comes with them.


## Performance and validation record

Measurements on 2026-10-26 use the working tree after the gameplay extraction, in Release on Windows, Vulkan
(`vulkan (nvrhi)`), NVIDIA GeForce GTX 1660, 1600x900, vsync and validation off. Each capture starts a fresh process
with `--props 500 --lamps 50 --warmup 240 --frames 1200 --quality 2`; walking adds `--walk 1`. Three pairs were run
serially, without concurrent builds or tests. The historical baseline below is from 2026-10-25 at `0676175`, with the
same recorded GPU, backend and parameters. The baseline has one capture per mode; its run-to-run variance was not
established. See [benchmark procedure](sandbox.md#play-test-checklist) for the setup and report semantics.

All durations in this table are milliseconds; fixed/update are inclusive `Components::OnFixedUpdate` and
`Components::OnUpdate` CPU scopes, and memory is resident MiB.

| Capture | Mean | Median | p95 | Worst | Fixed | Update | Interface | Memory |
|---|---|---|---|---|---|---|---|---|
| Baseline orbit | 18.352 | 17.955 | 20.916 | 28.561 | 4.346 | 0.069 | 1.646 | 419.1 |
| Baseline walk | 16.133 | 16.055 | 17.185 | 18.988 | 3.627 | 0.062 | 1.510 | 399.5 |
| Orbit 1 | 17.940 | 17.864 | 19.638 | 23.762 | 4.145 | 0.101 | 1.933 | 389.8 |
| Orbit 2 | 17.907 | 17.760 | 19.540 | 24.191 | 4.032 | 0.099 | 1.933 | 356.6 |
| Orbit 3 | 18.067 | 17.942 | 19.708 | 21.907 | 4.211 | 0.100 | 1.934 | 391.1 |
| Walk 1 | 16.775 | 16.718 | 17.885 | 22.568 | 3.833 | 0.106 | 1.941 | 381.4 |
| Walk 2 | 16.927 | 16.848 | 18.110 | 22.939 | 3.829 | 0.101 | 1.923 | 358.0 |
| Walk 3 | 17.098 | 16.989 | 18.427 | 19.986 | 3.866 | 0.100 | 1.933 | 370.2 |

The median of the three mean frame times is **17.940 ms orbit (-2.2%)** and **16.927 ms walking (+4.9%)** against
the historical means. Walking's increase repeats across all three captures (16.775â€“17.098 ms), so this record does
not classify it as noise or claim performance acceptance. The walking Interface scope's median is 1.933 ms against
1.510 ms, and the component fixed-update median is 3.833 ms against 3.627 ms. These are measured differences, not an
attribution of the slowdown; a contemporaneous baseline comparison and further profiling remain necessary before
claiming equivalent performance. All six current captures had 1,011 bodies, 3,600 script update calls and no dropped
profiler samples. Raw reports are local build artifacts, `build/phase5-orbit*.log` and `build/phase5-walk*.log`.

The extraction was also compiled and linked as an independent Debug game module using only copied `core`, `hud`,
`vehicles`, `persistence` and `effects` folders, the engine's CMake package and a minimal `SPOT_GAME_MODULE` entry point.
That build included no sandbox headers or assets. It demonstrates taking both an engine-only folder and a folder
with dependencies; the isolated system tests remain the executable specifications of their behavior. The final
`sandbox`, `spot_sandbox_tests` and `spot_sandbox_gpu_tests` Debug targets and the Release benchmark also compiled
without compiler warnings.

The migration scripts and temporary framework plan were removed after their data migrations and recipes were
preserved here. At that stage, behavioral tests and commits were deferred at the user's request. The follow-up
validation and performance investigation below supersede that deferred status.

### Performance follow-up

The follow-up reduces broad component queries from scanning every matching type for each result to a heap merge
when more than eight concrete types match. Small queries keep the existing linear merge. The UI draw pass now
checks the drawing interface before searching ancestors for a Canvas, and HUD collection uses one flat frame-local
list rather than allocating a list for every entity. Tests cover query ordering while callbacks remove components
and add new types.

Headless render asset requests now skip mesh preparation and font rasterization when no graphics device exists.
The requests still report the missing device and preserve their asset ids; CPU mesh loading remains available.
A representative sandbox menu case fell from about 25 seconds to 1.7 seconds, and the complete mouse/movement
case from 94 seconds to 6.8 seconds. These timings describe headless startup, not GPU frame rendering.

The investigation also found two benchmark confounders: additional authored vehicles changed the workload, and
window focus or visibility could stop the scripted walk or rendering. Captures now reject a measured frame without
rendering, reassert scripted walking input after focus loss, and accept --vehicles 1 to retain the original
vehicle population. Earlier total-frame comparisons do not establish a framework regression or performance parity.

Behavioral validation corrected input delivery to movement and vehicles before fixed steps, isolated failures to
the affected controller, preserved unloaded mesh references in headless placement previews, and separated the
spawn and pause menu hosts so settings callbacks are registered once. Regression tests cover these behaviors.


The visual sandbox fixtures disable authored cameras after initialization: possession enables the player's view
as components start. The authored-menu rendering test lives with the sandbox's registered gameplay and input
actions, and still renders before the first scene update. After these fixture changes, all eight Vulkan sandbox
GPU cases passed, including scenery, vehicle paint, effects, HUD, menus and held items. The captures were inspected.

The full Release Vulkan GPU run executed 157 cases. Four general rendering checks still fail after the sandbox
fixture corrections: the alpha/shadow, glTF-model and PBR-sphere golden comparisons, and the dielectric/metal
highlight check (metal blue channel 142 against a limit of 127). The same four failures reproduce with SDL_GPU.
No shader or backend changed, and neither golden references nor their tolerances were updated to conceal these
differences. This validation therefore does not claim a completely passing GPU suite.


Validation after the follow-up:

- The complete Release headless preset passed all **1,027** CTest cases in 139.81 seconds.
- The complete Debug and Release builds succeeded without compiler warnings. Debug checks passed **151** scene/asset
  cases (2,927 assertions), **22** UI cases (174 assertions) and **54** isolated gameplay cases (4,389 assertions).
  A redundant full Debug integration rerun was stopped after nine passing cases; it is not reported as complete.
- The Release sandbox executable passed --smoke-test, including menu selection and placement confirmation, firing,
  checkpoint restore, cleanup and ragdoll ownership. The smoke driver now confirms the preview it selects.
- The eight affected Vulkan sandbox GPU cases passed after correcting the fixtures. The four general rendering
  failures listed above remain; both available backends reproduce them.


Final captures on 2026-10-27 use the same Release/Vulkan hardware and parameters listed above, with `--vehicles 1`,
rendered-frame validation and persistent scripted walking. Three fresh-process pairs ran serially without concurrent
builds or tests. All six captured 1,011 bodies and no dropped profiler samples. Raw logs are
`build/perf-final-orbit-*.log` and `build/perf-final-walk-*.log`. Durations below are milliseconds.

| Capture | Mean | Median | p95 | Worst | Fixed | Update | Interface | Memory MiB |
|---|---|---|---|---|---|---|---|---|
| Orbit 1 | 17.294 | 17.198 | 18.860 | 20.969 | 3.924 | 0.097 | 0.674 | 397.1 |
| Walk 1 | 15.961 | 15.866 | 17.183 | 20.184 | 3.625 | 0.095 | 0.675 | 363.6 |
| Orbit 2 | 16.919 | 16.823 | 18.503 | 22.290 | 3.837 | 0.095 | 0.671 | 396.4 |
| Walk 2 | 16.016 | 15.926 | 17.219 | 20.363 | 3.601 | 0.093 | 0.672 | 362.9 |
| Orbit 3 | 16.928 | 16.842 | 18.427 | 20.141 | 3.781 | 0.092 | 0.666 | 409.7 |
| Walk 3 | 16.011 | 15.941 | 17.134 | 18.374 | 3.600 | 0.094 | 0.660 | 385.4 |

The median of final mean frame times is **16.928 ms orbit** and **16.011 ms walking**. The
Interface scope is **0.671 ms** across these captures, about **65.6% below** the contemporaneous
pre-optimization walking capture (1.952 ms). This is a CPU-scope improvement; it is not a claim that total frame time
improved by that percentage. Final walking means span 15.961–16.016 ms and do not repeat the earlier 16.775–17.098 ms
results. The historical baseline did not have the final route/render validation, so its comparison still cannot
isolate the framework's total cost.

`WindSway` uses the generated oak's branch skeleton: each limb and twig rotates around its attachment,
following its parent while adding its own gust frequency, phase and twist. Foliage follows the branch it grows on.
The trunk stays fixed. Rotor wash is amplified near the helicopter; smaller branches flex more strongly.
Only trees receiving wind or settling use skinned rendering, with padded bounds for the moving crown.
Calm trees use their original static renderers. The authored skeleton is shared; each tree owns its pose.

Rotor-driven branch flexion and twist now use 40% of their original amplitude. Forest pines are individual
instances of three sparse, articulated models, preserving the generated placements and original trunk colliders.
Their main branches, side shoots and leaders have independent joints; needles follow their supporting branches.
The old merged forest bark and needles renderers are disabled; the rock renderer remains active.
