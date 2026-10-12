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
