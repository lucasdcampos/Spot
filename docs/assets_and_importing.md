# Assets and importing

Assets live in two forms. **Source assets** are the files artists and tools make (PNG, glTF, WAV, `.material`,
`.scene` and the rest), under a project's `assets/` folder. **Imported assets** are the engine's own binary formats,
made from the sources, which is all a shipped game loads. Importing is for tools: `spot_import` (`spot::import`,
`engine/import/`) does it, and games never link it.

## Source assets and `.meta` files

Every source asset has a `.meta` file beside it, committed with it. It holds the asset's `AssetId`, which never
changes, and its import settings. References between assets and from scenes go through ids, so files can be moved
and renamed without breaking anything. Never edit ids in `.meta` files by hand.

Files are recognized by extension (`GetSourceAssetType()` in `engine/assets/asset_type.h`):

| Type | Extensions |
|---|---|
| Texture | `.png` `.jpg` `.jpeg` `.bmp` `.tga` |
| Model | `.gltf` `.glb` |
| Material | `.material` (JSON) |
| Sound | `.wav` `.ogg` |
| Font | `.ttf` `.otf` |
| Scene | `.scene` (JSON) |
| Prefab | `.prefab` (one entity tree in scene JSON) |

Folders whose name starts with `.` are skipped. A source can make sub-assets, such as a model's meshes and materials,
which get ids of their own under a key that names the part (`models/house.glb#Roof`).

## The asset database

`AssetDatabase` (`engine/import/asset_database.h`) looks after a source directory and an import directory:

- `Refresh()` finds the sources, gives new ones a `.meta` file and imports the ones that changed into the import
  directory, along with a manifest of what it holds. Ids that clash, as from a file copied with its `.meta`, are
  replaced in the copy.
- Shipped games hold their imported assets in one pak, `assets.pak` in the import directory
  (`engine/assets/asset_pak.h`), which `spot export` and `spot pack` write: a table of the assets, then each one,
  compressed with LZ4 when that saves enough. `AssetManager` reads from it whenever it is there, and from a file per
  asset otherwise, as the editor and `spot run` do; opening one file instead of hundreds halves the time a scene
  such as the sandbox's playground takes to load.
- Imports are cached: a source is imported again only when it, a file it depends on, its settings or its importer's
  version change. `GetChanged()` tells what was imported again, so loaded copies can be reloaded.
- `CreateFolder()`, `Move()`, `Delete()`, `Duplicate()` and `CopyIn()` change sources the way tools do, keeping each
  `.meta` file with its source.
- `ExtractMaterial()` turns a model's material into a `.material` file that can be edited.

## Importers

Each source type has an importer in `engine/import/importers/` that turns the source into imported files. An
importer has a version; **a change to any imported format bumps its importer's version**, which makes every source
of that type import again. A new source format gets a new importer there.

Import settings live in each `.meta` file, and the editor's inspector edits them: textures choose mipmaps and whether they hold colors (`srgb`, off for data such as LUTs and normal maps), models a
scale and material overrides, fonts their sizes, and sounds whether they stream (see [Audio](audio.md)); sounds that
do not stream are stored decoded, as 16-bit samples.

Textures with mipmaps whose sides are multiples of 4 are stored block-compressed (`compress`, on by default): BC1,
or BC3 when any pixel is not opaque, a quarter or an eighth of the memory of their pixels, with every mip level made
when importing (averaged as light for colors) rather than when loading. Textures without mipmaps, such as pixel art,
keep their exact pixels. On a GPU that cannot sample those blocks the loader turns them back into pixels. Models'
embedded images are compressed the same way. The block encoder is `stb_dxt`
(`engine/import/importers/texture_compression.cpp`). Averaging alpha makes cutouts such as leaves thin out in the
smaller levels, as their gaps blend in and sink below the cutoff; `keepAlphaCoverage` scales each level's alpha so as
much of it stays at half alpha or more as in the full image, and foliage keeps its density from afar.

## Loading imported assets

