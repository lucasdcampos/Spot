#pragma once

#include <spot.h>

class Car;

// Use gets the player into the Car it looks at within reach, and Use again out of it. Driving, Move works the pedals,
// forward to accelerate and back to brake and reverse, and the wheel, and Jump pulls the handbrake. The car's camera
// takes over from the eyes, and the player rides in its seat with what it does on foot put away: its movement, look,
// hands and tools. Getting out puts it beside the driver's door, or wherever around the car there is room, looking the
// way the car faces and moving as the car moved.
class CarDriver final : public Spot::Component {
public:
    float reach = 3.0f;

    bool IsDriving() const { return m_car != Spot::EntityId::Invalid; }
    // The car driven; null on foot, or when it is gone.
    Car* GetCar() const;
    // The car the player looks at that Use would get into, if any.
    Car* FindCar() const;
    void Enter(Car& car);
    void Exit();
    // Whether Use got in or out this frame, so it does nothing else.
    bool TookUse() const { return m_tookUse; }

protected:
    void OnDisable() override;
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // Brings back what the player does on foot, or puts it away, the eyes' camera included.
    void SetOnFoot(bool onFoot);
    // Where around the car the player fits, beside the driver's door first; above it when nowhere else.
    Spot::Vector3 FindExit(const Car& car) const;

    Spot::EntityId m_car = Spot::EntityId::Invalid;
    bool m_tookUse = false;
};
