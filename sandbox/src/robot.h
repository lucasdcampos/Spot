#pragma once

#include <spot.h>
#include <spot/scene/animation/animation_event_listener.h>

#include "gameplay/health/damage.h"

#include <string>

// Patrols back and forth along an axis, stepping like the metal it is, and falls as a ragdoll to whatever damages it,
// pushed by the damage, until revived.
class Robot final : public Spot::Component, public Spot::AnimationEventListener, public Gameplay::DamageReceiver {
public:
    Spot::Vector3 patrolAxis = {0, 0, 1};
    float patrolDistance = 3;
    float turnSpeed = 4;
    // Within the near distance of the camera, the pose updates every frame; beyond it 30 times a second, and beyond
    // the far distance 12 times, which is plenty for a robot that small on screen.
    float animationNear = 12;
    float animationFar = 30;
    // The kind of the SurfaceLibrary its footsteps sound like.
    std::string footstepSurface = "Metal";
    bool IsDead() const { return m_dead; }
    std::uint32_t GetFootsteps() const { return m_footsteps; }
    void Hit(Spot::Vector3 impulse, Spot::Vector3 point);
    void Revive();

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnFixedUpdate(float deltaTime) override;
    void OnAnimationEvent(const Spot::AnimationEvent& event) override;
    void OnDamage(const Gameplay::DamageInfo& damage) override;
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
