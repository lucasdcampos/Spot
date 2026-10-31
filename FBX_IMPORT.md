# FBX import plan

Goal: drop a character and its animations downloaded from Mixamo into a project and have them work as they do in
Unity: the model with its skeleton, materials and textures, every animation file playable on it, root motion that
behaves, and an editor that makes extracting and remapping materials a click. Temporary plan; delete when done.

## Where things are today

- **Models are glTF only.** `.gltf`/`.glb` map to `AssetType::Model` (`engine/spot/assets/asset_type.cpp`).
  `ModelData::Load()` (`engine/spot/render/model_data.cpp`, cgltf) reads a file into `ModelData`: images, materials,
  meshes with submeshes, nodes (parents first), skeletons and clips. `ImportModel()`
  (`engine/spot/import/importers/model_importer.cpp`) applies the import scale, the clip events from the `.meta` and
  the material overrides, and writes the sub-assets (`mesh:N`, `material:N`, `texture:N`, `skeleton:N`,
  `animation:N`). Everything after reading is format-independent: **an FBX reader only has to produce a `ModelData`.**
- **Clips bind to bones by index.** A skeleton's bones are all the model's nodes, and `AnimationChannel::bone` is a
  node index of the file the clip came from. `Animator` only checks `channel.bone < bones.size()`. A Mixamo character
  file has nodes that its "Without Skin" animation files lack (the armature root, the mesh nodes), so their indices
  differ and the clip plays on the wrong bones **without any warning**. This is the biggest gap for Mixamo, and also
  hits glTF animation files today ("animation-only files must use the same node hierarchy").
- **Root motion takes the root bone's whole delta** (`GetRootMotionDelta()`, `animation_motion.cpp`): with Mixamo's
  Hips as root, the entity would bob up and down and lean with the walk.
- **Materials are glTF metallic-roughness** (`MaterialData`, `MaterialSource`), with roughness and metalness packed in
  one texture (G and B). FBX materials are mostly Phong/Lambert, with separate specular, glossiness or roughness maps.
- **Editor:** files dropped from the OS are copied in (`AssetBrowserPanel::DropFile()`), imported with progress, get
  thumbnails (`thumbnail_cache.cpp`), and dragging a model into the scene instantiates it with an Animator
  (`CreateEntityFromAsset()`). Clip sub-assets have a timeline and preview on a selected rig. The model's import
  settings are drawn generically from `ModelImportSettings::Serialize()`; `AssetDatabase::ExtractMaterial()` exists
  but **nothing in the editor calls it**, and there is no way to extract embedded textures or remap materials by name.

## Decisions

1. **Library: [ufbx](https://github.com/ufbx/ufbx)** (MIT or public domain, one `.c`/`.h`, fetched with
   `FetchContent` at a pinned tag like cgltf). It reads binary and ASCII FBX 6 and 7, converts axes and units, resolves
   FBX's pivots, pre-rotations and geometric transforms, bakes animation to plain TRS keys (`ufbx_bake_anim()`), maps
   Phong, Lambert, Stingray, 3ds Max Physical and Arnold materials onto PBR fields, and reads embedded media. Assimp is
   heavy to build and slow; OpenFBX is less complete; the Autodesk FBX SDK cannot be redistributed. ufbx also reads
   `.obj`, which comes almost free.
2. **FBX is read only by `spot_import`.** Games never load source files, so ufbx stays out of the engine:
   `ModelData::Load()` remains glTF for code that loads raw files. The reader is
   `engine/spot/import/importers/fbx_reader.cpp` (`std::optional<ModelData> ReadFbx(path, ...)`), and
   `ImportModel()` picks the reader by extension. ufbx is linked `PRIVATE` to `spot_import`.
3. **Clips bind to skeletons by bone name**, once per clip and skeleton, not per frame. This fixes animation files for
   glTF as well, and is what makes "one character, fifty Mixamo files" work.
