# Physics

Physics simulates colliders and rigidbodies with Jolt, behind an engine contract
(`engine/physics/physics_simulation.h`) whose only backend is `engine/physics/jolt/`. Games only see components
and the `PhysicsWorld`.

## Components

- `BoxCollider`, `SphereCollider` and `CapsuleCollider` give their entity a solid shape, with a center, friction and
  restitution. Shapes scale with the entity: spheres take the largest scale, capsules stand along Y and take the
  largest of the X and Z scales for their radius.
- `MeshCollider` takes the shape of a mesh: its triangles, for level geometry, or the convex hull around its vertices
  (`SetConvex()`), for props that move. Dynamic bodies always get the hull, as Jolt cannot simulate triangle meshes
  that move freely. The mesh is an `AssetRef<MeshData>`: a mesh asset's triangles, which
  `AssetManager::LoadMeshData()` (or `Load<MeshData>()`) reads without a graphics device, or a `MeshData` made in
  code. The backend builds each mesh's shape once and shares it while the mesh lives.
- `Rigidbody` makes the entity's body move. **Dynamic** bodies fall with gravity and are pushed by forces and
  collisions; **kinematic** ones (`SetKinematic()`) move with their entity's transform and push dynamic bodies away
  without being pushed back. It has a mass, a gravity scale, linear and angular damping, velocities, forces and
  impulses, applied at the center of mass or at a point (`AddForceAtPosition()`, `AddImpulseAtPosition()`), and
  torques (`AddTorque()`). It can lock rotations around the world's axes (`SetRotationLocks()`), sweep through each
  step so a fast body cannot pass through thin ones (`SetContinuous()`), and be put to sleep, woken up or kept from
  ever sleeping (`Sleep()`, `WakeUp()`, `SetCanSleep()`).

```cpp
auto& ball = scene.CreateEntity("Ball");
ball.AddComponent<Spot::SphereCollider>();
ball.AddComponent<Spot::Rigidbody>().SetMass(2.0f);
```

The active colliders of an entity form **one body** with the entity's world pose. Without a `Rigidbody`, that body is
static: it stays where its entity is placed, like floors and walls.

A collider set as a **trigger** (`SetTrigger()`) is not solid: it detects the bodies that overlap it and never pushes
them. The trigger colliders of an entity form a sensor body of their own, beside its solid body, so an entity can
have both. The sensor follows the entity: static when the entity has no rigidbody, kinematic otherwise, moved to the
entity's pose each step.

## Joints

A `Joint` joins its entity's body to another entity's (`SetConnectedEntity()`), or to the world, at an anchor
relative to the entity, as the bodies are when the joint is made:

| Joint | What it does |
|---|---|
| `FixedJoint` | Holds the bodies together as they are, as a weld |
| `HingeJoint` | Lets them turn around an axis, within limits (`SetLimits()`), driven by a motor (`SetMotor()`) |
| `DistanceJoint` | Keeps two points within a range of distances, as a rope |
| `SpringJoint` | Pulls two points toward a rest length with a spring of a frequency and damping |
| `SwingTwistJoint` | Lets the bodies turn around a point, swinging in a cone and twisting within limits, as ragdolls' bones |

Angles and motors are those of the entity turning relative to what it is joined to. A joint with a break force
(`SetBreakForce()`, in newtons) breaks once holding its bodies takes more: it is disabled, `IsBroken()` turns true and
the entity's components get `OnJointBreak()`; enabling it mends it. `GetForce()` tells how hard it held in the last
step. Joints are made again whenever they or one of their bodies change, since Jolt's constraints cannot outlive their
bodies; a joint whose bodies do not exist yet waits for them.

## Characters

A `CharacterController` moves its entity as a character walks, under the game's control rather than by forces: a
capsule standing along Y, which slides along what it hits, climbs steps up to its step height, keeps to the ground
going down slopes and steps, stands on slopes up to its slope limit and pushes dynamic bodies out of its way. The game
gives it a velocity each update, gravity included, and asks where it stands:

```cpp
m_fall = controller.IsGrounded() ? 0.0f : m_fall - 9.81f * deltaTime;
controller.Move(walk + Spot::Vector3{0.0f, m_fall, 0.0f}, deltaTime);
```

`IsGrounded()`, `GetGroundNormal()`, `GetGroundVelocity()`, `GetGround()` and `GetVelocity()` describe the last move.
It is Jolt's `CharacterVirtual`, with a kinematic inner body of the same capsule, so dynamic bodies, triggers and
queries see the character. It collides with the layers its entity's layer does, gets collision and trigger events
like a body, and follows its entity when code places it elsewhere. The entity needs no collider or rigidbody.

## Layers

