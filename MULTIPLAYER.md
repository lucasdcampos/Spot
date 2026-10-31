# Multiplayer plan

Temporary plan, deleted when its phases are done; nothing in `docs/` or `AGENTS.md` refers to it.

**Goal:** several players in the sandbox's playground, everything they see kept in sync, with the hosting and joining
of servers, and any number of game modes and maps, custom ones included. Three layers, each a base for the next and
for other projects:

1. **The engine** (`engine/spot/net/`, `engine/spot/scene/net/`): lean. Moving bytes between machines, and keeping
   entities in sync. It knows nothing of players, pawns, modes, maps or lobbies.
2. **The sandbox's gameplay framework** (`sandbox/src/gameplay/network/`, plus the replication of each existing
   system in its own folder): sessions, players, their commands, maps and modes. It never names the sandbox, so other
   games can copy it the way they already copy the rest of the framework.
3. **The sandbox** (`sandbox/src/`): its "Sandbox" mode, its menus (host, join, server list), its spawn menu and its tools.

**Agreed on 2026-10-30:** every decision below, as recommended: ENet, replication in the engine, vehicles owned by
their driver, 16 players per server as the bandwidth target, no content download. The mode to test it all is the
sandbox's **free mode** (below), on the playground and on the empty map.

## Starting point (before phases 0–2)

- There is no networking of any kind in the repository.
- The framework already has the right seams: `Controller`/`Pawn` possession, intents kept apart from input
  (`CharacterIntent`, `VehicleInput`, `ItemInput`), damage through `Damage::Apply()`, pawn states by name, `GameMode`
  owning respawn, `WorldSave` snapshotting what is `Persistent`.
- Couplings that block multiplayer:
  - The playground and empty scenes each author their own copy of the **Player** (seventeen components) and
    **Player Controller** entities; nothing spawns players.
  - The **Game** entity mixes what belongs to the map (`EffectLibrary`), to the rules (`GameMode`, `SessionControls`,
    `GameControls`) and to the local player (`Hud`).
  - `PlayerInputListener` adapters write intents straight into the pawn's components, so there is no single per-step
    command to send or replay.
  - Time scale is global: the weapon wheel slows time (`inventory.cpp`), menus pause it (`menu.cpp`), the `timescale`
    command sets it. None of that may happen in a shared world.
  - The main menu's maps are a list in `MainMenu`; maps and modes are not data the game discovers.
  - `Application::Run()` fails without a window, so there is no dedicated server.
  - Entities get fresh `EntityId`s when a scene loads; the saved ids (`"id"` in the scene file) are stable but not
    exposed, so two machines cannot yet agree on which authored entity is which.

## Decisions

1. **Client-server, server authoritative.** No peer-to-peer, no lockstep. A *host* (listen server) and a *dedicated
   server* run the same code: the server is the scene plus a session; a host also has a local player in that scene.
2. **Single player is a server nobody joined.** The gameplay code has one path: whoever has authority (the server, or
   standalone) simulates; clients present. The sandbox alone plays exactly as today.
3. **State replication by snapshots,** not determinism. The server sends each client the state of the objects
   relevant to it at a network rate (30 Hz by default, simulation stays at 60 Hz fixed steps), delta-compressed
   against the last snapshot that client acknowledged. Clients interpolate remote objects about 100 ms in the past.
4. **Clients send commands, not state:** one command per fixed step (view angles and the intents), sent unreliably
   with the last few repeated, plus reliable *requests* for discrete actions (interact, spawn a prop, undo, a tool
   operation, a console command). The server validates both.
5. **Own character decided by its player** (changed 2026-10-31; it was predicted and reconciled, see *Feel*): the
   player's machine moves it and sends where it is, the server validating speeds and states and placing it back when
   they fail. Remote characters and everything else are interpolated.
6. **Driven vehicles are owned by their driver** (owner authority, the server checking speed and position jumps), so
   driving feels local without predicting vehicle physics. Physics props are server-simulated and kinematic on
   clients, but given for a while to the client whose player pushes or carries them (`BodyAuthority`).
