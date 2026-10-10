# Prefabs

A `Prefab` (`engine/scene/prefab.h`) is an immutable asset containing one entity tree. It stores the saved data
rather than live entities or components, so loading it needs neither a graphics device nor registered game code.
Instantiating it creates independent entities in a scene and resolves their components with the current registry.

## Creating and importing

A `.prefab` source uses the scene JSON format (`spot-scene`, version 1). Save a root with
`Entity::SaveToString()` to capture its descendants, transforms, tags, layers, activity and registered components.
The root can belong to a larger hierarchy: its external parent is discarded when the prefab is read.

The prefab importer validates that the document contains exactly one tree with distinct, nonzero entity ids.
Empty documents, multiple roots, duplicate ids and cycles are rejected before they can add entities to a scene.
Components of unavailable types remain serialized data; import does not create them or call their hooks.
The asset database creates the source's `.meta` file and imports its normalized text with an `AssetType::Prefab`
binary header. Prefab importer version 1 uses the same text payload encoding as scenes.

For code that already has serialized entities, `Prefab::FromString()` returns a validated prefab or logs a warning
and returns nothing. Wrap it in an `AssetRef<Prefab>` to instantiate it without the asset pipeline.

## Instantiating

```cpp
auto prefab = Spot::Assets::Load<Spot::Prefab>("props/crate.prefab");
if (Spot::Entity* crate = scene.Instantiate(prefab)) {
    crate->GetTransform().SetPosition({0.0f, 2.0f, 0.0f});
}
```

`Scene::Instantiate(AssetRef<Prefab>, InstantiateOptions)` returns the tree's root, or null for an unloaded asset.
Every call generates new entity ids. Serialized `EntityId` fields referring to entities inside the prefab map to
the corresponding entities of that instance; references outside the prefab become `EntityId::Invalid`.
Components of unknown types become `MissingComponent`s and retain their fields for later game code reloads.
Components start on the next scene update, as with other serialized entities.

The options can choose a destination parent and sibling index. Root transforms remain local to that parent.
The `keepIds` and `keepExternalReferences` options are always disabled for prefabs. Asset references load through
the running application's manager; when using a standalone manager, pass it explicitly:

```cpp
auto prefab = assets.Load<Spot::Prefab>("props/crate.prefab");
Spot::Entity* crate = scene.Instantiate(prefab, {.parent = shelf, .assets = &assets});
```

The asset manager shares the prefab while references hold it; each instance owns its entities and components.
Instantiation works from imported files alone, after source files have been removed, as in an exported game.

## Editor and persistence

The asset browser recognizes `.prefab` files. Drop one into a viewport or the hierarchy to add its tree at the
chosen position as one undoable action. The saved root's rotation and scale are preserved.

Instances currently save as ordinary entity trees. M8.4 adds their prefab identity and overrides so asset edits
propagate on scene load; M8.5 adds prefab authoring and editing in the editor, followed by nesting and live updates.
