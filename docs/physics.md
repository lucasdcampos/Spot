# Physics

Physics simulates colliders and rigidbodies with Jolt, behind an engine contract
(`engine/physics/physics_simulation.h`) whose only backend is `engine/physics/jolt/`. Games only see components
and the `PhysicsWorld`.

## Components

- `BoxCollider`, `SphereCollider` and `CapsuleCollider` give their entity a solid shape, with a center, friction and
  restitution. Shapes scale with the entity: spheres take the largest scale, capsules stand along Y and take the
  largest of the X and Z scales for their radius.
- `Rigidbody` makes the entity's body move. **Dynamic** bodies fall with gravity and are pushed by forces and
  collisions; **kinematic** ones (`SetKinematic()`) move with their entity's transform and push dynamic bodies away
  without being pushed back. It has a mass, a gravity scale, linear and angular damping, velocities, forces and
  impulses.

```cpp
auto& ball = scene.CreateEntity("Ball");
ball.AddComponent<Spot::SphereCollider>();
ball.AddComponent<Spot::Rigidbody>().SetMass(2.0f);
```

The active colliders of an entity form **one body** with the entity's world pose. Without a `Rigidbody`, that body is
static: it stays where its entity is placed, like floors and walls.

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

## Not there yet

Triggers, collision events, raycasts and other queries, a character controller, mesh colliders and joints come in
the rest of the roadmap's milestone M1.