7. **Replicated data is described with `Archive`**, as saved data is, in a separate `Replicate(Archive&)` (what a
   client needs to see is not what a save needs). A binary network backend reads `FieldOptions::min`/`max` to
   quantize, so one description drives saving, inspecting and replicating style.
8. **Transport: ENet** through `FetchContent`, pinned, behind the engine's own contract in `net/enet/` (small C library,
   MIT, reliable and unreliable channels, fragmentation, desktop platforms). Alternatives weighed: GameNetworkingSockets
   (encryption and more, but protobuf/OpenSSL), yojimbo (libsodium, mbedtls), SDL3_net (no reliability), our own
   reliability over UDP (more work for the same result). The contract leaves room for WebSocket/WebRTC on the web and
   platform transports on consoles. IPv6 support of the chosen ENet version is checked in phase 1.
9. **Maps and modes are data and are independent.** A map is a scene with a `MapInfo`; a mode is a prefab with a
   `GameMode` subclass, its HUD and its rules, which the server adds to the map it loads. Any project adds both
   without touching the framework.

## Engine: `net/` (transport, no scenes)

Public header `<spot/net.h>`; ENet only in `engine/spot/net/enet/`; public headers show no ENet type.

- **`NetHost`**: `Listen(port, maxPeers)` or `Connect(address)`; `Poll()` gives events (connected, disconnected with a
  reason, received on a channel); `Send(peer, channel, bytes, Delivery::Reliable | Unreliable | UnreliableSequenced)`,
  `Broadcast()`, `Disconnect(peer, reason)`. Per-peer statistics: round trip, jitter, loss, bytes each way.
- **`NetBackend`** contract with two implementations: ENet, and **loopback** (in memory, in `net/` itself), which the
  tests use to run a server and clients in one process.
- **Conditions simulator:** a wrapper over any backend adding latency, jitter, loss and duplicates, set by console
  settings (`net.lag`, `net.jitter`, `net.loss`), for testing feel on a LAN.
- **Discovery:** `NetDiscovery` broadcasts a query on a port and collects answers; the game supplies the answer's
  payload. The engine knows nothing of what is in it.
- **Bits and messages:** `BitWriter`/`BitReader`; quantization of ranged floats, angles, vectors and unit quaternions
  (smallest three); a binary `ArchiveBackend` that writes fields in order without names; a registry of message types
  by name hash, each with `Serialize(Archive&)`.
- **Handshake:** protocol version, a game build id, and a payload the game fills (name, password) and the server's
  callback accepts or refuses with a reason.

## Engine: `scene/net/` (entities in sync)

Header in `<spot/modules.h>`, like the other systems' components.

- **`NetworkObject`** component: a `NetworkId` the server assigns, the owner (a peer or the server), a send rate,
  relevance (radius, or always relevant) and a priority. Its origin is either a prefab (spawned at run time, by
  `AssetId`) or an authored entity of the loaded scene (scene asset plus saved entity id). The scene loader exposes
  the saved id of each entity it makes; nothing else changes in scene files.
- **Replicated components:** a component that has state to show implements the `Replicated` interface:
  `Replicate(Archive&)`, and `OnReplicated()` after a client applied new values. Only fields that changed since the
  client's baseline are sent.
- **`NetworkTransform`:** the transform, quantized, from the authority to the others; clients buffer and interpolate
  it, extrapolate briefly when snapshots are late and snap past a teleport distance. On clients, the `Rigidbody` of
  a replicated object becomes kinematic. Its `authority` is the server, or the owner (for the driven vehicle).
- **`NetworkSystem`** (a `SceneSystem`), whose role is server, client or none:
  - server: spawns and despawns for each client what becomes relevant or not, builds snapshots per network tick under
    a bandwidth budget with a priority accumulator, skips sleeping bodies, deltas against acknowledged snapshots;
  - client: applies snapshots, instantiates prefabs and maps ids, keeps an estimate of the server's tick and the
    interpolation time;
  - messages to objects: `SendToServer()`, `SendToOwner()` and `SendToAll()` for a `NetworkObject`, handled by its
    components; `SendToAll()` is for events such as effects and sounds.
