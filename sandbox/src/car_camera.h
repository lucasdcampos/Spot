#pragma once

#include <spot.h>

class Car;

// Follows the Car its entity belongs to from behind and above, as a chase camera: it swings around after the car as
// it turns, Look orbits it around the car, and it comes back behind a moving car a moment after the mouse rests. It
// keeps out of walls between it and the car, and widens its view at speed. The car makes it active while driven.
class CarCamera final : public Spot::Component {
public:
    float distance = 5.8f;
    // Above the car's origin: where it looks, and how far it looks down at it, in radians.
    float lookHeight = 1.1f;
    float pitch = 0.2f;
    // How quickly it swings behind the car, per second.
    float followRate = 3.5f;
    // Radians per pixel the mouse orbits it.
    float lookSensitivity = 0.003f;
    // Seconds after the mouse rests before it swings back behind the car.
    float recenterDelay = 1.5f;
    // How much wider the view gets at 30 meters per second than standing, in radians.
    float speedFieldOfView = 0.17f;

protected:
    void OnStart() override;
    void OnEnable() override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    Car* GetCar() const;
    // Where the car heads, around Y.
    float GetCarYaw() const;

    // Around Y, the angle it looks from, and how far Look has turned it from behind the car.
    float m_yaw = 0.0f;
    float m_orbitYaw = 0.0f;
    float m_orbitPitch = 0.0f;
    float m_sinceLook = 0.0f;
    float m_fieldOfView = 0.0f;
};
