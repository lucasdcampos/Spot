# Prefabs

A `Prefab` (`engine/spot/scene/prefab.h`) is an immutable asset containing one entity tree. It stores the saved data,
parsed once when it loads, rather than live entities or components, so loading it needs neither a graphics device nor
registered game code. Instantiating it creates independent entities in a scene and resolves their components with
the current registry.

Games only instantiate prefabs. Everything that compares an instance with its prefab (overrides, Apply, Revert,
Unpack and saving instances as overrides) is in `spot_tools` (`tools/prefabs/prefab_editing.h`), which the editor and
the command line use and games never link. The engine keeps nothing per instance but the links that let the tools
work it out: each entity's prefab root and id in the prefab, and the prefab on the root.

## Creating and importing

A `.prefab` source uses the scene JSON format (`spot-scene`, version 2, with version 1 still readable). Save a root with
`SaveEntitySource()` (spot_tools) to capture its descendants, transforms, tags, layers, activity and registered
components, keeping the prefabs nested in it as records of their overrides. `Entity::SaveToString(true)` writes an
independent tree instead. The root can belong to a larger hierarchy: its external parent is discarded when the
prefab is read.

The prefab importer validates that the document contains exactly one tree with distinct, nonzero entity ids.
Empty documents, multiple roots, duplicate ids and cycles are rejected before they can add entities to a scene.
Components of unavailable types remain serialized data; import does not create them or call their hooks.
The asset database creates the source's `.meta` file and imports its normalized text with an `AssetType::Prefab`
binary header. Prefab importer version 4 uses the same text payload encoding as scenes, normalizes component ids
and flattens nested prefabs (see [Nesting and variants](#nesting-and-variants)).

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
Components of unknown types are skipped with a warning; tools pass `keepUnknownComponents` to keep them as
`MissingComponent`s that retain their fields for later game code reloads. Components start on the next scene update,
as with other serialized entities.

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
and `GetPrefabLocalId()` identify an instance's entities; `GetPrefabAsset()` and `GetPrefabAssetId()` give the prefab
of an instance root. An ordinary tree made from a prefab without an asset id stays unlinked.

Instances are saved in two forms, described in `engine/spot/scene/prefab_document.h`:

- `Scene::SaveToString()` keeps them linked: each entity is saved whole, with its prefab root and local id, and
  references inside the instance as markers of local ids. Loading it restores the links without the prefab being
  compared, which is what undo, copies and play mode need.
- Scene files, written by `SaveSceneSource()`, keep one record per instance: its prefab asset id, a mapping from
  local entity ids to scene ids, removed entity ids, and overrides as JSON pointer patches per local entity. The
  tools find the overrides by instantiating the prefab the instance was made from and comparing the two.

Components have persistent `ComponentId`s scoped to their entity, so adding, removing or reordering components of the
same type in the source does not redirect existing overrides. Version 1 sources without component ids receive
deterministic ids based on their entity and slot; saving them writes persistent ids, which then survive source
reordering.

Overrides include entity fields, component fields and component additions/removals. Entity references and arrays are
values. Added entities save as ordinary entities alongside the instance, with parent references into its tree. A
removed inherited parent also removes its source descendants, including descendants added by later prefab edits.
Loading rebuilds the tree from the current prefab and applies its overrides, as JSON, without types: unchanged
fields, new components and new entities come from the current source. Plain references of the prefab's entities map
through the instance's local ids; overridden references to other scene entities, including other prefab instances,
use the scene's id mapping.

Overrides whose entity or component target has disappeared are logged and dropped; the next save no longer has them.
A replacement component with a different id does not inherit the old component's edits. Unavailable prefab assets
or malformed instance metadata fail loading before changing the destination scene. Supply the asset manager when
loading scenes outside an application.

`Scene::SaveToString(true)`, `SaveEntitiesToString(entities, true)` and `Entity::SaveToString(true)` expand instances
into ordinary trees, useful for standalone snapshots. Copying an inherited child without its instance root likewise
creates an ordinary tree.

## Editor

The asset browser recognizes `.prefab` files. Drop one into a viewport or the hierarchy to add its tree at the
chosen position as one undoable action. The saved root's rotation and scale are preserved.

Drag entities from the hierarchy onto an asset folder to create `.prefab` sources. A selected descendant is
included with its selected ancestor, rather than creating a second prefab. Names are valid across platforms,
existing files receive numbered names, and the asset database creates the `.meta` identities. The original
entities stay unchanged; drag the resulting asset into a scene to make a linked instance.

Double-click a prefab to open its document. Its view is marked **Prefab**, and saving requires exactly one tree.
The document preserves source-local entity and component ids across saves, including older normalized sources.
Prefab documents are edited without running their components, and saving preserves their nested links.

Select an inherited entity to see its prefab in the inspector. **Apply All** writes the instance's changes to the
source; **Revert All** rebuilds it from the source; **Unpack** turns it into ordinary entities. Expand **Overrides**
to Apply or Revert individual fields and structural changes, including removed targets. Changed component fields
have an accent marker and a right-click menu; a component's menu applies or reverts its whole set of overrides.
Arrays remain atomic values, so reverting one displayed axis of an overridden vector restores that vector.

Apply and Revert preserve entity ids and internal references. Added entities receive new source-local ids when
applied, and become inherited members of the instance. References to entities outside the prefab are cleared in
the source. Every action is one undoable step; Apply undo/redo restores both the source file and the scene. Unpack
preserves values and leaves other instances linked. Actions are disabled while playing.

`spot_tools` has `GetPrefabOverrides()`, `PreparePrefabEdit()` and `UnpackPrefabToString()`. Preparation returns
scene text and, for Apply, validated prefab text without changing live entities or writing files; Apply takes the
text of the prefab's source, which keeps its nested prefabs. A `PrefabOverrideScope` selects a source-local entity and
JSON-pointer subtree, a component, or the entire instance.
The editor executes those prepared documents through `PrefabCommand`, using the project's asset manager for
replay, including in headless tools.

## Nesting and variants

A source can contain ordinary entities and linked prefab instances. Open a prefab document and drop another
prefab into its hierarchy, then save. Dragging an ordinary root with linked children into the asset browser
likewise preserves those links. Base changes propagate through every nesting level when a containing instance
loads; edits made in the containing source override its dependencies, and scene instance edits override that
result. References are remapped at each level, including references between nested and ordinary members.

Drag a linked instance root into the browser to make a variant. Its source contains the base prefab's id and
only the instance's overrides and additions. A variant can itself be the base of another variant or be nested
inside another prefab. Opening it as a document shows the inherited tree; saving preserves the base link.
Apply to an instance of a variant changes the variant, while Apply from its authoring document changes the base.
Apply to a containing prefab preserves its nested dependencies, expressing changes as overrides of those links.

Importing flattens a prefab: the records of the prefabs nested in its source are expanded with their overrides, so
games instantiate one tree without resolving others. The importer reads the nested prefabs from their sources, and
each is a dependency of the import, so changing a base imports every prefab nesting it again. A flattened prefab's
entities from nested prefabs keep plain references to their own prefab's ids, with "referenceScopes" mapping them to
the flattened tree: flattening needs no component types, so the command line can do it.

A containing instance owns one local identity map for its complete expanded tree. Its inherited members refer to
the containing root, so scene files still store one containing record and its own overrides.
New children of dependencies receive deterministic sparse 64-bit local ids from the nested root and the child's
identity; inserting other children cannot redirect their overrides. Collisions are rejected rather than silently
renumbering entities. Source ids remain stable when the document saves those mappings.

`Prefab::GetDependencies()` gives a source's direct prefab asset ids; imported prefabs have none. Cycles, missing
dependencies, expanded trees that are not one tree and nesting deeper than 64 levels fail the import, without
constructing components. A prefab made in memory with nested records is flattened when instantiated, with the same
checks.

## Live updates and history

Saving a prefab refreshes its instances in every open scene and prefab document, including variants and nested
dependents. Apply and its undo/redo likewise propagate through the project's asset refresh on the next frame.
The editor saves each affected document's instances as records (`SaveSceneSource()`), compared with the prefabs
they were made from, resolves them against the current assets in a temporary scene, then replaces its entities only
after loading succeeds, and only when they changed. Broken dependencies leave the open
document intact. A later successful import retries the update. Running play copies keep their current instances;
their editable source documents continue to update.

An open prefab source with no unsaved edits also follows changes to its own file, including Apply from another
document. Unsaved source edits stay in their document; only their inherited instances refresh.

Updates preserve entity and component identities, selections, local additions, removals and field overrides.
They leave command histories and saved-state markers unchanged: inherited changes belong to the source, so a
clean scene remains clean. Commands find components by UUID rather than their previous position, and field undo
replays only changed fields so newly inherited values elsewhere survive. History entries whose targets were
removed from the source safely skip those targets. Undoing an inherited entity deletion restores the containing
instance's compact record against the current source, preserving its link and newly inherited children.

`SceneDocument::RefreshPrefabs()` and `ReplaceSceneText()` implement staging through the public engine API and the
tools.
`GetSceneRevision()` invalidates inspector caches and completes edits in progress after a replacement, without
adding synthetic commands to the history.

## Sandbox spawnables

The sandbox's props, lamps and four firearms are authored as prefabs. `PropSpawner` discovers the imported
prefabs below `prefabs/spawnables` and includes those whose root has a `Spawnable` component. Its category,
order and placement clearance are data; the remaining components define the spawned object. Adding another
marked prefab requires no changes to the game's code. See [Sandbox](sandbox.md) for the workflow and controls.

The playground contains linked instances with its original placements and component settings as overrides.
Held firearm instances remove their world colliders, rigidbodies and renderers; `ViewModel` draws the selected
weapon's model in the player's hand. Spawned firearms keep their complete model tree and physics components.
The tool gun uses separate point and spot lamp prefabs, with color, shadow choice and placement as overrides.