4. **Retargeting is by name, for rigs that share bone names** (every Mixamo character, and rigs exported with the same
   naming). A full humanoid avatar (Unity's muscle space, mapping differently named rigs) is out of scope; an optional
   bone-name map in the `.meta` can come later on top of the name binding.
5. **No Mixamo files in the repository.** Adobe's terms allow using Mixamo characters and animations in games, not
   redistributing them as files. Test files are made from our own content (see Tests).

## Steps

### 1. FBX to `ModelData`

- `.fbx` (and `.obj`) map to `AssetType::Model`; bump the model importer's version.
- Load with `target_axes = ufbx_axes_right_handed_y_up` (glTF's convention, which the engine uses) and
  `target_unit_meters = 1`, so Mixamo's centimeters come in at the right size with the import scale left at 1;
  space conversion that adjusts transforms rather than geometry, so skins stay valid.
- **Nodes:** depth-first from the root (parents before children, as `ModelData` requires), skipping ufbx's implicit
  root; local transforms from `node_to_parent`. Geometric transforms become helper nodes (ufbx option), which skinned
  meshes need.
- **Meshes:** one `ModelMeshData` per ufbx mesh, shared by its instances; faces triangulated
  (`ufbx_triangulate_face()`), split by material into submeshes (`material_parts`), vertices deduplicated
  (`ufbx_generate_indices()`); positions, normals (generated when missing), first UV set, tangents when the file has
  them (mikktspace otherwise, as glTF does). Blend shapes are skipped with one warning, as glTF morph targets are.
- **Skins:** a `Skeleton` per skin deformer, built like the glTF one (all nodes as bones, the clusters as joints, in
  cluster order), with each cluster's `geometry_to_bone` as the inverse bind. Vertices keep their four largest
  weights, renormalized; more than four is common in FBX, so this logs once per mesh at debug level only.
- **Clips:** one per animation stack, baked with `ufbx_bake_anim()` into linear T/R/S keys per node, key reduction on,
  times shifted to start at 0. Channels that never leave the bind value are dropped. A file with a single stack
  named generically (`mixamo.com`, `Take 001`, `Unreal Take`, empty) names its clip after the file: `Walking.fbx`
  gives "Walking". Without this every Mixamo clip is called "mixamo.com".
- **Materials:** from ufbx's PBR mapping: base color and its texture, normal map (bump maps without a normal map are
  skipped with a warning), emission color and strength, roughness and metalness, or roughness from glossiness or
  shininess for Phong (reusing `MaterialSource`'s shininess conversion) with metalness 0. When roughness, glossiness
  or metalness come as separate textures, the importer bakes them into one metallic-roughness texture (glossiness
  inverted), a sub-asset `texture:packed:N`, compressed like the others.
- **Transparency:** FBX opacity is notoriously unreliable (many Maya and Mixamo exports come out see-through in other
  engines). A material is transparent only with an opacity texture or an opacity factor clearly below 1; otherwise it
  is opaque, and the material override fixes the rest.
- **Textures:** embedded ones (`texture->content`) decode from memory like glTF's. External ones are looked for at the
  absolute path, the path relative to the FBX, then by file name in the FBX's folder, `textures/`, `<name>.fbm/` (where
  the FBX SDK extracts embedded media) and the project's assets. **A texture that is already a source asset of the
  project is referenced by its id, not copied into the model**, so editing it updates the model; this needs an
  `ImportContext::FindAsset(path)` beside `FindSource()`. Found files become dependencies. Missing textures are
  reported by name, kept in the import result so the inspector can list them.
- Lights and cameras in the file are skipped (one info line).

### 2. Clips bound by bone name

- `AnimationClip` keeps the name of the node each channel animates (a name table, channels index into it), and the
  bind pose of those nodes in the source file (needed for step 3). The clip's binary format and the model importer
  version are bumped; old imports reimport on their own.
- `AnimationBinding` (`engine/spot/animation/`): for a clip and a skeleton, the skeleton bone of each channel.
  Names match exactly, then without a namespace prefix (`mixamorig:Hips`, `mixamorig1:Hips` and `Hips` are the same
  bone; Mixamo changes the prefix when a scene has several characters). A clip from the same skeleton gets the identity
  binding.
- `Pose::Sample()`, blend trees, layers and `GetRootMotionDelta()` take a binding instead of reading `channel.bone`
  directly. `Animator` and `AnimatorControllerRuntime` build them when the skeleton, a clip or the controller change,
  next to the scratch poses they already allocate there; nothing is looked up per frame.
- A clip with no matching bone is rejected with a warning that names the clip, the rig and a few missing bones,
  instead of today's silent acceptance; partial matches play and warn once.

### 3. Rigs of other proportions

Mixamo writes translation keys on every bone, with the lengths of the character it was downloaded for. On another
character that stretches the skeleton. When a clip's binding is not the identity:

- bones other than the root keep the target's bind translation, and take only rotation (and scale, if animated);
- rotations are carried over relative to the bind poses (`target = sourceKey * inverse(sourceBind) * targetBind`, in
  the parent's space), so rigs whose rest orientations differ slightly still line up;
- the root's translation is scaled by the ratio of the root's bind heights, so strides fit the legs.

A per-clip import setting, `boneTranslations: Retarget | FromClip`, keeps the clip's translations for cases such as
stretchy cartoon rigs. This is a few dozen lines in the sampling path, applied through the binding.

### 4. Root motion that suits Mixamo

- Default root bone: the first skin joint, as now, or for skeletons without a skin (animation files) the bone named
  `Hips` after prefix stripping, then the topmost animated bone.
- Per-clip root motion settings, as Unity's "bake into pose": extract horizontal translation (XZ) and yaw (on by
  default), vertical translation and pitch/roll (off by default, left in the pose). `GetRootMotionDelta()` splits
  the delta accordingly; what is not extracted stays in the visual pose.
- Clips downloaded "In Place" just have no horizontal motion: nothing special is needed.

### 5. Clip import settings

`AnimationEventImportSettings` grows into the clip's import settings, keeping the `clip` and `events` fields so
existing `.meta` files still load: `name` (rename), `loop`, `start`/`end` (to split one long take into several
clips, as Unity does; each split is a sub-asset keyed by its name), `boneTranslations`, and the root motion options
from step 4. Settings left at their defaults are not written.

### 6. Editor

The model inspector gets sections instead of the generic list:

- **Model:** scale, the file's original units and up axis (read-only, so a wrong size can be explained), mesh, vertex
  and bone counts, skipped features (blend shapes, lights).
- **Rig:** the skeleton's bones as a tree, the root motion bone, and for animation-only files the rigs in the
  project their clips bind to fully.
- **Animation:** the clips with their import settings (step 5), the existing timeline and events, and a split editor
  on the timeline.
- **Materials:** each of the model's materials with a thumbnail and a slot that takes any `.material` (drag from the
  asset browser), which writes the existing `MaterialOverride`; buttons to **Extract Materials** (all of them into a
  `Materials/` folder beside the model, through `AssetDatabase::ExtractMaterial()`), **Extract Textures** (embedded
  textures to PNG source files, which the extracted materials then reference), and **Remap by Name** (assign the
  project's `.material` files whose names match). Missing textures are listed with a slot to assign each.
- All of these are undoable source edits, as the controller and material inspectors already do.

Flow around it:

- Dropping several FBX files from the explorer imports them in one refresh, as now; a file that is only animation
  shows a clip icon and its clips expanded in the browser.
- Dragging a clip onto a character in the viewport or the hierarchy previews it there (the existing clip preview);
  dropping it on an Animator's clip field sets it.
- **Create Controller from Clips:** with several clips selected, make a `.controller` with a state each, and a 1D
  blend tree when asked, ready for Idle/Walk/Run.
- A model preview in the inspector (orbit, play a clip) is worth doing after the rest; the thumbnail renderer already
  draws models.

## Tests

Test files come from our content: the sandbox generator's humanoid, exported once to FBX with Blender (binary 7.4, a
character with skin and a separate "without skin" walk, with a namespace prefix), plus a tiny hand-written ASCII FBX
for units, axes, pivots and a Phong material with an external and an embedded texture. Committed under `tests/data/`
with their `.meta` files.