- **Authority queries:** `Network::IsServer(scene)`, `IsClient(scene)`, `HasAuthority(entity)`, `IsOwner(entity)`.
  Without a session every entity has authority, so code written for multiplayer also runs alone.
- **Joining and changing maps:** the server names the scene asset and its content hash; the client loads it with
  `Application::LoadScene()`, says it is ready and receives a full snapshot; authored objects the server destroyed
  before the join are despawned by it.
- **`NetworkModule`:** owns the session in an `Application`, keeps it across `LoadScene()`, receives before the scene
  updates and sends after, registers `net.*` console settings and adds bandwidth, round trip and object counts to the
  statistics overlay.
- **Headless applications:** an `ApplicationConfig` option to run without a window, graphics or audio, for dedicated
  servers and tools; `Run()` stops requiring a window in that mode.
- **Example:** `examples/networking/`, a server and a client of a small scene over loopback, run by the tests.
- **Docs:** `docs/internals/networking.md` (linked from the index), a manual guide for making a multiplayer game.

## Framework: `gameplay/network/`

Depends on `core`, `game` and `persistence`; added to `GAMEPLAY_NEEDS` and the dependency table. Each other folder
replicates its own state with the engine's `scene/net/` only, so the table gains no other edge.

- **`Session`:** `Host(settings)` (port, map, mode, maximum players, server name, password), `Join(address)`,
  `Leave()`, with states and errors for the UI to show. Dedicated servers start one from the command line.
- **`PlayerInfo`:** one replicated entity per connected player, the host included: name, peer, ping, team, and
  numbers a mode names (score, kills). It outlives pawns. `Players` keeps meaning the *local* players; a new
  `PlayerInfos` lists everyone, for player lists and modes.
- **Joining as a player:** on a connection the server makes the mode's player prefab (controller and pawn) at a spawn
  point the `GameMode` picks, owned by that peer. On the server a `RemoteController` applies that peer's commands; on
  the client the `PlayerController` possesses the pawn it owns.
- **`PlayerCommand`:** tick, view yaw and pitch, `CharacterIntent`, `VehicleInput`, `ItemInput`. The adapters fill a
  command each fixed step instead of setting the pawn's components; the controller then applies it (alone or as host),
  or sends it and applies it for prediction (client). The server keeps a short buffer per peer against jitter.
- **Requests:** reliable messages for discrete actions, each checked on the server (reach, cooldown, ownership,
  permission): interact, select a slot, spawn from the catalog, undo, tool operations, console commands and cheats
  (host and admins only).
- **Maps:** `MapInfo` on a scene (name, description, preview, maximum players, the modes it supports) and
  `SpawnPoint`s with tags (a team, a mode). `MapCatalog` and `ModeCatalog` list what a game offers, replacing the list
  in `MainMenu`.
- **Modes:** a mode prefab holds a `GameMode` subclass (who spawns where, respawning, teams, ending, what is
  allowed) with its HUD elements; the server adds it to the map on load and it replicates. Modes say whether time may
  be scaled or paused (`GameMode::allowsTimeScale`), and the wheel, menus and the `timescale` command ask.
- **Content:** the server announces its map and mode by asset id and hash; a client lacking them is refused with a
  clear message. Downloading content is left for later.
- **Server list:** direct address, LAN discovery (name, map, mode, players, ping, password) and recent servers, behind
  a `ServerListSource` so an internet master server can be added later.

## The sandbox's free mode

The mode that tests the whole plan: the sandbox as it is today, with up to 16 players in the room. It is the mode
prefab "Free" (`sandbox/assets/prefabs/modes/free.prefab`), a `GameMode` with the sandbox's rules, and it is also what
single player runs, so playing alone stays as it is.

- **Everything of today, for everyone:** each player has the full kit (inventory and weapons, spawn menu, physics gun,
  tool gun, their own undo list), drives any free vehicle and sees what the others spawn, build and break.
