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

Drag entities from the hierarchy onto an asset folder to create `.prefab` sources. A selected descendant is
included with its selected ancestor, rather than creating a second prefab. Names are valid across platforms,
existing files receive numbered names, and the asset database creates the `.meta` identities. The original
entities stay unchanged; drag the resulting asset into a scene to make a linked instance.

Double-click a prefab to open its document. Its view is marked **Prefab**, and saving requires exactly one tree.
The document preserves source-local entity and component ids across saves, including older normalized sources.
Prefab documents are edited without running their components. Sources currently expand instances; nesting and
variants follow in M8.6.

Select an inherited entity to see its prefab in the inspector. **Apply All** writes the instance's changes to the
source; **Revert All** rebuilds it from the source; **Unpack** turns it into ordinary entities. Expand **Overrides**
to Apply or Revert individual fields and structural changes, including removed targets. Changed component fields
have an accent marker and a right-click menu; a component's menu applies or reverts its whole set of overrides.
Arrays remain atomic values, so reverting one displayed axis of an overridden vector restores that vector.

Apply and Revert preserve entity ids and internal references. Added entities receive new source-local ids when
applied, and become inherited members of the instance. References to entities outside the prefab are cleared in
the source. Every action is one undoable step; Apply undo/redo restores both the source file and the scene. Unpack
preserves values and leaves other instances linked. Actions are disabled while playing. Saving a source currently
reaches instances when their scenes load; updates of all open documents follow in M8.7.

The engine exposes `GetPrefabOverrides()`, `PreparePrefabEdit()` and `UnpackPrefabToString()` for tools. Preparation
returns scene text and, for Apply, validated prefab text without changing live entities or writing files. A
`PrefabOverrideScope` selects a source-local entity and JSON-pointer subtree, a component, or the entire instance.
The editor executes those prepared documents through `PrefabCommand`, using the project's asset manager for
replay, including in headless tools.
