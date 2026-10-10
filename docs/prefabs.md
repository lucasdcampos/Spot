# Prefabs

A `Prefab` (`engine/scene/prefab.h`) is an immutable asset containing one entity tree. It stores the saved data
rather than live entities or components, so loading it needs neither a graphics device nor registered game code.
Instantiating it creates independent entities in a scene and resolves their components with the current registry.

## Creating and importing

A `.prefab` source uses the scene JSON format (`spot-scene`, version 2, with version 1 still readable). Save a root with
`Entity::SaveToString(true)` to capture its descendants, transforms, tags, layers, activity and registered components.
The root can belong to a larger hierarchy: its external parent is discarded when the prefab is read.

The prefab importer validates that the document contains exactly one tree with distinct, nonzero entity ids.
Empty documents, multiple roots, duplicate ids and cycles are rejected before they can add entities to a scene.
Components of unavailable types remain serialized data; import does not create them or call their hooks.
The asset database creates the source's `.meta` file and imports its normalized text with an `AssetType::Prefab`
binary header. Prefab importer version 2 uses the same text payload encoding as scenes and normalizes component ids.

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

## Instance identity and overrides

An imported prefab's entity ids become stable local identities within each instance. `Entity::GetPrefabRootId()`
and `GetPrefabLocalId()` identify inherited entities; `GetPrefabAssetId()` identifies a linked instance root.
Entities made from a prefab without an asset id remain ordinary trees.

Scene files store one record per instance: its prefab asset id, a mapping from local entity ids to scene ids,
removed entity ids, and overrides as JSON pointer patches per local entity. Components have persistent
`ComponentId`s scoped to their entity, so adding, removing or reordering components of the same type in the source
does not redirect existing overrides. Version 1 sources without component ids receive deterministic ids based on
their entity and slot; saving them writes persistent ids, which then survive source reordering.

Overrides include entity fields, component fields and component additions/removals. Added entities save as
ordinary entities alongside the instance, with parent references into its tree. A removed inherited parent also
removes its source descendants, including descendants added by later prefab edits. Loading rebuilds the tree from
the current prefab and applies its overrides. Unchanged fields, new components and new entities come from the
current source. Internal references use the local id mapping; overridden references to other scene entities,
including other prefab instances, use the scene's id mapping.

Overrides whose entity or component target has disappeared are logged and retained, so restoring that source id
can restore the override. A replacement component with a different id does not inherit the old component's edits.
Unavailable prefab assets or malformed instance metadata fail loading before changing the destination scene.
Supply the asset manager when loading scenes outside an application.

`Scene::SaveToString(true)`, `SaveEntitiesToString(entities, true)` and `Entity::SaveToString(true)` expand instances
into ordinary trees, useful for prefab authoring and standalone snapshots. Copying an inherited child without its
instance root likewise creates an ordinary tree. The default saves preserve instance identity.

## Editor

The asset browser recognizes `.prefab` files. Drop one into a viewport or the hierarchy to add its tree at the
chosen position as one undoable action. The saved root's rotation and scale are preserved.

Drops and undo/redo preserve prefab identity. The document uses the project's asset manager when replaying its
history, including in headless tools. M8.5 adds prefab authoring, editing and override controls in the editor,
followed by nesting and live updates.
