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

## Holding things and looking around

Animations play the same whatever the character holds or looks at. Two components change the pose after them:

- **AimIK** turns a chain of bones so the last one points at a target: list them from the top, as **Spine** then
  **Chest**, or **Neck** then **Head**. Each turns its **weight** of what is left, so a last weight of 1 aims exactly;
  **axis** is where the last bone faces (forward, -Z, for most) and **maxAngle** how far it turns at most.
- **TwoBoneIK** makes a bone and its two parents, as a hand and its arm or a foot and its leg, reach a target entity:
  a node of a held weapon's model, say. **pole** is where the elbow or knee points, seen from the bone above the
  chain; **offset** where the hand goes from the target.

Set their **target**, or, for an AimIK, a **target position** from code, and turn them on and off with their
**weight**. A character's aims turn the pose before its reaches, so a spine can aim a weapon held at the chest while
both hands hold it.

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

Players and NPCs hold weapons with both hands at the model’s grip nodes. Players bleed when shot and fall into a physical ragdoll when killed, including in multiplayer. Restoring their health returns them to their animated body and movement.

## Character locomotion poses

Player characters can blend forward, backward and sideways steps while walking, running or crouching. Holding Aim
raises the weapon from a low ready posture into a sighted posture; both hands follow its grip nodes. NPCs raise
their weapons when engaging a visible enemy.

To customize a character, assign its skeleton and compatible clips to the Animator, keep the player
controller's `forward`, `sideways` and `crouching` parameters, and configure CharacterVisual's model and animator.
The locomotion clips are in place because CharacterMovement moves the player. On WorldModel, enable `poseMount`
and tune `readyPosition`, `aimingPosition` and their pitches for the new model's arm length. Configure the mount's
BoneAttachment bone and each hand's TwoBoneIK bone, grip offset and elbow pole. Clips can use the animator's
name-based retargeting for skeletons with matching bone names; other naming conventions require compatible clips.

Jumping blends distinct rising and falling poses and returns to locomotion on landing. When adapting the locomotion
controller, preserve its `airborne` boolean and `vertical` float parameters alongside the directional parameters.
WorldModel lowers weapons during a sprint; tune `sprintPosition`, `sprintPitch` and `sprintState` for the character.
The third-person camera now sits closer over the shoulder. On ChaseCamera, `distance` and `shoulderOffset` set the
framing, `maxBoomPitch` keeps steep upward looks from driving the camera into the ground, and `followRate` controls
how quickly it returns after an obstacle clears.

For weapons of different shapes, enable `worldPose` on the held item's prefab. Author separate `ready`, `aiming`
and `sprint` positions and angles (pitch, yaw, roll). These positions place the firing hand in the mount bone's
space. The yaw lets the character turn the chest sideways while keeping the barrel aimed forward, leaving room
for a rifle or shotgun stock beside the neck. Use `supportOffset` to move the support contact along the handguard
or pump; the character's wrist offsets remain on its TwoBoneIK components. Disabled item profiles use the
WorldModel defaults. Check each posture from the front and side on the intended rig, including when crouching.
