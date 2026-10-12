# Skeletal animation

Start with `ModelData::Load()` for CPU data, or load imported `Skeleton` and `AnimationClip`
assets through `AssetManager`. glTF skins become skeleton sub-assets and named animations
become clip sub-assets, also for animation-only glTF files. The model importer is version 10.

Skeleton bones are contiguous, parent before child, and include intermediary nodes needed
for transforms. The joint array preserves glTF skin order; inverse bind matrices follow it.
Clips address the same node indices and hold translation, rotation and scale channels with
step, linear or cubic interpolation. Cubic values retain glTF tangent/value/tangent order.
Animation-only files must use the same node hierarchy as their destination rig.

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
failure guard, then refreshes renderer palettes. Model instances create non-joint nodes
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
asset inspector. Model importer version 11 includes those settings. Events crossed by
the active clip (the dominant blend motion) reach the animator entity's active components
through guarded `OnAnimationEvent()`. Forward and reverse crossings include looping
boundaries; delivery is bounded to 256 events per update. Seeking/preview never dispatch.

Root motion can move the entity transform or its CharacterController, respecting collisions.
The default root is the first skin joint; choose another by name. Whole cycles accumulate
translation and turning without backwards jumps. Blend trees and crossfades blend deltas;
root translation/rotation are removed from the visual pose. Preview evaluates the in-place
pose without moving the entity. Character root motion changes position through `Move()`;
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
