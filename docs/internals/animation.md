# Skeletal animation

Start with `ModelData::Load()` for CPU data, or load imported `Skeleton` and `AnimationClip`
assets through `AssetManager`. glTF and FBX skins become skeleton sub-assets and named animations
become clip sub-assets, also for animation-only files (see [FBX and OBJ models](assets_and_importing.md#fbx-and-obj-models)).

Skeleton bones are contiguous, parent before child, and include intermediary nodes needed
for transforms. The joint array preserves glTF skin order; inverse bind matrices follow it.
Clips address the same node indices and hold translation, rotation and scale channels with
step, linear or cubic interpolation. Cubic values retain glTF tangent/value/tangent order.
Imported clips also keep the rig they were made for (`AnimationClip::rig`), every node of their file, which lets
them play on other skeletons (see [Clips of other files](#clips-of-other-files)).

Meshes keep four joint indices and normalized weights in a separate optional stream.
Tangent generation and submesh assembly preserve that stream, including split vertices.
Model import scale affects bind translations, animation translations and inverse binds together.
Imported skeletons and clips load without a graphics device; corrupt formats are rejected.
Unsupported morph-weight channels are reported and skipped.

`Pose::Sample()` restores bind fields before sampling sparse channels, wraps negative looping time,
and clamps non-looping time. Rotations use shortest-path spherical interpolation; cubic rotations
normalize the Hermite result. Cubic tangents are multiplied by the segment's duration.
`Pose::Blend()` interpolates local TRS. `ToModel()` walks parents once; `Palette()` computes
inverse(mesh model) * bone model * inverse bind for each joint. All outputs are caller-owned spans.

`SkinnedMeshRenderer` holds a mesh, material, palette and a configurable bounds padding.
`Renderer3D::DrawSkinnedMesh()` is the code-only API. Four weights blend joint matrices
in a second vertex stream; palettes are uploaded together and indexed per instance.
Skinned variants cover opaque, cutout, transparent and mirrored surfaces, shadows and
readable camera depth, including MSAA. Normals use the blended matrix's cofactors and
tangents follow its linear transform. Increase bounds padding to cover the clip's motion.
Meshes and palettes must stay valid until submission; too-short palettes are rejected.

`Animator::SetSkeleton()` and `Play()` work before a scene starts. `CrossFade()` advances
both clips and blends local poses; interrupting a fade starts from the currently blended
pose. Speed can be negative, loops wrap, and non-looping playback holds its last pose.
The lazy `AnimationSystem` evaluates active animators after component updates under their
failure guard, then refreshes renderer palettes. Poses are worked out together on the job system's threads, since an
animator only touches its own state while advancing its clips, controller and layers and sampling them; root motion,
events and the renderers and bone attachments it drives then follow in order, on the scene's thread, with what each
animator drives gathered once per frame rather than looked for by each. Model instances create non-joint nodes
and an animator per skin, and use skinned renderers for skin-bearing nodes.

Select an Animator in the editor to preview with real time, pause, restart or seek in the
inspector. Preview changes only transient arrays, resolves component/entity ids each frame,
and does not update gameplay or create undo entries. Play mode uses its own scene copy.

Animator controllers are versioned `.controller` source assets, imported without linking
importers into games. A controller contains named typed parameters, states and ordered
transitions. Float, int, bool and trigger values belong to each runtime instance; integer
values retain their precision. Any-state transitions use source -1. Conditions combine
with AND; the first matching transition wins. Exit time is normalized and may exceed one
to wait multiple cycles. Non-interruptible transitions block new transitions until their
fade finishes. Triggers are consumed only when a transition actually uses them.

Set a controller with `Animator::SetController()`, then call `SetFloat()`, `SetInt()`,
`SetBool()` or `SetTrigger()` from gameplay. `AnimatorControllerRuntime` can also run
without scenes. Controllers hold clip references, and reload when those clips change.
Validation rejects unknown parameters, bad state targets and incompatible clip channels.

Blend states contain a `BlendTree`: unsorted 1D thresholds or 2D freeform directional
positions, float/int parameter names and clips with speed multipliers. 1D weights clamp
at endpoints. 2D weights use angular/radial gradient bands, supporting several speeds in
one direction and an optional idle at the origin. Weights are nonnegative and normalized.
A weighted duration keeps all clips at the same phase. Scratch poses and weights are
allocated when the rig/controller changes, and reused each frame. Controller importer
version 2 includes the trees and tracks their clip dependencies for reload.

Clips carry sorted named events with string payloads. glTF animation extras may supply
an `events` array; model import settings can override events by clip name, also in the
asset inspector. Events crossed by
the active clip (the dominant blend motion) reach the animator entity's active components
through guarded `OnAnimationEvent()`. Forward and reverse crossings include looping
boundaries; delivery is bounded to 256 events per update. Seeking/preview never dispatch.

Root motion can move the entity transform or its CharacterController, respecting collisions.
The default root is the topmost skin joint, such as the hips; choose another by name. The entity takes what the root
does along the ground: its translation across the up axis of the root's parent and, unless the clip's
`rootMotionRotation` is off, its turning about it (`GetRootMotionDelta()`). The rest stays in the visual pose
(`RemoveRootMotion()`): the hips rise and fall and lean as a character walks, without the entity bobbing or tilting.
Walks whose hips sway can turn `rootMotionRotation` off so the entity keeps facing ahead. Whole cycles accumulate
translation and turning without backwards jumps. Blend trees and crossfades blend deltas. Preview evaluates the
in-place pose without moving the entity. Character root motion changes position through `Move()`;
missing/disabled character controllers receive no positional motion.

An Animator can apply up to 16 ordered override/additive layers after its base pose.
Each layer has its own clip, clock, speed, weight and named bone mask. Masks can include
descendants; later mask entries override earlier weights. Empty masks affect every bone.
Override interpolates local TRS; additive applies bind-relative translation, rotation and
scale to the current pose. `SetLayers()` validates the whole configuration before replacing
it; `SetLayerWeight()` changes influence at runtime. Layer clips deliver their own guarded
events when weighted above zero. Pose/mask storage is reused, and root motion belongs to
the base animation. The inspector edits serialized layer settings and masks.

Create controllers from the asset browser after importing clips. The controller inspector
shows draggable state nodes, entry/any-state links, named transition endpoints, conditions,
blend settings and parameters. Edits remain a draft until Apply; Revert discards them and
Apply creates one undoable source edit. Removing a state remaps surviving transitions.
During Play, the Animator inspector edits live typed parameters and shows its current state.

Selecting a clip sub-asset shows key/event marks and a timeline. Select a rig in an edit-mode
scene to play or seek that clip; the override affects only transient poses and restores the
authored animation when cleared. Skinned meshes can be framed, picked and outlined at their
animated positions. These paths share the GPU palette with normal viewport rendering.

`BoneAttachment` places an entity at an animator's model-space bone, with a local TRS offset.
Use an animator entity id and bone name (or an explicit index for unnamed/duplicate bones).
With no target id it finds an animator in its parents. Attachments update after animation
and late gameplay, and previews refresh them too. Missing references restore the authored
pose and log once; targeting a descendant is rejected. Disabling/destroying releases the pose.

## Inverse kinematics

`Pose::SolveTwoBone()` and `Pose::SolveAim()` (`engine/spot/animation/inverse_kinematics.h`) change a local pose and
its model pose, without scenes. The two-bone solver is analytic: it bends the middle and upper joints in the plane
of the chain until the end is as far as the goal (just short of straight when it is out of reach), swings the chain
onto the goal, then twists it about the line to the goal toward the pole; a straight chain bends toward the pole.
The end can take the goal's rotation, and the weight slerps the new local rotations in. The aim solver works out the
direction the last bone should point once, limited by its maximum angle and scaled by its weight, then turns each
bone of the chain, parents first, by its weight of what is still left.

The components `AimIK` and `TwoBoneIK` (`scene/animation/`) apply them to an animator after it evaluates, through
`Animator::ApplyConstraints()`, which `AnimationSystem` calls in the serial part of its update, after root motion and
events and before palettes, since constraints read other entities' transforms. Aims run first, ordered by the index
of their first bone so a spine's aim comes before a neck's look; then bone attachments are synchronized, so what hangs
on a turned bone, as a weapon mounted on the chest, is where the limbs reach for it; then the two-bone constraints
run. Targets are entities (or, for aims, a world position set by code) brought into the animator's model space.
`Animator::EditPose()` is the public way to write such a constraint. Constraints run only on updates that evaluate
the animator, so ones with an update interval keep the constrained pose in between; the editor's preview and
ragdolls do not apply them. Tests: `inverse_kinematics_tests.cpp`.

`Transform::SetWorldOverride()` is the code-only mechanism for derived poses. World queries
and descendants see the override; local fields and scene serialization retain authored values.
Clearing it, or editing a local TRS value, restores normal hierarchy calculation. This keeps
bone previews visible without changing scene text or undo history.

Model meshes on or below joints receive bone attachments automatically, including meshes
without skin weights, so accessories follow the evaluated hierarchy.

`Ragdoll` builds box bodies and SwingTwistJoints from named bones, with per-bone dimensions,
centers, masses, axes and limits. Proxy bodies are scene roots and owned by entity ids, so
following the pelvis cannot move a body twice. `SetActive(true)` captures the current pose
and character velocity, pauses animation and disables the character controller. Physics
updates the contiguous pose before attachments in late update. `AddImpulse()` pushes the
nearest body. `SetActive(false, duration)` disables proxies, restores character movement
and blends from the physical pose back to animation. Saved body ids/mode restore without
creating duplicates; destroying the component releases its proxies.

## Clips of other files

Characters are often downloaded with their animations apart, a file each, as Mixamo gives them: those files hold the
skeleton without the mesh or the armature above it, under another namespace (`mixamorig1:Hips` for `mixamorig:Hips`),
so their node indices differ from the character's. `Animator` binds each clip to its skeleton once, when it is first
played, through `BindAnimationClip()` (`engine/spot/animation/animation_binding.h`): a clip of the skeleton's own
file, with the same bones, hierarchy and lengths (`IsClipForSkeleton()`), or one made in code without a rig, plays
as it is; any other is rewritten by `RetargetAnimationClip()` for that skeleton, and the copy is shared by every
animator that plays it on the same skeleton. Bones match by name, exactly or by their base names ignoring case
(`GetBoneBaseName()`), and clips with no bone of the skeleton are rejected with a warning.

Retargeting carries rotations over relative to each rig's bind pose (`key * inverse(sourceBind) * targetBind`), so
rigs whose rest orientations differ a little still line up. Exporters key the translation of every bone with the
lengths of the character the clip was made for; with `retargetTranslations`, on by default, only the topmost bones
whose translation changes, such as the hips, keep it, scaled by the ratio of their bind offsets so strides fit the
legs, and every other bone keeps the skeleton's length. Rigs that share bone names, as all of Mixamo's characters
do, share their animations this way; rigs named differently need a humanoid mapping the engine does not have yet.

The binding is per animator (`Animator::Bind()`), looked up by the clip's pointer on each evaluation and released on
the scene's thread when nothing else refers to the clip (`ReleaseUnusedBindings()`); blend trees take the bound
clips through `SampleBlendTree()`'s `clips`.

A model's clip settings (`ClipImportSettings` in its `.meta`) find a clip by its name in the file and rename it,
keep part of it (`start`, `end`, in seconds), set its events, `retargetTranslations` and `rootMotionRotation`.
Several settings for one clip split it into clips of their own, as a file with all of a character's moves in one take
needs; the first keeps the clip's key, `animation:N`, and the others are `animation:N:<name>`. Cut cubic channels are
resampled into linear keys.

## Update interval

`Animator::SetUpdateInterval()` (the inspector's `updateInterval`) updates the pose only every so many seconds; 0, the
default, updates it every frame. The time in between accumulates and is simulated at once, so clips, events and root
motion keep their pace. Characters far from the camera can update a few times a second for a fraction of the cost.

## Asset loader setup

Call `RegisterAnimationAssets(assets)` before loading skeletons, clips or animator controllers through a standalone
manager. Skeleton, clip and controller data decode on workers; controller clip references resolve through that manager
when the caller finishes the load. `Application` performs this registration during setup. Animated model loaders use
the same manager and require both animation and rendering registrations.

Animation event receivers derive from `AnimationEventListener` as well as `Component` and
override `OnAnimationEvent()`. Components without this interface are skipped when clips emit events.
