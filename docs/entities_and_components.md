# Entities and components

Scenes hold entities; entities hold a transform, a place in the hierarchy and components, which give them behavior.
The code is in `engine/spot/scene/`.

## Entities

`Scene::CreateEntity()` and `Entity::CreateChild()` create entities, which stay at the same address until they are
freed after `Destroy()`. An entity has:

- a name and tags (`AddTag()`, `Scene::FindEntitiesWithTag()`),
- a `Transform`: local position, rotation and scale, and world ones computed through its parents,
- a parent and ordered children (`SetParent()`, `SetSiblingIndex()`),
- a layer, from 0 to 31, which physics filters by (see [Physics](physics.md)),
- an active flag: an inactive entity, and everything below it, has no active components.

### Ids

Every entity has an `EntityId`, unique across all scenes and never reused. Unlike a pointer, an id can be kept
after the entity is destroyed: `Scene::GetEntity()` then returns null. Ids are what lasts across frames, undo,
saving and code reloads, so tools and long-lived references use them rather than pointers. Components save
references to entities as ids too (`Archive::Field()` with an `EntityId`).

## Components

Components derive from `Component` and override the hooks they need:

```cpp
class Spinner final : public Spot::Component {
public:
    float speed = 1.0f;

protected:
    void OnUpdate(float deltaTime) override {
        GetEntity().GetTransform().Rotate(Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, speed * deltaTime));
    }
};

entity.AddComponent<Spinner>();
```

A component is **active** while it is enabled, its entity is active in the hierarchy, neither is being removed, and
it has not faulted. Its hooks run in this order:

| Hook | When |
|---|---|
| `OnStart()` | Once, in the first `Scene::Update()` where the component is active, followed by `OnEnable()` |
| `OnEnable()`, `OnDisable()` | Each time a started component becomes active or stops being active, at once |
| `OnFixedUpdate()` | On each fixed step, before the systems' (physics) step |
| `OnUpdate()`, `OnLateUpdate()` | Every update; all `OnUpdate()` of the scene run before the first `OnLateUpdate()` |
| `OnDrawUi()` | Each frame the scene is shown, after it was drawn, for what goes over it, such as a HUD |
| `OnDestroy()` | Once, if `OnStart()` ran, when the component is removed or its entity destroyed |
| `OnValidate()` | After tools changed its fields rather than its own functions |

The entity is not available in the constructor; setup that needs it goes in `OnStart()`. `GetComponent<T>()`,
`GetComponents<T>()`, `GetComponentInChildren<T>()`, `GetComponentInParent<T>()` and `Scene::FindComponent<T>()` find
components, including types derived from `T`. `Scene::ForEachComponent<T>()` walks the enabled ones on active
entities, in the order they were added; components fn adds are found by the next call.

These queries scale with the components they look at, not the size of the scene. Each component knows the index of
its concrete type, and whether a concrete type is of a queried one is answered by a cast once per pair of types, then
read from a table. The scene keeps a list of the components of each concrete type, updated as components are added,
moved and freed, so `ForEachComponent<T>()` goes through the lists of the types that are a `T`, merged by when each
component was added, and `FindComponent<T>()` looks at the head of those lists. Freeing a component or an entity
empties its slots, which are compacted once they are many, and costs the same in a scene of any size.

### Components that need others

A component can declare the components it needs on its entity, which is how an entity gets the shape of a class,
such as a player, out of components:

```cpp
class Player final : public Spot::Component {
    SPOT_REQUIRE_COMPONENTS(Health, PlayerMovement)
    ...
};
```

Adding a `Player`, by code or by tools from its registered name, first adds the `Health` and `PlayerMovement` the
entity lacks, and what those require in turn; a component of a type derived from a required one counts. Removing a
required component that is the only one of its kind while something requires it is refused, with an error logged.
Loading a saved scene adds nothing, since it holds what was saved. The required types must be default-constructible.

### Failures