- **Players can kill each other.** Shots, rockets, explosions, vehicles and fall damage hurt any player, as they hurt
  NPCs today: `Damage` already knows no kind of target, so this is the server applying it to a replicated `Health`.
  The instigator is the attacker's pawn, so the hit marker shows to the attacker and the damage flash to the victim.
- **Respawning as today:** the death screen's countdown, then the mode returns the player with full health. Where
  `GameMode` now returns a player to its initial position, it picks one of the map's `SpawnPoint`s, the free one
  farthest from other players; a fall below `fallHeight` returns the player the same way.
- **Both maps:** the playground and the empty map each get a `MapInfo` supporting "Free", and enough `SpawnPoint`s
  spread over them for 16 players. Hosting picks the map; the main menu's single-player list keeps both.
- **What a shared world changes:** the weapon wheel stops slowing time and menus stop pausing it while others are
  connected (the mode allows it only alone); quick save and load, cheats and console commands that change the world
  belong to the host; each player's undo list undoes only what that player made.

## What the playground replicates

Each system's replication lives in its own folder and is tested there.

| System | State | Events | Authority |
|---|---|---|---|
| `character` | position, velocity, view angles, crouch, sprint, noclip, grounded; flashlight | landings (fall damage on the server) | server; own pawn predicted |
| `core` | pawn states (interned names), possession, interactor focus stays local | interact request | server |
| `health`, `game` | health, dead; respawn countdown | damage taken and dealt (flash, hit marker) to the players concerned | server |
| `inventory`, `weapons` | items (prefab ids), selection, ammunition, reload | shots and impacts to all; projectiles are replicated objects | server; own effects predicted |
| `vehicles` | transform, velocity, occupant, input (wheels, sounds), telemetry, health, destroyed and burned | blast | driver, else server |
| sandbox props, spawn menu, undo | spawned prefabs, transforms, colors and materials, placed lamps | spawn and undo requests | server |
| tool gun, physics gun, carrier | joints as replicated objects, the held prop, the beam others see | tool requests | server |
| NPCs and spawners | as characters, plus animation parameters | shots | server |
| `environment` | the day's time; water reacts locally | splashes from server-side events | server |
| `effects`, audio | nothing; presentation follows events and replicated motion (footsteps from the walk cycle, impacts by event) | | |

`CharacterVisual`, `ViewModel`, `WorldModel` and the HUD keep presenting from replicated state, so they change only
where they read something the client lacks. Scripts (music, the monument's sign) stay local. Quick save and load run on
the server only; clients do not offer them.

## Phases

Phases 0 and 1 run in parallel; 2 needs 1; 3 needs 0 and 2; the folders of 4 run in parallel; 5 and 6 need 4 and can
run in parallel. Each phase ends with the sandbox playing as before, its tests passing and its docs updated.

0. **Preparation. Done** (2026-10-30): the player is `prefabs/actors/player.prefab`, made by `GameMode::playerPrefab`
   at the maps' `SpawnPoint`s (farthest from other players; a mode's `spawnTag`). The Game entity now keeps the map's
   `MapInfo`, effects and local scripts; `prefabs/modes/free.prefab` holds the rules, local HUD and session controls.
   Both maps have sixteen spawn points, separate checkpoints and restart settings. `WorldTime` shares the mode's
   pause and time-scale policy: permitted only alone, including a host with no peers, and resumed when a peer joins.
1. **Engine transport. Done** (2026-10-30): `net/`, `tests/net_tests.cpp`.
2. **Engine replication. Done** (2026-10-30): `scene/net/`, `Entity::GetSavedId()`,
   `Scene::GetSourceAsset()`, `WindowConfig::headless`, `examples/networking/`, `tests/network_tests.cpp`,
   `docs/internals/networking.md`, `docs/manual/guides/multiplayer.md`. Scene objects are matched by the ids they were
   saved with, and a joining client destroys those the server no longer has. Follow-up fixes keep changes replicated
   when they return to a baseline whose later acknowledgements were lost, and resolve entity references to objects
   spawned later, even across snapshot budgets.