`AssetManager` (`engine/assets/asset_manager.h`) loads imported assets from an import directory, by id or by source
path, and shares each one while something refers to it. The running application has one, reached anywhere through
`Spot::Assets`:

```cpp
auto model = Spot::Assets::Load<Spot::Model>("models/house.glb");
auto texture = Spot::Assets::Load<Spot::Texture>(textureId);
```

`Load()` returns an `AssetRef<T>`, a shared reference that also remembers the asset's id, so components that hold
one save it in scenes (`Archive::Field()` takes `AssetRef` fields through `assets/asset_archive.h`). A reference can keep an id without an asset when
loading failed; saving keeps the id. Assets that need the GPU, such as textures and meshes, only load with a
graphics device.

`AssetManager::GetAssetIds(type, directory)` lists imported assets of a type below a relative source directory,
including its descendants, sorted by source path and then id. The directory is optional; built-in assets are
excluded. It reads the runtime manifest without loading assets or inspecting source files, so shipped games can
discover content such as spawnable prefabs. `RefreshManifest()` makes newly cooked entries available to queries.

Loading has two halves: reading and decoding the asset's file, which needs neither the GPU nor other assets and runs
on any thread, and making the asset, on the thread that loads it. So:

- `LoadAsync<T>(id)` reads and decodes the asset on the job system's threads and returns an `AssetRequest<T>`; the
  next `AssetManager::Update()` after that, which the application calls every frame, makes it, and the request is
  then done (`IsDone()`, `Get()`). What the asset refers to, such as a material's textures, loads when it is made.
- `Preload(ids)` loads the assets and what they refer to at once: wave by wave, the files of a wave are read and
  decoded together on the job system's threads, which tells what they refer to (a material's textures, a model's
  meshes and materials, the ids that scenes and prefabs name), the next wave; then the assets are made from the last
  wave back, so each finds what it refers to ready. It returns what it loaded, which stays loaded while kept.
  `StartPreload(ids)` does the reading and decoding in the background, and `FinishPreload()` makes the assets once
  `IsReady()`, as scenes streamed in do. Loading a scene preloads what it names (`PreloadReferencedBy()`) before
  making its entities; fonts rasterize their
  glyphs while being read, the costliest part of loading them.

A mesh asset can also load as its CPU triangles, `MeshData`, with `AssetManager::LoadMeshData()` or
`Load<MeshData>()`, as mesh colliders need; it loads without a graphics device, and is cached apart from the GPU mesh.

Built-in assets have fixed ids and need no file (`engine/assets/built_in_assets.h`): the cube, sphere and plane
meshes and the grid and checker textures.

Raw files can also be loaded directly, without importing, through `Model::Load()`, `Image::Load()` and the like, for
code that does not use the asset pipeline.

`Prefab` assets share serialized entity trees and load without a graphics device. `Scene::Instantiate(prefab)`
creates independent entities and remaps internal entity references. See [Prefabs](prefabs.md) for the source
format, validation, placement options and asset resolution outside an application.

Models preserve their glTF node hierarchy, including empty nodes and local transforms. Each unique mesh becomes
one mesh subasset, whose primitives are submeshes with separate material references. A model's hierarchy and
references load without a graphics device; mesh references keep their ids for serialization. Import scale applies
to both vertex positions and node translations, so it scales the whole hierarchy. Model importer version 9 stores
this layout and reimports older models automatically, preserving subasset ids by their keys.

## Where imports go

- In a project, sources are under `assets/` and imports under `.spot/imported/`, which the editor and `spot run`
  load from.
- `spot cook --out <folder>` and `spot export` import into the game's folder, beside `game.json`, whose
  `importDirectory` points at them.
- The engine's own build cooks the sandbox's assets beside its executable.

## Asset types of other layers

The asset manager loads the types of the layers up to its own. Layers above it register loaders for theirs with
`AssetManager::RegisterLoader()`, as the scene layer does for prefabs; scenes are not shared assets, and
`InstantiateScene(AssetManager&, AssetId)` (`engine/scene/scene_assets.h`) makes a new one from its imported data
each time, which `AssetManager::ReadImportedData()` gives.
