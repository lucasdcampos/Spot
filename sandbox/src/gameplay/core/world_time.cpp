#include "gameplay/core/world_time.h"

namespace Gameplay {

WorldTime& WorldTime::Get(Spot::Scene& scene) {
    return scene.GetOrAddSystem<WorldTime>();
}

bool WorldTime::IsScalingAllowed(const Spot::Scene& scene) {
    const WorldTime* policy = scene.GetSystem<WorldTime>();
    if (policy && !policy->m_allowed) {
        return false;
    }
    const Spot::NetworkSystem* network = Spot::Network::Get(scene);
    return !network || (network->IsServer() && network->GetSession()->GetHost().GetPeers().empty());
}

void WorldTime::OnBeforeFixedSteps(float /*deltaTime*/) {
    if (!IsScalingAllowed(GetScene()) && Spot::Time::GetTimeScale() != 1.0f) {
        Spot::Time::SetTimeScale(1.0f);
    }
}

} // namespace Gameplay
