#pragma once

#include <spot.h>

namespace Gameplay {

// Drives bodies toward where a pawn's tools hold them, as a carrier or a physics gun does, through their rigidbodies'
// velocities, so they still collide with what is in the way.
namespace PhysicsDrive {

// Gives the body the velocity that brings the point of it toward the target within a fraction of a second, up to a
// speed, and the angular velocity that turns it toward the rotation.
void Toward(Spot::Rigidbody& body, Spot::Vector3 point, Spot::Vector3 target, const Spot::Quaternion& rotation,
            float deltaTime, float maxSpeed);

// The rotation around Y alone of where a pawn looks, which held bodies keep their rotation relative to.
Spot::Quaternion GetFacing(float yaw);
// The yaw of a direction: the angle around Y from -Z toward -X.
float GetYaw(Spot::Vector3 direction);

// Turns the rotation by the Look action's movement, in a mouse's pixels with up positive: sideways around Y, and up and
// down around the view's right.
Spot::Quaternion TurnByLook(const Spot::Quaternion& rotation, Spot::Vector2 look, float sensitivity);

} // namespace PhysicsDrive

} // namespace Gameplay
