#pragma once

#include "gameplay/core/input_context.h"

#include <spot.h>

#include <string>

namespace Gameplay {

class Pawn;

// Follows the pawn its entity belongs to from behind and above, as the chase camera of a vehicle: it swings around
// after the pawn as it turns, the look action orbits it around the pawn, and it comes back behind a moving pawn a
// moment after the mouse rests. It keeps out of walls between it and the pawn, and widens its view at speed, as the
// pawn's body moves. It does all this while its Camera is on, which the pawn's controller turns on while possessing it.
class ChaseCamera : public Spot::Component {
public:
    float distance = 5.8f;
    // Above the pawn's origin: where it looks, and how far it looks down at it, in radians.
    float lookHeight = 1.1f;
    float pitch = 0.2f;
    // How quickly it swings behind the pawn, per second.
    float followRate = 3.5f;
    // Radians per pixel the mouse orbits it.
    float lookSensitivity = 0.003f;
    // Seconds after the mouse rests before it swings back behind the pawn.
    float recenterDelay = 1.5f;
    // How much wider the view gets at 30 meters per second than standing, in radians.
    float speedFieldOfView = 0.17f;
    std::string lookAction = "Look";

protected:
    void OnStart() override;
    void OnDisable() override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Pawn* GetPawn() const;
    // Where the pawn heads, around Y.
    float GetPawnYaw() const;
    // Meters per second the pawn moves forward, negative backing up.
    float GetPawnSpeed() const;
    // Behind the pawn, as it starts following it.
    void Recenter();

    InputContext m_input{{.name = "Chase Camera", .actions = {"Look"}}};
    // Around Y, the angle it looks from, and how far Look has turned it from behind the pawn.
    float m_yaw = 0.0f;
    float m_orbitYaw = 0.0f;
    float m_orbitPitch = 0.0f;
    float m_sinceLook = 0.0f;
    float m_fieldOfView = 0.0f;
    bool m_following = false;
};

} // namespace Gameplay
