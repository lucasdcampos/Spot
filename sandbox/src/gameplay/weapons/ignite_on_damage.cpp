#include "gameplay/weapons/ignite_on_damage.h"

#include "gameplay/weapons/explosive.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(IgniteOnDamage);

void IgniteOnDamage::Ignite() {
    if (!IsLit()) {
        m_lit = true;
        ScheduleFuse(fuse);
    }
}

void IgniteOnDamage::OnDamage(const DamageInfo& /*damage*/) {
    Ignite();
}

void IgniteOnDamage::OnStart() {
    if (m_lit && !IsInvokePending(m_fuse)) {
        ScheduleFuse(m_savedFuse >= 0.0f ? m_savedFuse : fuse);
    }
}

void IgniteOnDamage::ScheduleFuse(float seconds) {
    m_fuse = Invoke(std::max(0.0f, seconds), [this] { Explode(); });
}

void IgniteOnDamage::Explode() {
    if (auto* explosive = GetEntity().GetComponent<Explosive>()) {
        explosive->Explode();
    } else {
        GetEntity().Destroy();
    }
}

void IgniteOnDamage::Serialize(Spot::Archive& archive) {
    archive.Field("fuse", fuse, {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "Seconds before it explodes"});
    if (archive.IsSaving()) {
        m_savedFuse = IsLit() ? GetInvokeTimeLeft(m_fuse) : -1.0f;
    }
    archive.Field("lit", m_lit, {.hidden = true});
    archive.Field("fuseLeft", m_savedFuse, {.hidden = true});
}

} // namespace Gameplay
