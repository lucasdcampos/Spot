# Sandbox

`sandbox/Sandbox.spot` is the engine's playable integration project. Run `./run.ps1` on Windows or `./run.sh`
on Linux and macOS. Both scripts use Release by default; add `editor` to open it in the editor, or `debug`
for a Debug build (for example, `./run.ps1 editor debug`). The build cooks
the project's assets beside its executable. Headless tests in `tests/sandbox/` load the real playground and
exercise gameplay through simulated input. The gameplay that other games can take, whole or in parts, goes in
`sandbox/src/gameplay/`, apart from the sandbox's own code: see [Sandbox gameplay framework](sandbox_gameplay.md).

Add `no-build` to launch the last build without configuring, compiling or cooking assets:
`./run.ps1 no-build` for the sandbox or `./run.ps1 editor no-build` for the editor. The same arguments work
with `./run.sh`. Run without `no-build` after changing code or sandbox assets to update the executable's files.

## Main menu and maps

The startup scene is `assets/scenes/main_menu.scene`. Its `MainMenu` extends the gameplay `Menu` with Home,
Play, Settings and Quit pages, all plain text in a column at the left over a full-screen view of the selected map.
A shade over that view darkens it per page: lightest on Play, where the map is the subject, nearly opaque under the
settings. Controls have no boxes: the focused one, or the one under the pointer, brightens and steps aside, and a
short accent bar marks it; in the map list the bar marks the selected map instead (`MainMenu::StyleText`).

Home offers Play, Settings and Quit. Play lists the authored `maps`, each with its scene, name, description, details
and preview camera position/target. Selecting one changes the view behind the menu and its details; Start
instantiates that scene and queues its replacement through `Application::LoadScene()`. A failed load displays an
error and leaves the menu available. Adding a map means adding its text button and entry in the scene; the
selection is kept while moving between pages, and Home shows it too.

Previews use a separate `SceneRenderer` through the public BeforeOpaque render hook. Each selected map is instantiated
once for its preview with canvases and player cameras disabled and an authored overview camera added. This scene is
never updated, so its gameplay, audio, input and physics do not start. The 1600x900 texture is cached for the menu's
lifetime. No source screenshot or generated asset is required.

The same `SettingsMenu` and `BindingsMenu` parts as pause provide sensitivity, FOV, volumes, graphics quality and
rebinding, using the same per-user `interface.json`. Leaving Settings flushes changes. The modal canvas supports
mouse, keyboard and gamepad navigation; Escape/East/Start returns to Home, or opens a quit confirmation from Home.
Escape while rebinding cancels only the rebind. Each page restores a useful focus, and quitting first focuses
Cancel. The menu releases the cursor and never pauses simulation time.

Playground remains `assets/scenes/playground.scene`. Empty Map (`assets/scenes/empty.scene`) keeps the same player,
inventory, tools, effects and HUD, with a single 200 by 200 meter collidable floor, a sky and a directional light.
Its `materials/debug_grid.material` uses the built-in debug grid at one meter per tile. `GameControls::spawnNpcs`
is false there, so the fallback NPC population does not fill the empty map. Its restart scene points to itself
and its checkpoint is `empty_world.json`, separate from the playground's checkpoint.

Both maps offer Main Menu in the pause menu. Returning closes the pause menu before replacing the scene, restoring
simulation time; the startup menu releases the cursor again. The executable's `--smoke-test` exercises this route
through the empty map before checking the playground. Visual acceptance tests and benchmarks load Playground
explicitly so their coverage stays independent of the startup scene.

## The playground

The playground is a valley of yards around a central plaza, built to show what the engine draws and simulates. Every
piece is an ordinary scene entity or a generated model, editable in the hierarchy; the level and its permanent lamps
remain outside world checkpoints and cleanup.

