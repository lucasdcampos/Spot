#pragma once

#include <spot.h>

namespace Gameplay {

// The rules permit changing time only while the world is private. A host that gains a peer resumes normal time,
// including when it was paused before the connection. Menus, wheels and console commands share this policy.
class WorldTime final : public Spot::SceneSystem {
public:
    static WorldTime& Get(Spot::Scene& scene);
    static bool IsScalingAllowed(const Spot::Scene& scene);
    void SetAllowed(bool allowed) { m_allowed = allowed; }

protected:
    void OnBeforeFixedSteps(float deltaTime) override;

private:
    bool m_allowed = true;
};

} // namespace Gameplay
