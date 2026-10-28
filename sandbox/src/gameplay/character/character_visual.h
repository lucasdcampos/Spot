#pragma once

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

struct CharacterPoseLayer {
    std::string state;
    std::string layer;
    void Serialize(Spot::Archive& archive);
};

// Presents a character without moving its physics body. The prefab supplies the model, in-place controller and
// optional pose layers; no skeleton names or asset paths belong to the presentation behavior.
class CharacterVisual : public Spot::Component {
public:
    Spot::EntityId modelRoot = Spot::EntityId::Invalid;
    Spot::ComponentRef<Spot::Animator> animator;
    std::string speedParameter = "speed";
    std::string seatedLayer;
    std::string crouchLayer;
    std::string seatedState = "Seated";
    std::vector<CharacterPoseLayer> stateLayers;
    Spot::Vector3 standingOffset;
    Spot::Vector3 seatedOffset;

    void Synchronize();

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;
};

} // namespace Gameplay
