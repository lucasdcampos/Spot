#pragma once

#include <spot.h>

class Robot final : public Spot::Component {
public:
    Spot::Vector3 patrolAxis = {0, 0, 1};
    float patrolDistance = 3;
    float turnSpeed = 4;
    // Within the near distance of the camera, the pose updates every frame; beyond it 30 times a second, and beyond
    // the far distance 12 times, which is plenty for a robot that small on screen.
    float animationNear = 12;
    float animationFar = 30;
    bool IsDead() const { return m_dead; }
    std::uint32_t GetFootsteps() const { return m_footsteps; }
    void Hit(Spot::Vector3 impulse, Spot::Vector3 point);
    void Revive();

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnFixedUpdate(float deltaTime) override;
    void OnAnimationEvent(const Spot::AnimationEvent& event) override;
    void Serialize(Spot::Archive& archive) override;

private:
    bool m_dead = false;
    bool m_hasHome = false;
    bool m_positive = false;
    Spot::Vector3 m_home;
    float m_pause = 0;
    float m_fall = 0;
    std::uint32_t m_footsteps = 0;
};

namespace Robots {
bool Hit(Spot::Scene& scene, Spot::EntityId entity, Spot::Vector3 impulse, Spot::Vector3 point);
void Explode(Spot::Scene& scene, Spot::Vector3 center, float radius, float impulse);
} // namespace Robots
