# Characters and animations

Spot imports models from FBX, glTF (`.gltf`, `.glb`) and OBJ files, with their skeletons, animations, materials and
textures. This guide follows a character downloaded from [Mixamo](https://www.mixamo.com) with a few of its
animations; characters from other tools come in the same way.

## Downloading from Mixamo

- **The character:** choose it, then **Download** with the format **FBX Binary (.fbx)** and the pose **T-pose**. Its
  textures are inside the file.
- **Each animation:** choose it on the same character, then **Download** with **FBX Binary (.fbx)** and skin
  **Without Skin**: the file holds only the animation, small and quick to import. Tick **In Place** for animations
  that should not move the character, such as an idle, or leave it off when the animation's own movement should move
  it (see [Root motion](#root-motion)).

Name the files after what they do, such as `Walking.fbx`: each animation is named after its file.

## Importing

Drop the files on the editor's **Assets** panel, or copy them into the project's `assets/` folder. Spot converts them
to meters with Y up, whatever they were made in, so Mixamo's centimeters need no scale. Each file shows its parts
under it: the character's meshes, materials, textures and skeleton, and each animation file's clip.

## Materials

Select the character's file. Its **Import Settings** list its materials:

- **Extract Textures** writes the textures the file holds to a `Textures` folder beside it, as image files you can
  edit.
- **Extract Materials** writes its materials to a `Materials` folder beside it, as `.material` files you can edit;
  the character uses them from then on. Extract the textures first, so the materials use the files.
- Each material has a slot: drop any material there, and **Apply**, to use it instead of the file's own.
- **Remap by Name** uses the project's material files whose names are those of the character's materials, such as
  for another copy of the same character.

## Putting the character in a scene

Drag the character's file into a scene view: it comes in with its skeleton and an **Animator**. Drag an animation
clip onto the character to see it play; it plays on any character whose bones have the same names, as all of
Mixamo's do, even those of other sizes: each keeps its own proportions.

## Animator controllers

Select the animation files, or their clips, in the **Assets** panel, then choose **Create > Animator Controller**
from the panel's toolbar, or by right-clicking an empty spot of it: the controller gets a state for each. Select it to add transitions and parameters, or a blend tree that blends Idle, Walk and Run by speed,
then set it on the character's Animator.

## Root motion

An animation downloaded without **In Place** moves its hips across the ground. Set the Animator's **Root Motion** to
**Transform**, or **Character controller** for a character with a `CharacterController`, and that movement moves the
character instead, so its feet do not slide; the hips' rise and fall and lean stay in the animation.

When the hips sway from side to side as the character walks, the character turns with them. Select the animation's
file and, in its **Import Settings**, add an entry to **animations** with the clip's name and **rootMotionRotation**
off: the turning then stays in the animation, and the character keeps facing ahead.

## Clip settings

Each entry of a file's **animations** settings finds a clip by its name in the file:

| Setting | What it does |
|---|---|
| `name` | Another name for the clip |
| `start`, `end` | Keeps only that part of it, in seconds; an end of 0 keeps the rest |
| `events` | Events at times of the clip, which components receive as it plays |
| `retargetTranslations` | On other characters, each bone keeps that character's length; off plays every bone's movement as the file has it |
| `rootMotionRotation` | Root motion turns the character; off leaves the turning in the animation |

Several entries for the same clip split it into clips of their own, as for a file with all of a character's moves in
one long animation.

## What is not imported

Blend shapes, such as facial expressions, and lights and cameras in the file are skipped. Opacity textures of their
own are not supported: put the transparency in the base color's alpha.