- **Plaza** (spawn): paved, with benches, planted oaks, street lamps, the build pad and a monument whose neon
  "SPOT ENGINE" sign glows through bloom under a mirror sphere and gold, copper, plastic and glass samples, for PBR
  and reflections. The NPCs patrol here, and a red hatchback is parked by the spawn (see [Driving](#driving)).
- **Workshop** (north-east): a brick hangar with a ten-meter door, glass windows, four shadowed ceiling lamps, a
  workbench, shelving, the lamp pad and a neon sign. Steel stairs up the west wall lead onto the parapeted roof.
- **Proving yard** (west): a quarter pipe, a jump ramp, a platform with steps, a nine-meter drop tower with
  switchback stairs over a bouncy pad, a wrecking ball on a rope beside a wall of crates, a seesaw on a hinge and a
  curve of dominoes.
- **Firing range** (east): a covered booth with a counter, lanes between concrete barriers, target dummies at ten
  and twenty meters, steel plates on hinges that swing when hit, explosive barrels and a berm.
- **Container yard** (south-west, across the road): stacked shipping containers in four colors behind a cutout
  chain-link fence, with steps onto one of them.
- **Pond** (south-east): transparent water in a hollow of the terrain, with a dock and rocks. It is `Water`:
  shots, things falling in and blasts splash it (see [Particle effects](#particle-effects)).
- Around them, the generated terrain rises into hills with rock on its steep slopes, scattered with pines,
  oaks and boulders. A road with markings crosses the valley under street lamps.

Street lamps, the booth and the monument's uplights carry `NightLight`, which lights them as the day cycle's
daylight fades (`DayCycle::GetDaylight()`). The workshop's lamps stay on. The day lasts eight minutes; hold T to
advance it quickly. Clouds drift over the valley (a `Clouds` component on the Sky entity, set in the scene), and at
night stars come out and turn with the sun's path (`DayCycle` sets `Sky::SetStarRotation()`), with a moon opposite the
sun; see [Clouds, stars and the moon](sky.md). Three tagged pads mark where to build, test lamps and test weapons.

The level's surfaces are generated PBR texture sets (grass, gravel, asphalt, paving, concrete slabs, brick,
corrugated steel, tread plate, rock, bark, leaves, needles and water), used by the materials in `materials/level/`,
most projected from the world (`worldTextureSize`) so they keep their scale on boxes of any size, their normal maps
included. Most are painted pixel by pixel from noise and cells (`generator/level_textures.cpp`); the grass, leaves
and needles are painted stroke by stroke (`generator/foliage_textures.cpp`): the lawn from tens of thousands of
blades in tufts over straw and soil, with rosettes of broadleaf weeds, clover and a few flowers, repeating every four
meters, and the foliage as a twig of oak leaves and a spray
of spruce needles cut out by their alpha, imported with `keepAlphaCoverage` so they do not thin out from afar. Its
models (`generator/level_models.cpp`) are the terrain, rocks, trees, the container, the street lamp, the barrier, the
bench, the quarter pipe and the fence panel. The terrain also contains sparse ground-cover meshes: curved,
tapered grass blades in two greens and dry straw, small stones and fallen twigs. Fixed seeds and broad noise patches
leave open ground between clusters. Placement excludes the road, paved yards, workshop, central plaza, spawn path
and pond; blade roots follow the terrain's actual triangulated surface. These extra meshes are renderer-only children
of Ground in the playground, using the model's imported materials; only the original grass and rock surfaces collide.
Each of the three grass renderers has `GrassVisibility`: when the game has a graphics device, it partitions the
original triangles into eight-meter cells once and creates a renderer for each occupied cell. Only cells whose
bounds reach within `drawDistance` (25 meters by default) of its referenced player are active, with two meters of
hysteresis at the boundary. With no player reference it follows the main camera. Distant cells submit no color,
depth or shadow draws; the ground's grass texture remains visible. The original renderer stays available for
authoring and is restored if the component is disabled. Generated chunk entities keep no persistent ids or physics.
The trees are grown by `generator/trees.cpp`: branches as tapering tubes
that split into smaller ones, with buttress roots at the trunk's foot, and foliage as double-sided cards whose normals
point out of the crown, so it is lit as a whole. The sprays bend along their length, oaks carry fuller clusters, and
pines have alternating side shoots and irregular branch lengths. Leaf and needle PBR maps are 1024 pixels wide,
with mottled leaf blades, worn edges, fresh needle tips and varied roughness. Bark uses 2048-pixel PBR maps with
longitudinal fissures, fine fibers and pores, weathered grey-brown ridges, sparse lichen and healed branch scars.
Its relief comes from the normal map, with restrained color contrast and roughness varying between worn surfaces
and crevices. Branch UVs have separate offsets and repeat every two meters to reduce recognizable repetition.

`LevelModels::GetForestTrees()` places 180 trees around the hills using fixed seeds, varied scales, a minimum
distance between trunks and a slope limit. The paved playground remains clear. The forest still merges into three
material meshes for its 104 pines and boulders; the remaining 76 placements are `Forest Oak` entities in the scene,
sharing the existing oak bark and leaf mesh assets. Species selection uses a separate seed so it preserves spacing
and placement. `Forest Bark` in `playground.scene` has two narrow capsule colliders per pine trunk, tapering from the
lower to the upper section, instead of colliding with the rendered branches. Needles and branches have no collision;
each oak has its own short trunk capsule. `Forest Rock` retains its triangle collider. The capsule centers and
sizes and the oak entities are authored from these placements, so changes to the placement rules, species selection,
terrain height or trunk dimensions must update the scene together.
Rebuilding the sandbox generator regenerates the models and textures; no asset ids need to change.

GPU acceptance captures of every yard by day, at sunset and at night,
and of the grass and trees up close, are written to the test build's `tests/sandbox_captures/` directory.

## Spawnable prefabs

The playground's props, lamps and firearms are linked prefab instances. Their authored positions, collider
choices, masses, colors and weapon settings remain instance overrides. Editing a source updates inherited
values in every instance; local changes remain. The editor also updates open documents when a source is saved,
keeping selection and undo history. See [Prefabs](prefabs.md) for Apply, Revert, variants and nesting.

`PropSpawner` queries the imported asset manifest for prefabs under its `folder`, normally `prefabs/spawnables`.
Only prefabs with a `Spawnable` component on their root enter the catalog. Set its category (Prop, Lamp or
Weapon), order and placement clearance, then author the object's renderers, physics and gameplay components.
Entries sort by order, name and asset id. The first eight preserve the existing F1–F8 shortcuts; the Spawn
action cycles through the entire catalog, including ramps, stairs, lamps and firearms. There is no source
directory scan at runtime, so discovery works in exported games too.

To add an entry, make an entity tree in the editor, add `Spawnable` to its root and drag the root into a folder
under `prefabs/spawnables`. Save or cook the project and restart Play to rebuild the catalog. A marked variant
works the same way. The `.meta` file generated by the asset database must be committed with the prefab.

The tool gun uses point and spot lamp prefabs under `prefabs/lamps`; it sets color, shadows and placement for
each instance. Firearm prefabs contain their world model hierarchy and physics. The player's held instances
remove those world components as overrides, while `ViewModel` draws the model referenced by their `HeldItem`.

## Controls

The Props tab includes Target Dummy after the original eight shortcuts, ramps and stairs. Its
generated glTF model and bullseye texture use the wood PBR maps; four compound boxes follow its
base, post, torso and head. Hits push it and emit wood effects. It uses the same spawning, undo,
cleanup and checkpoint paths as other props. The level's original primitive target remains editable.

Bindings live in the project's Input settings and can be changed in the editor. The default keyboard and mouse
controls are:

| Control | Action |
|---|---|
| WASD, mouse | Move and look |
| Shift, Ctrl, Alt | Run forward, crouch, toggle noclip |
| Space | Jump |
| 2–5 | Select a firearm |
| 1, 6 | Select the physics gun or tool gun |
| Hold Tab, mouse | Weapon wheel: point at a slot, release to take it |
| Left mouse, right mouse | Fire/use the tool, aim or turn a held prop |
| R | Reload; cycle the tool gun's mode |
| E | Carry or drop a nearby light prop; get into the car looked at, or out of it |
| F | Toggle the flashlight |
| F1–F8 | Spawn crate, barrel, explosive barrel, ball, plank, beam, cone or wheel |
| Q | Open the spawn menu |
| Z | Undo the last spawn or tool operation |
| X | Remove props created by the spawner |
| F9, F10 | Save and load the editable world checkpoint |
| F11 | Show or hide frame statistics |
| Grave (`) | Open or close the developer console |

The Player entity is the gameplay framework's `Character` (see [the gameplay framework](sandbox_gameplay.md)), possessed by
the `PlayerController` on the Player Controller entity, through which every part of the player reads its input. Its
`FirstPersonView.head` reference points to its Head child with the active camera, which is also the pawn's `eye` and
`viewCamera`; `Flashlight.light` points to the light below that head. Mouse look turns the head, walking uses that view's
yaw, and noclip also follows its pitch. Keep these references assigned in the scene so movement, aiming and the
rendered view agree. Use goes through the pawn's `Interactor`: the car's `VehicleSeat` and light props (`Carrier`)
are what it can use. `GameMode` owns starting positions and respawn, and `SessionControls` owns cursor capture,
Pause, Restart and the quick-save shortcuts. `GameControls` keeps the sandbox's console rules, NPC population and
developer tools font. See [session and game rules](sandbox_gameplay.md#session-and-game-rules).

Gamepad bindings are in the same settings. Spawn cycles through the catalog rather than requiring a separate
button per entry. The tool gun's right mouse/Aim action cycles colors, materials or lamp kinds in those modes.

## Driving

`prefabs/spawnables/props/helicopter.prefab` places an orange two-seat helicopter on the clear grass west of the
workshop, at (-9, 0.03, -38), facing the plaza (+Z). It is also available under Props. Its generated model has a
rounded glazed cockpit with seats and controls, skid landing gear, a tapered tail boom, stabilizers, navigation
lights and independent four-blade main and tail rotors with yellow tips. The generator also synthesizes its rotor
loop; model and sound metadata come from the normal asset import.

Use (E) enters and exits through the existing `VehicleSeat`. W/S pitches forward/backward, A/D turns, Space climbs
and Left Ctrl descends. On a gamepad RT climbs and LT descends proportionally; the left stick pitches and banks,
and LB/RB turn left/right around the vertical axis. Aircraft use separate flight actions so walking and driving
keep their own bindings. Both stick axes have a 0.2 dead zone, and diagonal tilt stays within the maximum bank angle.
The rotor takes three seconds to reach flight speed. Releasing the controls levels the body and holds height while
horizontal drag slows it. The chase camera, HUD speed/climb/rotor readings, impact components, spawn catalog and
checkpoint persistence use the same vehicle framework as the cars. `Helicopter` derives `Gameplay::Vehicle`; new
vehicles must use this base rather than duplicate its shared behavior. Exiting in flight shuts the engine down;
the body remains under physics and falls as its rotors coast down.

Its `HelicopterDownwash` component sends the main rotor's wind toward water along the rotor axis. Within 18 meters,
it emits expanding surface rings every 0.2 seconds, with opacity fading with squared rotor speed and distance.
Tilting the helicopter moves the wind's footprint. The waves continue briefly while the rotor coasts down and
fade on their own when it stops or moves away. They reuse the water's ripple layer without impact spray or audio.
Nearby oaks and forest pines also react to rotor wash (`WindSway`): the oak's 156 articulated branches, and each
pine's whorl branches, side shoots and leader, flex and twist independently, with faster, stronger motion in the
smaller twigs. Leaves and needles follow their branches; trunks and their collision stay fixed. Each generated
branch skeleton is shared, while each affected tree computes its own turbulent pose.

`prefabs/spawnables/props/coupe.prefab` adds a blue two-seat coupe, parked on the workshop's light pad, facing the
door, and available in the spawn menu's Props. Its generated model has a lower roof, a sloping rear window, a rear
lip and its own paint, lamps and trim; it shares the hatchback's wheel geometry and the generator's shape builder
and wheel-arch helper. Its prefab authors the colliders, 1,180 kg mass, wider track, suspension, tire grip and
six-speed rear-wheel drivetrain. `WheeledVehicle`, `VehicleWheel`, `VehicleSeat`, `ChaseCamera`, vehicle sounds,
impact effects and persistence are the same components as the hatchback's; no runtime code selects a car by name.
The scene holds a prefab instance at (21, 0.07, -37), with its placement in the scene rather than game code.

`prefabs/spawnables/props/car.prefab` is a car, parked in the plaza and in the spawn menu's Props. Use (E) on it gets
in: its `VehicleSeat` seats the character, whose parts follow its "Seated" state, and transfers the player's controller
to the `WheeledVehicle`. The car's own camera, a `ChaseCamera` on its Camera child, takes over in the third
person: it swings around behind the car as it turns, Look orbits it, and it comes back behind a moving car a moment
after the mouse rests, keeping out of walls and widening at speed. Accelerate (W / right trigger) drives forward;
Brake (S / left trigger) brakes and, once stopped, reverses. Move steers with A/D or the left stick. Jump pulls the
handbrake, and Use gets out beside the driver's door, or wherever around the car
there is room, looking the way the car faces. Dying or loading a checkpoint gets the player out first. The HUD shows the
speed and gear in place of what is in hand.

Car collisions at a closing speed of at least 2 m/s, with the car itself moving toward the target at least that
fast, ignite explosive barrels, knock NPCs into ragdoll with the impact impulse and deal 10 damage per m/s to
players other than the driver. Walking or sprinting into a parked car does not deal damage.

Both cars and the helicopter have `VehicleHealth`: 300 health, without regeneration. Shots, blasts and
collisions above 8 m/s accumulate damage. At zero health the vehicle explodes once, damaging and pushing its
surroundings, ejects its driver and becomes unusable. Its own explosion has a 12-meter radius and deals up to
600 damage, enough to kill nearby players and the ejected driver. The fireball renders at 2.4 times its base scale.
An off-center impulse launches and turns the wreck, preserving its existing motion; wrecks release the parking
brakes, and destroyed helicopters lose flight stabilization and lift. Its body, windows, lights and wheels receive
`materials/vehicle_burned.material`, a dark, rough soot finish. The wreck keeps its physics body; checkpoints
preserve both partial damage and destroyed vehicles without replaying the explosion.

`WheeledVehicle` simulates a car on its Rigidbody with the public physics API alone. Each physics step, each wheel is cast down
from the top of its travel (a sphere as wide as the tire, at its bottom) and holds the body up with a spring, preloaded
so the car rests where its wheels are placed, a damper, a bump stop and an anti-roll bar per axle. The tires grip by how
far they slip, along (slip ratio) and across (slip angle), combined within their friction circle on Pacejka's curve:
rising steeply, peaking at `peakSlipRatio` and `peakSlipAngle` and falling as they slide, with less grip per newton as
they are loaded more (`loadSensitivity`). Their forces reach the body at `rollCenterHeight`, as a suspension's links
take them, and push back on dynamic bodies they stand on. The wheels' spin is stepped four times a physics step and
implicitly, since the road couples it stiffly; forces never exceed what stops a tire sliding within the step, so a
stopped car neither jitters nor creeps. An engine with a torque curve and engine braking turns the driven wheels
(`VehicleWheel::driveShare`) through a clutch that slips at launch, an automatic gearbox shifting by revolutions and
throttle, a final drive and an open differential, its inertia added to theirs. Brakes, biased to the front and with
anti-lock, and the handbrake on the rear wheels stop them; the air drags as the square of the speed. The front wheels
steer with Ackermann geometry, less at speed. Parked, its brakes hold it and it sleeps once still. Every value is a
field of the vehicle, its `Drivetrain` or each `VehicleWheel`, in SI units. `VehicleEngineSound` pitches the engine's
loop to the revolutions and `VehicleTireSound` makes the tires squeal past their
peak, both generated (`generator/sounds.cpp`), as is the model (`generator/vehicles.cpp`): an extruded body with
rounded wheel arches, glass, trim, interior, lights and a wheel that the car's four wheel entities show, steered and
spun.

`tests/sandbox/car_tests.cpp` drives the playground's car with simulated input: resting at its ride height, getting in
and out with the cameras switching, accelerating through the gears, braking, reversing, steering and the handbrake.
The GPU test `The cooked playground's car shows its paint, glass, lights and wheels` captures it from the front, the
side and behind into `tests/sandbox_captures/car_*.png`.

## Luau script demonstrations

The authored playground runs three imported Luau scripts from `assets/scripts/`, each a module with `OnStart` /
`OnUpdate` hooks that keeps its state in locals and reaches the engine through `require("spot")`.
They are already assigned to enabled `ScriptComponent`s in the scene; press Play in the editor or run the
sandbox normally. Built with `SPOT_ENABLE_LUAU=OFF`, the scene keeps its ordinary C++ gameplay and static scenery.

| Script / entity | Behavior | Control |
| --- | --- | --- |
| `monument_sign.luau` / Monument Sign | The plaza's SPOT ENGINE neon gently floats and changes scale; the solid plinth stays still. The script also accesses its Neon child's `MeshRenderer`. | **N** switches the neon mesh off/on. |
| `music_fade.luau` / Music | The existing looping soundtrack fades to its authored volume over 1.5 seconds. Muting fades to silence while playback continues, preserving the song's position. | **M** fades music out/in. |
| `day_controls.luau` / Game | Finds the Sun entity and calls the Sandbox's C++ `DayCycle` through generic descriptors. Sky, sunlight, fog, exposure and automatic street lamps respond to the new time. | **J** morning, **K** noon, **L** night; **P** pauses/resumes the day. |

These demo keys are physical key queries in the scripts, separate from the configurable gameplay action bindings
above. Input capture by menus/console suppresses them. Animation and fades use `deltaTime`, and the controls do
nothing while simulation time is paused. The day pause uses `DayCycle.dayLength = 0`, keeping the component active
so night lamps can still read its daylight level.

`src/game.cpp` registers the game's `DayCycle` script API after component registration, on every module load:
read-only `timeOfDay` and `daylight`, writable `dayLength`, and `SetTimeOfDay(time)` for a value in [0, 1). The method
applies the sun and atmosphere immediately. This uses the same descriptors as engine components; it requires no
special case in the Luau adapter. See [scripting](scripting.md#native-game-libraries) to expose other components.

`tests/sandbox/luau_tests.cpp` drives these exact scene assets through simulated input, including music playback,
menu input capture, neon visibility and night lamps. The targeted graphical test
`Luau scripts change the rendered monument and daylight in the cooked sandbox` captures the monument by day,
with its neon off, and at night under `build/<preset>/tests/sandbox_captures/luau_monument_*.png`.

## World checkpoints and weapon feel

The physics gun grabs the surface point in the prop's local coordinates, including its world scale;
scaled prefab instances therefore stay at the grabbed point without jumping. Disabling either the
physics gun or the carrier releases its prop, and disabling the gun stops its hum. Checkpoint loads
also release both tools before replacing editable entities. Permanent workshop geometry and lights
survive save/load and cleanup, including when a checkpoint is loaded while the pause menu is open.

F9 writes `world.json` in the application's user data directory. F10 validates and restores the editable root
props and their children: transforms, body velocities, frozen tags and kinematic state, welds, ropes, painted
materials, lamps, ammo and any remaining reload, cooldown or explosive fuse. References between saved props are
remapped; references to the unchanged level remain valid. Spawned props stay owned by cleanup after loading.
NPC populations, patrol state and ragdoll bodies are saved together, including their ownership references.
The player, level, ambience and day cycle keep running. Loading releases carried props and clears undo history.
Transient projectiles, flashes and held view models are excluded. Missing or malformed checkpoints leave the
current world intact. Checkpoints expand prefab instances so later prefab edits do not rewrite a saved world.

Weapon intervals, reloads, explosive fuses, projectile lifetimes and impact quiet periods use component-owned
timers. Aiming, switching and recoil use owner-bound tweens, and flashes fade on their own updates. All advance with scaled simulation time and
pause at zero time scale. The HUD samples unscaled time. Destroying an owner cancels its work.

Each Firearm prefab exposes `cameraRecoil` (radians), `viewRecoil` (meters) and `recoilRecovery`
(seconds). The rifle recovers quickly; the shotgun and launcher kick harder. Aiming reduces recoil
and spread, and is not possible while reloading. Camera recoil returns to the player's underlying look without
undoing mouse movement.

### Held items

`ViewModel` draws only the item in hand and moves it procedurally; there is no arm rig. Everything is computed from
state each frame, so it pauses, saves and reloads with the game: the item lags behind turning (`swayAmount`,
`maxSway`), breathes, bobs along a figure of eight with the walk cycle (`bobAmount`), kicks back and up on each shot
and recovers, jolts on landing, comes up from below when switched to (`switchTime`), rolls aside while reloading
(following `HeldItem::DescribePose()`) and turns down and aside when a wall in front would swallow it. Aiming
moves it so its sights line up with the eye and steadies all of that motion.

Models describe how they are held with mesh-less nodes, which the generator writes as markers: `Muzzle`, where shots
and the physics gun's beam leave; `Eye`, the point aiming puts at the eye, in line with the sights; and optionally
`Hip`, the point held at `offset` from the eyes (the grip, the model's origin, when absent). Parts move on their own
by name: a `Slide` cycles as it fires and locks back when empty, a `Pump` is worked after each shot, a `Magazine`
drops out and is seated again, a `Rocket` disappears when fired and is loaded from the front, and `Prongs` spin,
faster while the physics gun holds something. Any model following these names gets the same behavior.

Shots aim from the eyes, where the crosshair is, so aiming is exact. Hitscan pellets draw a tracer, a stretched
particle flying from the muzzle to the impact; rockets leave the muzzle toward the crosshair's target unless a wall
is closer than the muzzle.

### Footsteps and head bob

`CharacterMovement` keeps a walk cycle (`GetStepPhase()`), advanced by the distance walked on the ground. A footstep
lands at each whole step: `strideLength` (meters at walking speed) lengthens with the square root of the speed, so
walking takes a little over two steps a second, running about three and crouching fewer. Steps pick a different
variant each time, never the same twice in a row, with small random changes of pitch and level; landing from a fall
plays a louder one and dips the head on a damped spring. The head dips `bobHeight` as each foot lands, scaled by
`GetBobWeight()`, which eases in and out with speed. The held item bobs from the same cycle, so steps, head and
hands stay in time. The generator synthesizes four variants per surface: a soft heel thud, the scuff of the sole and
the toe a few hundredths of a second later, with grit on concrete, hollow boards on wood and a ring on metal.

The authored crosshair has a dark outline for bright surfaces. Fresh users start with music at 30%,
ambience at 50% and master at 90%; saved personal volumes remain authoritative.

## Developer commands

Open the console with Grave; the sandbox permits cheats while it runs (`GameControls::cheats`), so its commands work
at once, and `console.cheats false` forbids them again. Registrations belong to
the active scene's GameControls and disappear when it is disabled, removed or unloaded. Commands resolve the
current player components through `Players` on execution, following the occupant while driving, so world checkpoint
loads do not leave stale entity pointers.

| Command | Effect |
|---|---|
| `gravity [Y]` | Query/set scene gravity on Y, preserving X/Z; accepts -1000..1000 |
| `timescale [value]` | Query/set simulation speed, 0..100; zero pauses |
| `noclip [true/false]` | Toggle flying through collisions, or set it explicitly |
| `thirdperson [true/false]` | Toggle the player inspection camera; 1/0 also work |
| `firstperson` | Return to the first-person camera |
| `cleanup` | Remove spawned props and their undo entries |
| `spawn <prop>` | Spawn a catalog item where the player aims; no arguments lists names |
| `give <weapon>` | Equip/refill an owned firearm or add a missing one to the player's inventory |
| `stress <count>` | Spawn 1..2000 crates in a pile in front of the player |

Item names are case-insensitive; spaces, underscores and hyphens are interchangeable. Multiword names work with
or without quotes, for example `give "Rocket Launcher"`. Giving a weapon removes its world rendering/colliders
before parenting it to the player; the held view model renders it, and repeated `give` commands refill ammunition
and cancel cooldown/reload without creating duplicates.

For profiling, enable `stats true`, run `stress 500`, inspect a slow frame in **Window > Profiler**, then run
`cleanup` and compare bodies/frame times. Select **Game process** for a separately launched game. `render.enabled
false`, `physics.debug true` and `timescale 0` help isolate drawing, shapes and simulation. See
[Developer tools](developer_tools.md) for profiler selection, scope averages and capture commands. The automated
stress regression uses sixteen props headlessly; larger counts are explicit interactive measurements.

## Play-test checklist

Build `spot_sandbox_benchmark` in Release to measure the complete cooked playground rather than
isolated rendering. Run it with `--props 500 --lamps 50 --warmup 240 --frames 1200 --quality 2`.
Quality is 0/1/2 for Low/Medium/High. A camera orbits the level at 1600x900 with vsync and validation
off; gameplay, physics, NPCs, particles and the HUD continue. It spawns actual imported crates,
adds point lamps (every eighth shadowed), excludes loading/warmup, then reports frame mean, median,
p95, worst, body count, memory and inclusive CPU scopes, then the mean of each profiler counter per measured
frame, draw calls and the GPU's timers (`GPU Opaque (ms)` and the like) among them. Exit status is nonzero if setup
fails.
The report also records the startup scene's synchronous `InstantiateScene` time (including asset reads,
decoding and entity/component creation, excluding window/device setup, scene activation and first updates),
each crate's `PropSpawner::Spawn` time (including its placement raycast, prefab instantiation and undo entry,
excluding the benchmark's grid placement), and the sum of script `.OnUpdate` scopes per measured frame.
The first spawn is printed separately because it may load the prefab. Script totals include hook dispatch and
bindings, exclude fixed/start hooks, and print the invocation count and dropped profiler samples so an empty
or incomplete capture is visible. CPU scope averages cover every measured frame, excluding warmup, rather
than just the profiler's 240-frame history. Frame intervals include this collection overhead. Loading uses
an initially uncached asset manager; the operating system's file cache is not flushed.

To reproduce the gameplay framework's Release measurements from the repository root:

```powershell
cmake --build --preset msvc-release --target spot_sandbox_benchmark
./build/msvc/tests/Release/spot_sandbox_benchmark --props 500 --lamps 50 --warmup 240 --frames 1200 --quality 2
```

`--vehicles N` optionally keeps only the first N authored vehicles, in component addition order, before warmup.
The default keeps all vehicles in the scene. Use `--vehicles 1` when comparing with the original one-car baseline;
new vehicles added to the playground otherwise change both physics and rendering work. Compare body counts too.
Keep the benchmark window visible throughout measurement; a frame without rendering rejects the capture instead
of mixing update-only frames into the reported performance. The walking script reasserts its held Forward action
after focus loss, which clears physical keyboard state; changing focus therefore does not stop the scripted route.

Run each capture in a fresh process; keep the cooked assets, backend, quality, resolution and population
fixed when comparing changes. The framework's before/after measurements are recorded in
[its performance record](sandbox_gameplay.md#performance-and-validation-record).

Run without concurrent builds or tests. `--walk 1` instead walks the player through the plaza into the workshop,
seen through its own eyes with its weapon, as a game plays: 150.6 FPS in Release and about 71 FPS in Debug. On an i5-11600K / GTX 1660 the valley playground measures 99.0 FPS average, 11.83 ms p95 and 19.92 ms
worst in High quality with that population, and 148.9 FPS (8.45 ms p95) on its own. HUD texts do not clip
(`overflow` off): each clipped text breaks the 2D batch, which cost the HUD about 3 ms a frame. Individual frames can still exceed the 60 FPS budget.

- Walk, jump, climb ramps and stairs, carry and throw a light prop; check collisions and footsteps.
- Visit every yard: skate the quarter pipe, climb the drop tower and drop props onto the bouncy pad, push the
  wrecking ball into the crates, tip the dominoes, ride the seesaw, ring the range's plates, climb the container.
- Watch dusk fall over the pond: street lamps come on, neon signs glow and the workshop stays lit.
- Fire and reload all four firearms, aim, push props and trigger an explosive barrel's fuse and chain reaction.
- Grab, move, rotate and freeze a prop with the physics gun, then release it.
- Get into the car, drive it around the valley through the gears, brake, reverse, slide it with the handbrake, orbit
  its camera, jump the ramp and get out; check the cameras switch and the engine and tires sound.
- Weld and rope props, paint and change their materials, remove them and undo the operations.
- Place point and spot lamps with and without shadows, change their color, attach one to a moving prop and undo.
- Spawn every catalog entry; verify firearm model trees, lamp lighting, convex cone/wheel collisions and cleanup.
- Override one prefab instance, edit the source and save; verify inherited changes propagate while overrides stay.
- Add a marked prefab or variant asset, cook and restart Play; verify it appears without changing C++.
- Check the day cycle, flashlight, reflections, transparency, post-processing and the audio mix together.
- Save painted, frozen, connected and lit props with F9, edit or remove them, then restore with F10; check cleanup.
- Walk and fire while changing time scale; check bob, recoil, flash fades, reloads and fuses pause together.
- Spawn a stress pile, select/pause a slow frame in the profiler, remove it with `cleanup` and compare body counts.
- Give each firearm twice; check selection, full ammunition, canceled reloads and unchanged inventory size.

For release acceptance, repeat this checklist from the exported folder on each platform. Also check running,
crouching under a low ceiling, noclip/returning to walking and falling off the level; rotate a carried prop and
throw it; rotate and wheel a physics-gun prop, freeze with Aim and unfreeze with U; enter the workshop, collide
with its glass, climb to its roof and advance T through sunset/night. Spawn and shoot Target Dummy, then undo
and clean up. Run `gravity`, `timescale`, `noclip`, `spawn`, `give`, `stress` and `cleanup`; capture a screenshot
and check the file. Play surface impacts and footsteps beside the ambience/music mix. Verify every weapon's
idle/fire/reload/switch/sights poses and recoil recovery, npc patrol/ragdoll/revival, and wood/metal/concrete
particles. Complete the runtime-interface checklist below, including a saved rebind and a restart. Export
instructions, the automated smoke test and the per-platform results are in [Sandbox release](sandbox_release.md).

Watch the NPCs patrol, shoot one and trigger an explosion nearby; check that the
animated pose becomes a physical ragdoll. Fire, reload, aim and switch each weapon;
check that tracers leave the muzzle, that the sights line up with the crosshair and that moving parts cycle.

## Particle effects

Each shot flashes at the muzzle: a star of flame turned at random, tongues of fire stretched along the barrel and a
puff of gun smoke that drifts, swells and thins, with a flickering light of a frame or two. `Firearm.muzzleFlash`
scales all of it per weapon: the pistol's is smallest, the shotgun's biggest. Impacts throw sparks on metal, dust on
concrete, and dust and splinters on wood.

Explosions unfold in layers, as real ones do. A blinding flash lasts a tenth of a second; a fireball of turbulent
flame swells, slows and rises, cooling from white through orange and red into soot, while black smoke billows out of
it and rises for seconds. Embers arc out and fall, cooling as they go, debris chunks tumble out, and when the ground
is close below, a skirt of dust rolls out along it. The light flickers and dies down over most of a second, and the
blast wave shakes the views within eight times its radius (`Gameplay::CameraShake::Add()`), without moving their
aim. Fire and smoke are textured from `generator/effect_textures.cpp`, and their speed and size follow drag and
swelling curves.

The pond's `Water` component (on its "Pond Water" entity) has no collider; it checks rays and bodies against its
surface, the unit square its entity's scale stretches. A shot that reaches it splashes and goes on for `shotDepth`
(a meter) before the water stops it, pushing less on the way; tracers end at the surface. Bodies and players falling
in splash as hard as they hit it, more for heavier ones, and the water drags on what moves in it. Rockets explode on
the surface. Explosions at the water only flash, and throw up a column of white water that rains back down; under it,
they show nothing but the water and sound muffled. Each splash throws droplets, a spout of spray, mist for harder
ones, and rings that lie flat on the water
(`ParticleFacing::Horizontal`) and spread, with the water's own small or large splash.

Smoke, dust, spray and ripples are lit layers (`ParticleLayer.lit`): particles are not shaded, so the effect pool
dims them by the light the day cycle gives it as they are spawned, and they do not glow at night. Fire, sparks and flashes
give off their own light.

The playground's Game entity owns the `EffectLibrary`, its named effects ("muzzle", "impact.wood", "explosion",
"splash"...), and the Sounds entity the `SurfaceLibrary`, the kinds a `Surface` names; props carry ImpactParticles
separately from their impact audio. Effects reuse a bounded pool and expire without affecting
physics, spawning undo or world checkpoints. See [Particles](particles.md).

Play-test: fire each weapon at wood, metal, the ground and the pond; detonate a barrel, a rocket on the ground and
one on the water; throw a crate into the pond; hold, move and release a prop with the physics gun. Check that the
flash, sparks, dust, fire, smoke, embers, debris and splashes appear and fade, that ripples spread on the water,
that the beam stops emitting on release, and that saving/cleanup keeps no transient effects.

Sandbox headless tests normally read the workspace's authored assets. Set `SPOT_SANDBOX_TEST_ASSETS`
to an absolute directory containing a snapshot of `sandbox/assets` to validate a committed scene
while preserving concurrent editor changes. Imported assets still come from the test build.


## Runtime interface

Q (gamepad Back) opens the spawn menu, laid out as an asset browser: a header with its title, how many objects it
shows, a search field and two views (larger cards, six to a row, or compact ones, eight), a sidebar of categories
(Props, Lamps, Weapons and NPCs, each counting what matches the search; the one shown is raised and marked in the accent,
`SpawnMenu::SelectCategory()`), the grid of cards, and Undo, Clean Up and Close at the bottom, as quiet text
buttons. Selecting a card closes the menu and starts placing that prefab (`PropSpawner::BeginPlacement()`): a
translucent copy of its meshes, with no body or behavior, follows the aim where it would spawn; Fire places it and
Aim cancels. Until both are released, the spawner's input context takes them, so the held item neither fires nor
aims on the same press. The menu marks the card last chosen in the accent; Return in the search takes the first object found. Undo and
Cleanup use the normal gameplay operations. Thumbnails are rendered from the real prefab meshes into cached textures,
two per frame, and stay hidden until drawn. Escape (gamepad Start) opens pause. Resume, Escape or gamepad East/Start
closes it; the previous time scale and requested cursor lock return. Tab/Shift-Tab, arrows, D-pad and left-stick
threshold crossings navigate; Return, Space or South activate. Arrow/D-pad input adjusts sliders; offscreen entries
scroll into view.

Pause stores sensitivity, field of view, volumes, input bindings and Low/Medium/High graphics quality
in the game's per-user `interface.json`. Select any binding row and press its new control; Escape
cancels. Composite directions have individual rows. Graphics changes rebuild the renderer on the
next frame, in both the standalone game and the editor's game view, while keeping rebinds.

The HUD is laid out like a modern shooter's: nothing is boxed, and it keeps to the corners. A small crosshair (a dot
and four lines) opens up while moving, in the air and under recoil, gives way to the sights while aiming, hides while
nothing in hand can be used, and flashes a hit marker when a shot moves a prop or knocks down a npc. At the bottom
right, the silhouette of what is in hand sits beside its rounds, the magazine large and the reserve smaller (`30 /
240`), which turn red when low, or the tool's mode and option, with a thin reload line under them; its name shows
above for a moment after switching. Its controls show there only while they matter: after switching, for the first
`Hud::introTime` seconds, while reloading or low, and while the physics gun holds something. What Use would do, or how
to place what the spawn menu gave, appears under the crosshair. Health is at the bottom left, a small cross, the number
and a thin bar, which turn red below 30%. The clock at the top right is the time and a small sun or moon. Instead of a
permanent list of keys, a line at the bottom left names the key of the controls panel as the game starts; the Help
action (H) shows or hides that panel, its rows made from the action map, so they follow rebinding. Frame rate, body
count and update and render times appear only on the Statistics action (F11). Each part is a HUD element on the
widgets it draws into, shown by the framework's `Hud` on the player's pawn's states (see
[Gameplay framework](sandbox_gameplay.md#hud)); in the car, its gauges take the place of what is in hand. Controls
show with the keys bound to them now.

The interface has one visual language, in `interface.prefab` and `interface_palette.h`: graphite surfaces, slate text
in three strengths and one amber accent (the menus' `style.accent`) for what is selected; Inter for text, Inter
SemiBold for titles and JetBrains Mono for numbers and keys (`assets/fonts`, under the OFL); corners rounded by 4
pixels, 1 pixel outlines and a soft shadow under menus, all generated (`ui_panel`, `ui_outline`, `ui_shadow`), and soft
`ui_scrim` gradients behind the HUD's corners for legibility over a bright sky. Menus ease in over 160 ms, and their
controls' outlines, labels and icons follow hover, focus and selection over about 120 ms
(`Gameplay::Menu::StyleControl()`): the selected one in the accent, the focused one in white. The prefab is laid out
on a 4 pixel grid at the 1280 by 720 reference resolution.

Holding Tab opens the weapon wheel, a GTA-style ring of eight sectors clockwise from the top, one per slot, each with
the item's icon and number key, and slows time to `Inventory::wheelTimeScale`. Moving the mouse (or the right
stick) points at a sector; the middle names it with its rounds or mode, and releasing Tab takes it. Empty sectors
keep the slot pointed at; the number keys and the mouse wheel still switch at once. Opening a menu over the wheel
closes it without choosing, before the pause takes the time scale. `WeaponWheel` is a framework `UiWidget` that
only draws: it rotates the generated `ui_wheel_sector` texture around its center for each sector. Every item names
its `HeldItem::icon` in the firearm prefabs or on the physics gun and tool gun child entities. The generator
paints the icons (`generator/icons.cpp`), and the spawn menu's search, view and category glyphs, as white silhouettes
from signed distances, so they tint and stay sharp.

### Running, health and damage

Sprint runs only forward, and only with free hands: aiming, carrying a prop or holding one with the physics gun
walks instead. While running, `ViewModel` carries what is in hand low and across the body, swinging with the stride,
the view widens a little, and nothing in hand can be used (`Inventory::CanUseItem()`); aiming ends the run.

The player's `Health` is 100. Landing faster than `FallDamage::safeFallSpeed` (12 m/s, about a 7 m drop) hurts, up
to killing at `lethalFallSpeed`; `CharacterMovement` reports each landing. Explosions damage what is within their radius
by up to their `damage` at the center (barrels 120, rockets 110), characters a third as much behind cover, and throw
the player back (`CharacterMovement` takes "Explosion" damage as a push). The car's `ImpactDamage` hurts what it runs
into. Health comes back after five seconds without hurt. Hurts flash the edges of the
screen red, which keep beating at low health. At zero the player drops what it holds, the screen shows how long until
it comes back, and after `GameMode::respawnTime` the mode returns it to the start with full health. Health itself
stays dead until the game restores it. `GameMode` also returns a player below `fallHeight` without healing; movement
only resets its motion through the pawn contract.

The layout and style are authored in `assets/prefabs/interface.prefab`, opened in the editor's prefab
mode. Its menus, on its root, and its HUD elements, on the widgets they draw into, refer to widgets by id; its inactive item/row templates are
filled from the spawn manifest and action map. To preview a menu, enable its canvas and turn on UI
in the viewport toolbar. The HUD is authored in that same prefab.

Play-test: open/close both menus with keyboard and gamepad; navigate every tab and an offscreen
binding; spawn, undo and clean up; adjust FOV/sensitivity, each bus and quality; rebind a keyboard
and gamepad control; restart and confirm the values persist. Check that menu clicks never fire a
weapon, pause freezes the world, cursor ownership returns and every thumbnail shows its actual prop.

## Player body and inspection camera

The player has a visible, articulated body in both playground and empty maps. `thirdperson` toggles an inspection
camera over the right shoulder; `firstperson` returns to the FPS view. These are cheat commands owned by GameControls,
like `noclip`. The camera follows the aim's yaw and pitch and avoids walls. Its lateral shoulder offset and height
are authored on the ChaseCamera. It converges on the eye ray's first hit, keeping the reticle aligned with
interaction and weapon targeting; movement and shots still use the original eye position and aim.

`prefabs/actors/player_body.prefab` supplies the model, bone attachment and holding, crouching and sitting layers.
`animation/player.controller` uses in-place versions of Walk/Run so animation never moves the physics capsule.
The first-person camera excludes the body's authored layer 30, while external/vehicle cameras include it.
WorldModel puts the equipped item's world meshes on the same layer. The FPS ViewModel is hidden in ThirdPerson
and Seated states; shooting uses the visible world muzzle when the first-person model is hidden.

`CharacterVisual` follows movement speed and facing, and switches the authored pose layers. On entering any vehicle,
the player sits at its hip anchor with the seat's full rotation, including aircraft pitch and roll. The normal
vehicle chase camera shows the driver through the windows. Exiting restores the selected character camera and
standing/held-item presentation. `thirdperson`/`firstperson` while seated select the view to return to without
replacing the vehicle camera. The car and coupe hip markers sit below the window sill and select a compact seated-leg pose; the helicopter
uses the standard seated pose with its own hip height. Head and foot bounds are tested against each cabin.

GPU tests write `player_firstperson.png`, `player_thirdperson.png` and `player_seated_<vehicle>.png` to
`tests/sandbox_captures/`. They compare occupied and empty cabins to verify the driver renders through the glass. Additional up/down
captures verify vertical camera movement. Headless tests aim from the shoulder view and press E to enter and exit
each vehicle, checking both the reticle target and the interaction focus.

## Animated assets

The generator writes `generated/models/humanoid.gltf`: a roughly 1.86 m tall, beveled low-poly human in a blue
shirt and dark trousers. Its 21-bone skeleton includes the spine, neck, shoulders, elbows, wrists, knees, ankles
and toes. Skin weights keep the simple rigid segments attached to their joints. Idle, Walk and Run include basic
arm swing, elbow and knee bends; HoldPistol is a separate upper-body pose. WalkInPlace, RunInPlace, Seated and
Crouch and SeatedCompact support the player presentation. These are ordinary imported assets,
with no special engine loading path or dependency on the generator at runtime.

`animation/humanoid.controller` blends Idle/Walk/Run through its float `speed` parameter. `Npc` is the sandbox's
patrolling Pawn: it turns toward its destination and the Animator moves its CharacterController with root motion.
Footstep clip events play concrete steps. Damage goes to its Health (100, slowly regenerating); when that runs out
it activates its 17-body Ragdoll and applies the impact impulse, and later hits push the ragdoll. `Npc::Hit()` kills
at once; `Npc::Revive()` restores its health and blends back to locomotion. Damage of a `knockdownTypes` type ("Impact", as a car
running it over) that does not kill it knocks it down as a ragdoll; it gets up after `knockdownTime` (3) seconds. Nearby NPCs animate every frame, then 30 times a second beyond
`animationNear` (12 m), and 12 times beyond `animationFar` (30 m).

`prefabs/actors/npc.prefab` is unarmed; `npc_armed.prefab` adds the pistol prefab to its editable loadout. Both have
an Inventory and the framework's WorldModel. The character prefab supplies a BoneAttachment at its right hand,
the weapon grip offset, and the Holding animation layer and bone mask. WorldModel follows the selected item's
model and toggles that layer; it knows no skeleton, bone name, weapon class or sandbox asset path. Its muzzle
position supplies the firearm's effects when there is no first-person ViewModel. NPCs carry weapons without
reading local input; `Inventory::Use()` accepts controller/test intents for firing and reloading.

`Npc::attitude` decides whom it fights. An Enemy fights the players' pawns and Ally NPCs; an Ally fights Enemy NPCs
and otherwise follows the player within `followDistance`; a Neutral fights nobody. Every NPC also fights back whoever
hurts it (kept in its saved provokers), unless they are on its side: NPCs of its own attitude, and the player for an
Ally. A few times a second it picks the nearest hostile pawn it sees within `sightRange` (a physics ray from its eyes);
it walks closer until within `attackRange`, turns to it and, after `reactionTime`, fires its held Firearm through
`Inventory::Use()`, aiming with `GetViewPoint()` at the target's chest with up to `aimError` radians of stray and at
least `shotPause` seconds between shots, refilling an empty weapon. Its shots take `damageScale` (half) of the
firearm's damage, so NPCs do not take players down as fast as players take them. Without a firearm it hits within `meleeRange`.
Out of sight, it searches where it last saw the target for a few seconds. Firearms take `Firearm::damage` health per
pellet; only a possessed pawn's shots rumble the gamepad.

Every Health that runs out also emits `DamageSystem::OnKilled`. The HUD's `KillFeedElement`, under the clock at the
top right, lists those deaths newest first in five authored rows, as "Killer  [Weapon]  Victim" (the source's name,
or the damage type when the killer did it directly, and "Victim  [Fall]" for one with no other killer). Each line
fades out after six seconds; lines the player took part in are in the accent color.

`prefabs/spawnables/npcs/` holds the spawn menu's NPCs: Ally (green shirt, rifle), Neutral (beige, pistol) and Enemy
(red, rifle), standing where they are placed. Placement previews and menu thumbnails draw skinned meshes in their bind
pose.

`npc_spawner.prefab` places one armed and two unarmed neutral NPCs in the playground.
Each spawn chooses its prefab and position. GameControls loads this population when none exists; set its `npcs`
reference to use another population or `spawnNpcs` to disable it. The spawner's instance ids, carried items,
world-model instance references and ragdoll ownership survive checkpoints without duplicating the loadout.

The animation tests exercise the generated skeleton, patrol, damage, revival, mixed loadouts and weapon intents.
Framework tests attach arbitrary items to an unrelated skeleton and socket. GPU tests capture the real armed and
unarmed prefabs, locomotion and ragdolls. Set `SPOT_ANIMATION_CAPTURE_DIR` for the engine's animation test captures;
the sandbox GPU tests write acceptance captures into `tests/sandbox_captures/`.

Rotor-driven branch flexion and twist now use 40% of their original amplitude. Forest pines are individual
instances of three sparse, articulated models, preserving the generated placements and original trunk colliders.
Their main branches, side shoots and leaders have independent joints; needles follow their supporting branches.
The old merged forest bark and needles renderers are disabled; the rock renderer remains active.
