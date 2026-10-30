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
`system->SetOwner(object, peer)`: that player's machine then simulates it and sends its state, and everyone else
shows it. When the player leaves, it goes back to the server. `Spot::Network::Get(scene)` is the scene's
`NetworkSystem`, null when it plays alone.

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

## Hosting and joining

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

Run two copies of the game, one hosting and one joining `127.0.0.1`. To see how the game plays over a bad network,
set these in the console of either: `net.lag 0.1` (seconds each message takes), `net.jitter 0.03` and `net.loss 0.05`
(the share of messages lost).

## Dedicated servers

A server without a player needs no screen: run it with `ApplicationConfig::window.headless` set, graphics disabled
(`graphics.enabled = false`) and without the audio module.