Every hook runs under one guard. A hook that throws **faults** its component: the error is logged once with the
entity, the component's type and the hook, the component stops being active (`IsFaulted()`), and the rest of the
scene keeps running. A faulted component still gets `OnDisable()` and `OnDestroy()`, so it can release what it holds.
The fault lasts until the scene is reloaded, which creates new components. The editor's inspector marks faulted
components. See [Diagnostics](diagnostics.md).

## Saving and loading

`Scene::Instantiate(const Model&, name)` creates a named root and the model's node tree below it. Empty nodes and
duplicate names are kept; each entity receives a new `EntityId`. Each node retains its local transform, so a rotated
child under a non-uniformly scaled parent keeps the resulting world-space shear. A mesh node has one `MeshRenderer`
per submesh, with its material; the GPU mesh and material assets are shared between instances. Nodes can then be
moved, reparented or disabled independently, and the named root moves or destroys the whole instance.

```cpp
auto model = Spot::Assets::Load<Spot::Model>("models/house.glb");
if (model) {
    auto& house = scene.Instantiate(*model, "House");
    house.GetTransform().SetPosition({10.0f, 0.0f, 0.0f});
}
```

Instances are ordinary entity trees: saving retains their hierarchy, transforms, mesh and material asset ids and
submesh indices. `MeshRenderer::SetSubmesh()` selects one range; `Mesh::AllSubmeshes` (saved as `-1`) draws the whole
mesh and remains the default for older scene files. `InstantiateModel()` delegates to the same scene API.

`Scene::Instantiate(AssetRef<Prefab>)` creates a saved entity tree with independent ids and remapped internal
references. Linked instances save their prefab id and overrides, so source edits propagate on load. Components have
persistent `ComponentId`s within their entities; `Component::GetId()` identifies them across save/load and undo.
Version 2 scenes save these ids and prefab records while still loading version 1. See [Prefabs](prefabs.md) for the
format, source-local identity, overrides and expanded snapshots.

Scenes save as JSON (`Scene::SaveToString()`, `SaveToFile()`, `Entity::SaveToString()` for a subtree) and load
with `LoadFromString()`, `LoadFromFile()` or `Instantiate()`. A component is saved when its type is registered and
it describes its fields:

```cpp
class Spinner final : public Spot::Component {
public:
    float speed = 1.0f;
    Spot::EntityId target;

    SPOT_FIELDS(speed, target)
};
SPOT_COMPONENT(Spinner);
```

`SPOT_FIELDS()` lists fields by name; overriding `Serialize(Archive&)` instead allows options such as ranges and
tooltips, which the editor's inspector uses:

```cpp
void Serialize(Spot::Archive& archive) override {
    archive.Field("speed", speed, {.min = 0.0f, .tooltip = "Turns per second"});
}
```

`Archive` is in the core (`engine/spot/core/io/archive.h`), so every layer describes its data with it; the core only names
entity and asset ids. Types of higher layers add themselves with a `SerializeField()` free function found by
argument-dependent lookup, as `AssetRef` does in `assets/asset_archive.h` and `ComponentRef` in its own header.

The same `Archive` drives saving, loading and the inspector, through different backends, so a field described once
is saved and edited the same way. Fields added later keep their defaults when older files load.

### Registering types

`SPOT_COMPONENT(Type)` registers a type under its name when the game's code loads; `RegisterComponent<T>("Name")`
does it by hand under another name. The name is written to files, so it must stay the same. Components whose type is
not registered when a scene loads, as when the game's code is not loaded in the editor, become `MissingComponent`s
that keep their data and save it back unchanged.

`Entity::ForEachChild()` visits live children without copying the child list. Do not reorder the hierarchy during this traversal.

Animation clips send `OnAnimationEvent(const AnimationEvent&)` to the active components on
the animator entity, with the same failure guard as lifecycle and physics hooks. Seeking and
editor previews do not send gameplay events; see [animation](animation.md).
