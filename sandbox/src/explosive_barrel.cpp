#include "explosive_barrel.h"

#include "explosion.h"

#include <algorithm>

SPOT_COMPONENT(ExplosiveBarrel);

void ExplosiveBarrel::Ignite() {
    if (!IsLit()) {
        m_lit = true;
        ScheduleFuse(fuse);
    }
}

void ExplosiveBarrel::OnStart() {
    if (m_lit && !IsInvokePending(m_fuse)) {
        ScheduleFuse(m_savedFuse >= 0.0f ? m_savedFuse : fuse);
    }
}

void ExplosiveBarrel::ScheduleFuse(float seconds) {
    m_fuse = Invoke(std::max(0.0f, seconds), [this] { Explode(); });
}

void ExplosiveBarrel::Explode() {
    const Spot::Vector3 center = GetEntity().GetTransform().GetWorldPosition();
    GetEntity().Destroy();
    Explosion::Explode(GetScene(), center, radius, impulse, damage, GetEntity().GetId());
}

void ExplosiveBarrel::Serialize(Spot::Archive& archive) {
    archive.Field("radius", radius, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("impulse", impulse, {.min = 0.0f, .max = 100000.0f, .speed = 1.0f, .tooltip = "At the center"});
    archive.Field("damage", damage,
                  {.min = 0.0f, .max = 10000.0f, .speed = 1.0f, .tooltip = "To a player at the center"});
    archive.Field("fuse", fuse, {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "Seconds before it explodes"});
    if (archive.IsSaving()) {
        m_savedFuse = IsLit() ? GetInvokeTimeLeft(m_fuse) : -1.0f;
    }
    archive.Field("lit", m_lit, {.hidden = true});
    archive.Field("fuseLeft", m_savedFuse, {.hidden = true});
}