3. **Sessions. Done** (2026-10-31): `gameplay/network/` with `Session` (rooms with a name, password, player limit and a
   content check at the handshake), `PlayerInfo` (names, pings, the F2 player list), `NetworkPlayers`,
   `RemoteController` and `PawnIntents`; `GameMode` runs only on servers; possession, vehicles and seats, reliable
   pawn requests checked against the sender's pawn; disconnecting removes the player's character and releases the
   vehicle. `MapInfo`, the shared Free mode prefab, `MapCatalog` and `ModeCatalog`; the main menu discovers maps and
   has host and join pages. Committed with phases 4 and 5 so far as 9c6db2a (engine) and 1b14490 (sandbox).
4. **Every system in sync. Done** (2026-10-31): the free mode on both maps, everything of the playground for every
   player, per the table above: characters, items and firearms, carrying, the flashlight, physics gun and tool gun
   (`PropEdits`, lamps on props), vehicles with their wheels, rotors, sounds and crash damage, health and respawn
   countdowns, NPCs and their knockdowns, camera shakes, footsteps and impacts. `tests/sandbox/network_playground_tests.cpp`
   plays a guest, or a dedicated server and two guests, in the real playground.
5. **Feel. In progress**: steps 2 and 3 below done; before them, the own character predicted (to be replaced, see *Feel*), clients' clocks by
   snapshot arrival, 60 snapshots a second shown at least 50 ms back and farther as they come less evenly, firearms
   predicted with hits reported by the shooter's machine, vehicles driven by their driver, bodies given to the
   clients that push them, and the session's numbers on the statistics overlay. The rest is the *Feel* section's
   steps.
6. **The sandbox's menus and server. In progress.** Done: host and join pages (map, players, name, password,
   address), the player name in settings, the F2 player list, and a dedicated server (2026-10-31, ahead of the rest,
   to test without a rendering host): `sandbox --server [--port] [--map] [--mode] [--players] [--name] [--password]`,
   headless at 60 frames a second, no local player (`SessionSettings::dedicated`, `Players::RemoveAll()`), joins and
   leaves logged; drawn assets are unavailable without a graphics device (`AssetLoader::unavailable`). To do:
   - the join page's LAN list: **done** (2026-10-31, 8bc8c15; `ServerListSource`, `LanServers`, rooms answer on 27016);
     fuller connecting and error screens: **done** (be46a98, `SessionError`, retry, steps); recent servers;
   - a server package without textures, sounds and fonts (389 of the build's 526 MB of imported assets are
     textures): first, the room's content check (`GetSessionContentSignature()`) must hash only what the simulation
     reads (scenes, prefabs, scripts, skeletons, clips, the meshes of colliders), as it hashes every asset today and a
     server without textures would refuse everyone;
   - a server-only executable only if its size or dependencies matter one day (a Linux server without a GPU): the
     same executable is 10.5 MB and keeps both roles on one code path and one protocol.

## Feel: diagnosis and plan (2026-10-31; owner-decided movement agreed)

Reported: the own character rubber-bands (it walks, then jumps elsewhere), actions register late, objects freeze.
The causes below come from reading the code. **Found since:** most of what was felt came from testing with two
windows on one computer, the host in the background window sharing the GPU and slowed by the system; with the
dedicated server and one client, the overlay shows 59 snapshots a second, gaps of two frames, 50 ms back, nothing late
and no corrections in the open. The causes below remain for real networks and for hosts that play in a window.

**Own character, corrected again and again.** Prediction is exact in the open (measured in round 2) and wrong
wherever there is something to touch:

1. The server steps the character in another world than the client's. Bodies within `handOffRadius` (1.5 m) are given
   to the client (`BodyAuthority`), so every prop the player walks into is the client's: it moves at once there, while
   on the server it is kinematic, following the client's states only after transit and the server's fixed 50 ms delay.
   The server's character is blocked where the prop was, and the client is corrected back into it. The same with
   players, NPCs and moving props, which the client shows in the past and the server has in the present.
