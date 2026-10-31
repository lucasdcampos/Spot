# Multiplayer

Spot plays games over the network with one machine as the **server**, which runs the game's rules, and the others as
**clients**, which show what the server decides and send it what their players do. The same scene runs on every
machine; the engine keeps its objects in sync. This guide makes a scene multiplayer, then hosts and joins it.

## What runs where

Write the game as you would alone, then ask who decides each thing before changing it:

```cpp
void Door::OnUpdate(float) {
    if (!Spot::Network::HasAuthority(GetEntity())) {
        return; // a client: the server opens and closes it
    }
    // ...
}
```

`Spot::Network::HasAuthority(entity)` is true when the game plays alone, on the server, and on a client only for what
it owns (below) and for entities that are not networked, such as effects. `Spot::Network::IsServer(scene)` tells
whether the scene runs the rules. Code written this way runs the same alone and in a session.

## Making objects networked

Add a **NetworkObject** to an entity to keep it, and what is below it, the same on every machine:

- A **NetworkTransform** beside it sends where it is and how it is turned. Clients show it a tenth of a second in the
  past, moving smoothly between what they received; its body, if it has one, stops simulating on clients.
- Your components send their own state by deriving `Spot::Replicated` and describing it as you describe saved fields,
  but only what others need to see:

```cpp
class Door final : public Spot::Component, public Spot::Replicated {
public:
    bool open = false;

    void Replicate(Spot::Archive& archive) override { archive.Field("open", open); }
    void OnReplicated() override { /* show it open or closed */ }
};
```

Only what changed is sent. References to other networked entities arrive as the client's own entities.

Networked objects placed in the scene are the same objects on every machine. To make one while playing, make it from
a prefab whose root has a NetworkObject, on the server: clients make it from the same prefab, and destroy it when the
server does.

**Priority** and **Relevance Radius** on the NetworkObject decide what goes first when there is too much to send:
far objects of a large world change less often than those near the player.

## Owned objects

Some things should feel immediate to the player using them, such as the vehicle they drive. Set the NetworkObject's
**Authority** to **Owner**, and give the object to a player on the server with
`system->SetOwner(object, peer)`: that player's machine then simulates it and sends where it is, and everyone else
shows it; the rest of its state, as its health, stays the server's. A part whose state that player decides along with
where the object is, as the wheels of a car they drive, says so by returning true from `IsDecidedByOwner()`. When the player leaves, it goes back to the server. `Spot::Network::Get(scene)` is the scene's
`NetworkSystem`, null when it plays alone.

A player's own character should also respond at once, but the server must still decide where it is, so it cannot
walk through walls or run faster than allowed. Tick **Predicted** on its NetworkObject: the owning player's machine
then moves it itself instead of being shown where the server had it, and your game corrects it from what the server
says. The gameplay framework of the sandbox does this for its players' characters, by sending the server each fixed
step's inputs and stepping again those the server had not seen when it corrects the player.

## Messages

For what is not state, such as a player pressing a button or an explosion everyone should see, send messages. A
message is a struct with a name and its fields:

```cpp
struct Use {
    static constexpr std::string_view NetName = "MyGame.Use";
    Spot::EntityId target;
    void Serialize(Spot::Archive& archive) { archive.Field("target", target); }
};

// On the server, once; system is Spot::Network::Get(scene):
system->OnObjectMessage<Use>([](Spot::NetworkObject& object, Spot::NetPeerId player, Use& use) { /* ... */ });
// On a client:
system->SendToServer(*door.GetComponent<Spot::NetworkObject>(), Use{.target = door.GetId()});
```

`SendToServer()`, `SendToOwner()` and `SendToClients()` are about an object; the session's `Send()`, `SendToAll()`
and `SendToServer()` are for the game as a whole. The server must check what clients ask, as a player may send
anything.

The sandbox's copyable gameplay framework provides `Pawn::Request(name)` for discrete player actions. Implement
`PlayerInputListener::OnPlayerRequest(name)` on the pawn part that handles an action; it runs on the server, or
immediately while playing alone. With `NetworkPlayers` in the scene, a client sends it reliably and the server checks
that player still possesses the pawn. The handler must check the action's rules too. The supplied `Interactor`
rechecks reach and obstacles on the server, and vehicles use requests to exit their seats. Movement continues through
`SerializeIntent()`, so repeating movement packets cannot repeat a discrete action.

## Hosting and joining

### Maps and modes in the gameplay framework

The sandbox's `gameplay/game/` keeps maps separate from their rules. To add a map, save a scene with `MapInfo`,
give it a name, description, player limit and spawn points, and select its default mode prefab. `modes` lists the
mode prefab ids it supports; an empty list accepts any mode. The map also owns its checkpoint file, restart scene
and preview. A restart uses the current scene asset when no override is set.

A mode is a prefab with `GameMode` (or your subclass) on its root, its player prefab and the HUD and controls it
needs. Both `GameMode` and its root must be active, with `NetworkObject` on the root so clients can instantiate the
mode selected by the server. Set its name,
description and catalog order on `GameMode`. `MapCatalog` and `ModeCatalog` discover imported content without running
its gameplay; the sandbox's main menu builds its map list from this data automatically.

Before starting a map, the server can call `MapInfo::StartMode(prefab)` to select a supported mode; otherwise the
first update starts `defaultMode`. Clients wait for the server's mode. The supplied Free mode is shared by the
playground and the empty map, each with sixteen spawn points and its own checkpoint.

`GameMode::allowsTimeScale` controls whether the rules allow pause and slow motion. Menus, the inventory wheel and
the sandbox's `timescale` command share `WorldTime` in `gameplay/core/`: changes are allowed only while alone,
including a host with no connected peers. A paused host resumes normal time when another player connects.

### Starting a session

Add the **NetworkModule** to the game's application, then host or join from your menus:

```cpp
auto* network = Spot::Application::Get().GetModule<Spot::NetworkModule>();
network->Host(7777, 15);                              // a server for 15 players
network->Join({.host = "192.168.0.10", .port = 7777}); // or a client
```

A client that joins loads the scene the server runs, and loads the next one whenever the server changes it. The
session's `OnPeerConnected` and `OnPeerDisconnected` tell when players come and go, and a client why it could not
join or was disconnected. `Leave()` ends the session; the scene goes on alone.

## Testing on one machine

Run two copies of the game, one hosting and one joining `127.0.0.1`. Both draw and share the computer, and the one
in the background may be slowed down by the system: as the host is the server, everything then lags for everyone. A
dedicated server avoids that: in the sandbox, `Sandbox --server` serves the playground without a window, and the
copies that play join it. To see how the game plays over a bad network,
set these in the console of either: `net.lag 0.1` (seconds each message takes), `net.jitter 0.03` and `net.loss 0.05`
(the share of messages lost). The `stats` console command shows the session's numbers with the others: round trip,
loss, snapshots a second and the longest wait between them, bandwidth, and how many moving objects are shown late.

## Dedicated servers

A server without a player needs no screen: run it with `ApplicationConfig::window.headless` set, graphics disabled
(`graphics.enabled = false`) and without the audio module, with a frame rate cap (`maxFrameRate`) so it keeps a
steady pace. Remove the map's local players before it starts, so its rules give no pawn to anyone on the server.
