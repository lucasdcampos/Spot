#include "projectile.h"

#include "explosion.h"

#include <algorithm>

SPOT_COMPONENT(Projectile);

Spot::Entity& Projectile::Launch(Spot::Scene& scene, Spot::Vector3 position, Spot::Vector3 direction, float speed,
                                 Spot::EntityId shooter) {
    constexpr float Diameter = 0.25f;
    auto& rocket = scene.CreateEntity("Rocket");
    rocket.GetTransform().SetPosition(position);
    rocket.GetTransform().SetScale({Diameter, Diameter, Diameter});
    rocket.AddComponent<Spot::MeshRenderer>(Spot::Assets::Load<Spot::Mesh>(Spot::BuiltInAssets::SphereMesh));
    rocket.AddComponent<Spot::SphereCollider>();
    auto& body = rocket.AddComponent<Spot::Rigidbody>();
    body.SetGravityScale(0.0f);
    // Fast enough to pass through thin walls between two steps otherwise.
    body.SetContinuous(true);
    body.SetLinearVelocity(Spot::Normalize(direction) * speed);
    rocket.AddComponent<Projectile>().shooter = shooter;
    return rocket;
}

void Projectile::OnStart() {
    Invoke(std::max(0.0f, lifetime), [this] { Explode(GetEntity().GetTransform().GetWorldPosition()); });
}

void Projectile::OnCollisionEnter(const Spot::Collision& collision) {
    if (collision.other != shooter) {
        Explode(collision.pointCount > 0 ? collision.points[0] : GetEntity().GetTransform().GetWorldPosition());
    }
}

void Projectile::Serialize(Spot::Archive& archive) {
    archive.Field("explosionRadius", explosionRadius, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("explosionImpulse", explosionImpulse, {.min = 0.0f, .max = 100000.0f, .speed = 1.0f});
    archive.Field("explosionDamage", explosionDamage,
                  {.min = 0.0f, .max = 10000.0f, .speed = 1.0f, .tooltip = "To a player at the center"});
    archive.Field("lifetime", lifetime, {.min = 0.0f, .max = 60.0f, .speed = 0.1f});
    archive.Field("shooter", shooter);
}

void Projectile::Explode(Spot::Vector3 center) {
    if (m_exploded) {
        return;
    }
    m_exploded = true;
    GetEntity().Destroy();
    Explosion::Explode(GetScene(), center, explosionRadius, explosionImpulse, explosionDamage, GetEntity().GetId());
}
