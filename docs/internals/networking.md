# Networking

Multiplayer in two parts: `net/` moves bytes between machines and knows nothing of scenes; `scene/net/` keeps a
scene's entities in sync between a server and its clients. Neither knows players, pawns, game modes or lobbies,
which games build on them. Where to start: [`examples/networking`](../../examples/networking/main.cpp), a server and a
client in one process, and `tests/network_tests.cpp`.

## Transport (`net/`)

`NetTransport` (`net/net_transport.h`) is the contract of the backends: open a port, connect, send on a channel with
a `NetDelivery` (`Reliable`, in order; `Unreliable`; `Sequenced`, never older after newer), disconnect, and poll
events. `CreateNetTransport()` makes the platform's, ENet over UDP, in `net/enet/`; it is the only file that includes
ENet, and ENet 1.3 speaks IPv4 only. `NetDatagramSocket` sends single packets without connections, for discovery.

- **Loopback** (`LoopbackNetwork`): transports and sockets of one process that reach one another by port, delivering
  at the receiver's next poll, in order and without loss. The tests run servers and clients through it, with no
  sockets. Sockets sharing a port get distinct hosts (`loopback1`, `loopback2`…), as machines would.
- **Conditions** (`ConditionedNetTransport`): wraps any transport and delays, drops and duplicates what this end
  sends, by `NetConditions`. Reliable messages are only delayed, keeping their order, and sequenced ones never pass a
  newer one. Its clock and seed can be given, so tests are repeatable.

`NetHost` (`net/net_host.h`) is what games use: a server (`Listen()`) or a client (`Connect()`) over a transport. It
keeps the transport's channel 0 for a handshake and shifts the game's channels after it. A client introduces itself
with a hello and the `NetHostConfig::protocol`; the server refuses another protocol, or whatever its `onConnecting`
refuses, sending the reason before disconnecting, so the client's `Disconnected` event says why. A server tells only
of clients it accepted; a client always learns how its attempt ended. Connections that do not introduce themselves
within `handshakeTimeout` are dropped.

Data is written with `BitWriter` and read with `BitReader` (`net/bit_stream.h`), which fails instead of reading past
the end, so a packet is checked once after it is read. `NetQuantize` writes ranged floats, angles and rotations
(smallest three) in fewer bits. `NetArchiveWriter` and `NetArchiveReader` are `Archive` backends that write fields
in order, without names and exactly, so messages and replicated state are described as saved data is. Entity ids
differ between machines, so a `NetEntityMapper` turns them into ids both know.

Messages (`net/net_messages.h`) are structs with a `NetName` and a `Serialize(Archive&)`; their id on the wire is the
FNV-1a hash of the name. `NetMessageHandlers` calls the handler of each type and refuses unknown or truncated ones.

`NetDiscovery` broadcasts a query on a port; every `NetDiscoveryResponder` of the same protocol listening there answers
with what the game gives it, such as a server's name and port.

## Replication (`scene/net/`)

`NetworkSession` owns a `NetHost` with `ChannelCount` channels: the game's messages on channel 0
(`GetMessages()`, `Send()`), then reliable replication, snapshots (sequenced) and unreliable replication. It outlives
scenes and tells of peers joining and leaving. The server's clock (`GetTime()`) is what objects are shown by; clients
estimate it from snapshots, half a round trip late, easing toward each new estimate.

`NetworkSystem` binds one scene to a session, which it keeps alive; `SetSession(nullptr)` lets go of it and the scene
plays alone, its objects back as they were. With no system, or a released one, every `Network::` query answers as for
a game alone: the scene is the server's and has authority over everything. Gameplay code asks
`Network::HasAuthority(entity)` and runs the same with and without a session.

### Objects

A `NetworkObject` makes its entity, and what is below it down to the next `NetworkObject`, one replicated object. Its
state is its `NetworkTransform`, if any, and its `Replicated` components, each written by `Replicate(Archive&)` into a
blob of its own with its size, so a component that reads differently skips only itself.

- **Objects of the scene's file** are matched by the id they were saved with (`Entity::GetSavedId()`), which every
  machine that loads that file gives them; `Scene::LoadFromString()` sets it, and nothing else does. When a client is
  ready, the server sends it the saved ids of the scene objects that still exist, and the client destroys the others.
- **Objects made since** must be prefab instances whose root has a `NetworkObject`. The server spawns the root and
  numbers the objects below it in depth-first order; the client instantiates the same prefab
  (`NetworkSystemConfig::loadPrefab`, or the application's assets) and gives its objects the ids in the same order.
  An object added below a replicated one, or made without a prefab, is not replicated, with a warning.

The server assigns `NetworkId`s at bind time and when objects are added, registering added trees at the next snapshot
so that their children exist.

### Snapshots

Every `1 / sendRate` seconds the server writes each object's state once, and, for each ready client, sends what
differs from what that client acknowledged: spawn information until a snapshot holding it is acknowledged, the
owner flag, and the state. Changes are taken by an accumulated priority (`NetworkObject::priority`, lower beyond
`relevanceRadius` from the client's focus, `SetFocus()`), never-seen objects first, until `snapshotBudget` bytes;
the rest wait. A child is sent only once its root is known to the client. Despawns go in every snapshot until one
holding them is acknowledged: by saved id for scene objects, by network id for spawned ones.

Clients acknowledge the latest snapshot with a mask of the 32 before it, unreliably, after each one they receive; the
server keeps 64 snapshots per client to match acknowledgements to what they held. Nothing is resent by the
transport: state that was lost differs from what was acknowledged, so it goes again.

`NetworkTransform` writes the world position (bits from `range` and `precision`) and rotation (15 bits per component).
Machines without authority keep the states and show the object at `GetShownTime()`, `interpolationDelay` in the
past, between the two states around it; past the last one it carries on for `maxExtrapolation` seconds, and farther
than `teleportDistance` in one step it jumps. A new object starts at its first state. Bodies of objects this machine
has no authority over are made kinematic, and restored when it gets authority.

### Authority and messages

`NetworkAuthority::Server` objects are decided by the server. `Owner` objects are decided by the client that owns
them (`SetOwner()`), which sends their state at the send rate; the server reads it only from that client, shows it
interpolated and forwards it in its snapshots. When a client leaves, what it owned goes back to the server.

Messages about an object go with its network id: `SendToServer()`, `SendToOwner()` and `SendToClients()`, reliable or
not, to the handler `OnObjectMessage<T>()` registered for the type, with the object and the sender. A server ignores
those of clients not ready; a client drops those of objects it does not know.

### Applications

`NetworkModule` hosts (`Host()`) or joins (`Join()`) a session over the platform's transport, wrapped in the
conditions the `net.lag`, `net.jitter` and `net.loss` console settings give, and binds the application's scene to it
each frame. The server sends each client the scene asset it runs (`Scene::GetSourceAsset()`, set by
`InstantiateScene()`), when it joins and whenever the scene changes; a client loads it, and its system tells the
server which scene it runs once connected: the server replicates only to clients running its scene. `Leave()` tells
the other machines and lets the scene play alone. The profiler counts replicated objects and snapshot bytes.

A dedicated server needs no screen: `WindowConfig::headless` with graphics disabled, and without the audio module.