- Import: hierarchy, centimeters to meters, Z-up to Y-up, mesh instancing, submeshes per material, at most four
  normalized weights, bind pose giving an identity palette, clip named after the file.
- Materials: PBR mapping, glossiness inversion and packing, transparency rules, embedded textures, external textures
  found by each search rule, project textures referenced by id, missing textures reported.
- Animation: name binding across files, prefix stripping, rejection of unrelated clips, retargeted translations on a
  rig of other proportions, horizontal root motion without bobbing, clip splitting and renaming.
- Editor (`tests/editor/`): extract materials and textures, remap by name, override slots, undo.
- Golden image: the FBX character mid-walk next to its glTF twin; nothing in rendering changes, so no GPU suite run
  beyond the golden test.

## Documentation

- `docs/internals/assets_and_importing.md`: FBX in the type table, how the reader works, texture search, the new
  import settings and versions.
- `docs/internals/animation.md`: binding by name, retargeting, root motion extraction; remove the "same node
  hierarchy" restriction.
- `docs/internals/editor.md`: the model inspector sections and the clip actions.
- `docs/manual/guides/`: a new guide, "Characters and animations from Mixamo": download settings (FBX Binary, With
  Skin for the character, Without Skin for animations, In Place or not), importing, materials, a controller, root
  motion.

## Order

1. ufbx and the FBX reader for static models and materials (steps 1, without skins and clips).
2. Skins and clips (rest of step 1).
3. Binding by name (step 2): from here Mixamo animation files play.
4. Root motion and clip settings (steps 4 and 5).
5. Retargeting (step 3).
6. Editor (step 6), then the manual guide.

Each step lands with its tests and doc changes.

## Open questions

- Retargeting beyond shared bone names (a humanoid avatar mapping, e.g. Mixamo clips on Unreal's mannequin) is left
  out; a bone-name map in the `.meta` would be the first step if it is wanted.
- Blend shapes (facial expressions) are skipped in FBX, as in glTF: the engine has no morph targets yet.
- Whether `ModelData::Load()` should read FBX for code-only use: left out so games do not carry ufbx.
