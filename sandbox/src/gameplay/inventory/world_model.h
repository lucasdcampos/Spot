#pragma once

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

// Shows the selected item's model at an authored mount, held in the hands. The prefab supplies the mount's bone
// attachment, any holding animation layer and the constraints that aim the body and reach the item's hand nodes, so
// neither a character skeleton nor a particular item is required by the framework.
class WorldModel : public Spot::Component {
public:
    Spot::EntityId mount = Spot::EntityId::Invalid;
    Spot::ComponentRef<Spot::Animator> animator;
    // An optional existing layer whose weight follows whether an item is shown.
    std::string poseLayer;
    std::string muzzleNode = "Muzzle";
    // Where the hands hold the item, as nodes of its model; the right one sits at the mount. Without it, the item's
    // origin does, and a hand without its node keeps its animation.
    std::string rightHandNode = "RightHand";
    std::string leftHandNode = "LeftHand";
    // Optional constraints that follow whether an item is shown: one that aims the body where the item points, and
    // ones that reach for its hand nodes.
    Spot::ComponentRef<Spot::AimIK> aim;
    Spot::ComponentRef<Spot::TwoBoneIK> rightHand;
    Spot::ComponentRef<Spot::TwoBoneIK> leftHand;
    std::vector<std::string> hiddenWhen = {"Dead", "Seated"};
    // The render layer of the model's parts while a local player plays as the pawn, as one its first-person camera
    // leaves out; -1, and for other pawns, the owning entity's layer.
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
    void Place(Spot::Entity& instance, Spot::Entity* parent) const;
    void Hold(Spot::Entity* instance);
    int GetModelLayer() const;
    Spot::EntityId m_instance = Spot::EntityId::Invalid;
    // Where the shown model's right hand node is, from its origin.
    Spot::Vector3 m_grip;
    Spot::AssetRef<Spot::Model> m_shown;
    int m_layer = -1;
};

} // namespace Gameplay