2. The server applies a frame's steps one after another before its physics steps (`RemoteController::ApplySteps()`
   in `OnBeforeFixedSteps`); the client alternates character steps and physics steps.
3. A correction moves the Jolt character without refreshing its contacts: the first replayed step reads the ground
   state of where it was before (`Walk()` asks `IsGrounded()` before `Move()`), so replays around jumps, landings,
   stairs and slopes diverge, and the next step state corrects again.
4. On the client, steps take their yaw from the head, which camera shake turned (`FirstPersonView::OnLateUpdate()`);
   on the server and in replays, from `SetLook()`, without shake. Nearby explosions cause corrections.
5. Only the camera eases a correction (`CharacterMovement::m_correction`), and beyond 2 m it snaps.

**Actions late.** What is not predicted (interacting, vehicles, physics gun, tool gun, spawning, undo) waits for a
client frame, a server frame, a snapshot and the shown delay. The delay rises at once with one late snapshot and falls
slowly (lateness ×0.98 and delay ×0.02 per snapshot): after one hitch, things are shown 200–400 ms back for seconds.
With both processes on one computer the host's frame rate (≈25 fps measured in Debug) quantizes every hop. The
physics gun keeps its prop on the server (`KeepOnServer`), so the prop follows the view a round trip plus the shown
delay late.

**Objects freezing or jumping.**

1. States of owner-decided objects (driven vehicles, given props) are stamped with the server's arrival time and shown
   there with a fixed 50 ms (`shownDelay` adapts only on clients). A client sending every 25–40 ms frame with jitter
   is extrapolated (`maxExtrapolation`), stops, then jumps; the server sends what it showed, and other clients
   interpolate that jitter a second time.
2. Snapshots are stamped with the wall clock when sent, but hold the state after the frame's fixed steps: the
   fixed-step remainder (0–16.7 ms) and two or three steps a frame become jitter in what clients show.
3. A body given to a client starts there where the client showed it, in the past, and the server follows it from that
   pose: moving props jump back when a player comes near, and again when given back.
4. ENet's packet throttle is on (nothing calls `enet_peer_throttle_configure()`): when round trips rise above their
   recent low plus twice their variance, as on Wi-Fi, ENet drops unreliable packets at the sender (snapshots, intents
   and steps). Not seen on localhost (round 3); likely on a real network.

**Server authority or owner authority.** Decision 5 (server-authoritative movement with prediction, chosen
2026-10-30) was for the server to decide everything, so cheating is hard. Since then the sandbox gave clients the
vehicles they drive, the props they push or carry and the hits of their shots: the server no longer has one world to
step the character in, and a modified client can already report hits. Proposed: **owners decide what their player
moves** (the character, its view, the vehicle driven, the props pushed, carried or held) and **the server decides the
rules** (health and damage, inventories and ammunition, spawning and undo, tools, NPCs, the mode), validating what
owners send. It removes code: the engine's `NetworkAuthority::Owner` path already carries vehicles. It is also the
model a survival sandbox such as Shelf needs.

### Steps

1. **Measure. Statistics done** (2026-10-31, ff0f3a5, 9c6db2a, 1b14490); feel tests to do. Network statistics on the
   statistics overlay: round trip, ENet loss and throttle, snapshots received
   per second, bytes, shown delay, share of objects past their last state, corrections per second and their size
   (`NetworkPlayers::GetCorrections()` is counted and shown nowhere). Feel tests in
   `tests/sandbox/network_playground_tests.cpp`, failing today: the guest walks into a crate, up the stairs and into
   another player without being moved back; a third machine sees a driven car never go backwards; a pushed prop is
   never rewound. Played in Release on one computer and on two, and with `net.lag 0.05`, `net.jitter 0.01`.
