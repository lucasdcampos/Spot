#pragma once

#include <spot.h>

#include <optional>

// A rocket, which explodes where it hits something other than whoever fired it, water included, or once its time
// is up.
class Projectile final : public Spot::Component {
public:
    float explosionRadius = 5.0f;
    float explosionImpulse = 400.0f;
    float explosionDamage = 110.0f;
    float lifetime = 5.0f;
    Spot::EntityId shooter = Spot::EntityId::Invalid;

    // Fires a rocket from the point along the direction.
    static Spot::Entity& Launch(Spot::Scene& scene, Spot::Vector3 position, Spot::Vector3 direction, float speed,
                                Spot::EntityId shooter);

protected:
    void OnStart() override;
    void OnFixedUpdate(float fixedDeltaTime) override;
    void OnCollisionEnter(const Spot::Collision& collision) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void Explode(Spot::Vector3 center);

    bool m_exploded = false;
    // Where it was a step ago, to tell when it went into water, which has no collider.
    std::optional<Spot::Vector3> m_last;
};
