#include "gameplay/weapons/projectile.h"

#include "gameplay/core/body_authority.h"
#include "gameplay/core/trace_volume.h"
#include "gameplay/weapons/explosive.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(Projectile);

Projectile* Projectile::Launch(Spot::Scene& scene, const Spot::AssetRef<Spot::Prefab>& prefab, Spot::Vector3 position,
                               Spot::Vector3 direction, float speed, Spot::EntityId shooter) {
    Spot::Entity* entity = prefab ? scene.Instantiate(prefab) : nullptr;
    auto* projectile = entity ? entity->GetComponent<Projectile>() : nullptr;
    if (!projectile) {
        if (entity) {
            entity->Destroy();
        }
        return nullptr;
    }
    entity->GetTransform().SetPosition(position);
    entity->GetTransform().SetRotation(Spot::Quaternion::LookRotation(direction));
    projectile->shooter = shooter;
    BodyAuthority::KeepOnServer(*entity);
    if (auto* body = entity->GetComponent<Spot::Rigidbody>()) {
        body->SetLinearVelocity(Spot::Normalize(direction) * speed);
    }
    return projectile;
}

void Projectile::OnStart() {
    Invoke(std::max(0.0f, lifetime), [this] { Hit(GetEntity().GetTransform().GetWorldPosition()); });
}

void Projectile::OnFixedUpdate(float /*fixedDeltaTime*/) {
    // Where it flies is the server's, which also finds what it hits.
    if (!Spot::Network::HasAuthority(GetEntity())) {
        return;
    }
    const Spot::Vector3 position = GetEntity().GetTransform().GetWorldPosition();
    if (m_last) {
        const Spot::Vector3 travel = position - *m_last;
        const float length = Spot::Length(travel);
        if (length > 1e-4f) {
            if (const auto volume = Traces::Raycast(GetScene(), *m_last, travel / length, length)) {
                Hit(volume->point);
                return;
            }
        }
    }
    m_last = position;
}

void Projectile::OnCollisionEnter(const Spot::Collision& collision) {
    if (collision.other != shooter) {
        Hit(collision.pointCount > 0 ? collision.points[0] : GetEntity().GetTransform().GetWorldPosition());
    }
}

void Projectile::Hit(Spot::Vector3 point) {
    if (!Spot::Network::HasAuthority(GetEntity())) {
        return;
    }
    if (m_hit) {
        return;
    }
    m_hit = true;
    if (auto* explosive = GetEntity().GetComponent<Explosive>()) {
        explosive->Explode(point, shooter);
    } else {
        GetEntity().Destroy();
    }
}

void Projectile::Serialize(Spot::Archive& archive) {
    archive.Field("lifetime", lifetime, {.min = 0.0f, .max = 60.0f, .speed = 0.1f});
    archive.Field("shooter", shooter);
}

} // namespace Gameplay
