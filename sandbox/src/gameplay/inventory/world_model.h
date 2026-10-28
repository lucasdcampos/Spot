#pragma once

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

// Shows the selected item's model at an authored mount. The prefab supplies the mount's bone attachment and any
// holding animation layer, so neither a character skeleton nor a particular item is required by the framework.
class WorldModel : public Spot::Component {
public:
    Spot::EntityId mount = Spot::EntityId::Invalid;
    Spot::ComponentRef<Spot::Animator> animator;
    // An optional existing layer whose weight follows whether an item is shown.
    std::string poseLayer;
    std::string muzzleNode = "Muzzle";
    std::vector<std::string> hiddenWhen = {"Dead", "Seated"};
    // A presentation layer for all model parts; -1 inherits the owning entity's layer.
    int renderLayer = -1;

    Spot::Entity* GetModelEntity() const;
    const Spot::AssetRef<Spot::Model>& GetShownModel() const { return m_shown; }
    Spot::Vector3 GetMuzzlePosition() const;
    void Synchronize();

protected:
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void OnDestroy() override;
    void Serialize(Spot::Archive& archive) override;

private:
    void Clear();
    Spot::EntityId m_instance = Spot::EntityId::Invalid;
    Spot::AssetRef<Spot::Model> m_shown;
};

} // namespace Gameplay