Every entity is on one of 32 layers (`Entity::GetLayer()`, `SetLayer()`; 0 by default), saved with its scene. Games
name the layers they use and decide which collide with which in the physics settings section: `PhysicsSettings`
holds the names (`layerNames`, "Default" for layer 0) and a symmetric matrix (`layerCollisions`, a bit mask per
layer), which the editor's Project Settings panel edits as a list of names and a triangle of checkboxes. Applied to
a scene, they reach its `PhysicsWorld`, which can also change them at run time (`SetLayerCollision()`,
`SetLayerMask()`) and finds layers by name (`FindLayer()`):

```cpp
const int props = *scene.GetPhysics().FindLayer("Props");
scene.GetPhysics().SetLayerCollision(props, props, false);
```

The backend turns each layer into two Jolt object layers, for static and moving bodies, so static bodies still never
test each other. Tags stay for finding entities; layers are what systems filter by.

## Collision and trigger events

After each step, `PhysicsWorld` turns the contacts between bodies into events for the components of both entities,
once per pair of entities however many of their shapes touch (see `engine/physics/collision.h`):

| Hook | When |
|---|---|
| `OnCollisionEnter(const Collision&)` | Two solid bodies start touching |
| `OnCollisionStay(const Collision&)` | They keep touching, on each step while either is awake |
| `OnCollisionExit(const Collision&)` | They stop touching, or one of them goes away (destroyed, disabled, its colliders removed) |
| `OnTriggerEnter(const TriggerContact&)`, `OnTriggerExit()` | A body starts or stops overlapping a trigger, told to both entities |

A `Collision` has the other entity, up to four contact points, the normal pointing toward the entity that receives
it, its velocity relative to the other's, and an estimate of the impulse of the hit from the bodies' masses and how
fast they met, which suits impact sounds. A `TriggerContact` has the other entity and whether the receiver's trigger
is the one overlapped. Bodies of the same entity, and two triggers, report nothing.

The backend records contacts as Jolt reports them from its threads, and ends them itself after each step: Jolt
reports contacts as removed when a body falls asleep, but resting bodies keep touching, so a contact ends only when
Jolt no longer finds it and neither body sleeps. Pairs are compared with the step before through sorted lists that
keep their memory, so events cost no allocation once warmed up. Hooks run under the component guard, and may destroy
entities or change colliders; changes show from the next step.

## Queries

`PhysicsWorld` answers questions about the bodies (see `engine/physics/physics_query.h`):

| Query | Finds |
|---|---|
| `Raycast()` | The first body a ray meets within a distance |
| `RaycastAll()` | Every collider a ray meets, nearest first |
| `SphereCast()` | The first body a sphere moving along a direction meets, as a thick ray |
| `OverlapSphere()`, `OverlapBox()` | The colliders overlapping a sphere, or a box of a size and rotation |

```cpp
if (auto hit = scene.GetPhysics().Raycast(eye, forward, 100.0f, {.ignore = player.GetId()})) {
    // hit->entity, hit->collider, hit->point, hit->normal, hit->distance
}
```

Each takes a `QueryFilter`: a mask of the layers it sees, whether triggers count (not by default), and an entity to
go through, such as the one asking. Hits name the entity and the collider. Queries see the bodies of the last step,
rebuilt first where colliders or rigidbodies changed since, and never wake bodies.

## The physics world

`PhysicsWorld` (`engine/physics/physics_world.h`) is the scene's physics system (see
[Application and scenes](application_and_scenes.md)): `Scene::GetPhysics()` creates it the first time it is needed,
which colliders and rigidbodies do, so scenes without physics never pay for it. It has the scene's gravity
(`SetGravity()`) and reports how many bodies it simulates.

It steps on each of the scene's fixed steps, after the components' `OnFixedUpdate()`:

1. Bodies whose colliders or rigidbody changed are rebuilt, in id order, so runs are reproducible.
2. Bodies follow the entities that something else moved since the last step; kinematic bodies are moved toward their
   entity's pose over the step.
3. The simulation steps.
4. Entities follow their dynamic bodies.

Code that pushes rigidbodies, such as applying forces, goes in `OnFixedUpdate()`, so it behaves the same at any frame
rate. The fixed time step and gravity come from the game's physics settings section (`PhysicsSettings`), which the
editor's Project Settings panel edits.

## Seeing what physics does

`PhysicsWorld::SetDebugDraw()` makes the world draw, with `DebugDraw`, before each frame: the colliders, character
controllers and joints (`PhysicsDebugFlags::Colliders`), where bodies touch with the normals pushing them apart
(`Contacts`), and the rays, casts and overlaps the game asked since the last frame, with what they hit (`Queries`).
Static bodies are green, dynamic ones orange, or blue while asleep, kinematic ones purple, triggers yellow,
characters cyan and joints white. `DrawColliders()` adds the same shapes to any `DebugLines` from the components
alone, which is how the editor's scene views show colliders (the shapes button in their toolbar) in scenes whose
physics never ran. The sandbox switches the debug view with B.
