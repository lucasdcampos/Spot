#pragma once

#include <spot.h>

#include <optional>

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
    // The render layer of the model while a local player plays as this pawn, as one its first-person camera leaves
    // out; for other pawns, as other players' characters, the model has this entity's layer. -1 leaves it as it is.
    int localLayer = -1;

    void Synchronize();

protected:
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // Where the pawn moved since the last update, as how fast it walks where another machine moves it.
    std::optional<Spot::Vector3> m_lastPosition;
    Spot::Vector3 m_shownVelocity;
    std::optional<int> m_modelLayer;
};

} // namespace Gameplay
