# Mirrors

Add a `PlanarMirror` component to an entity to show the scene reflected in a flat rectangle.
The rectangle lies in the entity's local XY plane and faces local +Z. Set its width and height
with the entity's X and Y scale, then rotate it to face into the room. Place it slightly in
front of its wall so the wall does not cover it. A frame can be made from ordinary meshes.

The mirror updates only when the main camera is in front of it, within `maxDistance` (12 world
units by default), and the rectangle is in view. `resolution` sets each side of the reflection
texture, defaulting to 512 pixels. Lower it to reduce the cost. Every visible mirror draws an
additional view of the scene, so keep the number of nearby mirrors small.

`reflectionMask` chooses which entity layers appear in the reflection. Its default includes
all layers, allowing a player's body to appear even if the main camera hides that layer.
The reflection updates with moving objects and animated characters. Other mirrors are not
reflected recursively. Reflections omit shadow maps and expensive screen effects.


For a material fallback outside the capture distance, place a thin mesh just behind the mirror surface.
The workshop uses its chrome material this way. Its mirror excludes layer 29, which holds both this backing
and the local first-person arms and weapon; the full player body remains on layer 30.
