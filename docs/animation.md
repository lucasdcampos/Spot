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
