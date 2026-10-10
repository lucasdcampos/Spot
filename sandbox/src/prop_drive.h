#pragma once

#include <spot.h>

// Drives props toward where the player's tools hold them, as the carrier and the physics gun do, through their
// rigidbodies' velocities, so they still collide with what is in the way.
namespace PropDrive {

// Gives the body the velocity that brings the point of it toward the target within a fraction of a second, up to a
// speed, and the angular velocity that turns it toward the rotation.
void Toward(Spot::Rigidbody& body, Spot::Vector3 point, Spot::Vector3 target, const Spot::Quaternion& rotation,
            float deltaTime, float maxSpeed);

// The rotation around Y alone of where the player looks, which held props keep their rotation relative to.
Spot::Quaternion GetFacing(float yaw);

// Turns the rotation by mouse movement in pixels: sideways around Y, and up and down around the view's right.
Spot::Quaternion TurnByMouse(const Spot::Quaternion& rotation, Spot::Vector2 mouseDelta, float sensitivity);

} // namespace PropDrive
