#include "explosive_barrel.h"

#include "explosion.h"

SPOT_COMPONENT(ExplosiveBarrel);

void ExplosiveBarrel::Ignite() {
    if (!IsLit()) {
        m_fuseLeft = fuse;
    }
}

void ExplosiveBarrel::OnUpdate(float deltaTime) {
    if (!IsLit()) {
        return;
    }
    m_fuseLeft -= deltaTime;
    if (m_fuseLeft > 0.0f) {
        return;
    }
    const Spot::Vector3 center = GetEntity().GetTransform().GetWorldPosition();
    GetEntity().Destroy();
    Explosion::Explode(GetScene(), center, radius, impulse, GetEntity().GetId());
}

void ExplosiveBarrel::Serialize(Spot::Archive& archive) {
    archive.Field("radius", radius, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("impulse", impulse, {.min = 0.0f, .max = 100000.0f, .speed = 1.0f, .tooltip = "At the center"});
    archive.Field("fuse", fuse, {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "Seconds before it explodes"});
}
