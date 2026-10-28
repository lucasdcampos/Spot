#pragma once

#include <spot.h>
#include <vector>

namespace Gameplay {

// Articulated branches share their parent's motion and add independent turbulent bending.
class WindSway : public Spot::Component {
public:
    Spot::AssetRef<Spot::Skeleton> skeleton;
    void AddWind(Spot::Vector3 wind) { m_wind += wind; }

protected:
    void OnStart() override;
    void OnLateUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    std::vector<Spot::EntityId> m_visuals;
    std::vector<Spot::TransformComponents> m_local;
    std::vector<Spot::Matrix4> m_model;
    std::vector<Spot::Matrix4> m_palette;
    Spot::Vector3 m_wind{};
    Spot::Vector3 m_force{};
    float m_time = 0.0f;
};

} // namespace Gameplay