2. **Engine timing. Done** (2026-10-31, bc00dde) (`scene/net/`, `net/enet/`), whatever the authority:
   - snapshots and owner states carry the simulation time their state was made at (fixed steps), not the wall clock
     when sent;
   - the shown delay follows the peak lateness of the last second, falling as fast as it rises once a hitch is past;
   - owners stamp their states with their estimate of the server's time; the server shows them with the same adaptive
     delay, per peer, and forwards the owner's states with their own times (a time per entry in snapshots) rather
     than what it showed, so other clients interpolate the original motion once;
   - a machine taking authority over a body starts from the newest state it has, carried forward to now, and eases
     what is shown;
   - ENet's throttle off for every peer; the snapshot budget is the rate control;
   - each object's `Replicated` parts cached instead of walking its tree with `dynamic_cast` every send (characters
     carry skeletons).
3. **Characters decided by their owners. Done** (2026-10-31, 3f1635f): as below, with `Pawn::Teleport()` for the
   server's placements, `Pawn::GetSpeedLimit()` and the pawn state "Noclip" (a cheat, the host's alone) for the checks,
   and the feel test of a guest walking into a crate and another player on a dedicated server.
   - `player.prefab`: `Owner` authority, not predicted; `CharacterMovement`'s crouching, sprinting and flying and
     `FirstPersonView`'s look decided by the owner (`IsDecidedByOwner()`).
   - Engine: the server places an owner-decided object (respawning, returning after a fall, leaving a vehicle,
     checkpoints, resetting a vehicle) with a reliable placement whose sequence the owner's states then carry; the
     server ignores older ones.
   - Pushes (knockback, blasts, `SetVelocity()`) go to the owner as messages; the owner reports landings, and the
     server checks them against the fall it saw before applying the damage.
   - Validation in `gameplay/network/`: speeds per state and mode (noclip only where the mode allows it), no movement
     while `blockedBy` or `inactiveWhen`, distance over a window with slack for bursts; failing it, the server places
     the character back where it was last valid and logs it.
   - Removed: `PredictionSystem`, `StepsMessage`, `StepStateMessage`, `RemoteController::ApplySteps()`,
     `PredictedPart`, the per-step sets of `PawnIntents`, `NetworkObject::predicted` and `IsPredicted()` with their
     uses. Items' intents keep going every frame.
   - Protocol 7; `docs/internals/networking.md`, the multiplayer guide and `docs/internals/sandbox_gameplay.md`.
4. **Tools and hand-offs. In progress**: the physics gun's prop given to the holder's machine while held (26de85c).
   To do: a rocket shown leaving at once on its shooter's machine (a local stand-in until the
   server's arrives, as shots already are); the physics gun's prop given to the holder's machine while held, as carried ones are;
   bodies given when the character or vehicle touches them (its contacts), with the radius as a margin and some
   hysteresis, rather than anything within 1.5 m; requests showing their sounds and effects at once where that is
   cheap, the server confirming.
5. **Then** the rest of phase 5: bandwidth with the 500-props benchmark, `net.lag`/`net.loss`, and checking reported
   hits against where the server had the target.

Step 2 comes before step 3: it is what every object the player does not move depends on. Target, checked by step 1's
numbers: on a local network, what the server decides (NPCs, props, rockets, explosions) shown at most about 100 ms
back, recovering within a second of a hitch, and never frozen while it moves on the server.

## Tests

- Engine: serialization round trips and quantization limits; loopback delivery for each delivery kind under loss and
  reordering; deltas against acknowledged baselines; relevance; interpolation and teleports; spawning, despawning and
  late joins; disconnects and map changes.
- Framework (`tests/sandbox/gameplay/network_tests.cpp` and each system's tests): a server `World` and client `World`s
  over loopback with the conditions simulator; commands producing the same motion as playing alone; requests refused
  out of reach; the test-only mode proving a mode is only a prefab.
- Sandbox (`tests/sandbox/`): the free mode with a host and two clients, headless, on each map: they see each other,
  drive, spawn and undo only their own, shoot and blow each other up, see the hit marker and damage flash on the right
  side, and respawn at spawn points after the countdown; alone, the free mode plays as the sandbox does today. The smoke test gains `--smoke-test-network`, a server and a client in two processes.

## Later, not in this plan

Internet server lists and NAT traversal, downloading custom content, playing several clients from the editor,
encryption and accounts, a Luau networking API (and modes written in Luau), voice and text chat.
