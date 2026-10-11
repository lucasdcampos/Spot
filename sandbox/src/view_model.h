#pragma once

#include <spot.h>

#include <vector>

// Shows what the player holds in front of the eyes: the model of the weapon in hand (Weapon::model), or of the physics
// gun or the tool gun, drawn by entities of its own that follow the view each frame.
class ViewModel final : public Spot::Component {
public:
    Spot::AssetRef<Spot::Model> physicsGun;
    Spot::AssetRef<Spot::Model> toolGun;
    // Where the item is held from the eyes, along the view's right, up and back axes: in front is -Z.
    Spot::Vector3 offset = {0.2f, -0.2f, -0.45f};
    // Where it moves to while aiming down the sights, in line with the eyes.
    Spot::Vector3 aimOffset = {0.0f, -0.12f, -0.35f};

    // The model shown now; empty when nothing is.
    const Spot::AssetRef<Spot::Model>& GetShownModel() const { return m_shown; }
    // The entities that draw its parts.
    const std::vector<Spot::EntityId>& GetPartEntities() const { return m_parts; }
    void Kick(float strength = 0.05f);
    float GetRecoil() const { return m_recoil; }

protected:
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    // The model of what is in hand.
    Spot::AssetRef<Spot::Model> GetHeldModel() const;
    void Show(const Spot::AssetRef<Spot::Model>& model);
    void Clear();

    Spot::AssetRef<Spot::Model> m_shown;
    std::vector<Spot::EntityId> m_parts;
    // From 0 at the hip to 1 down the sights.
    float m_aim = 0.0f;
    float m_aimTarget = -1.0f;
    float m_recoil = 0.0f;
    Spot::TweenId m_aimTween = Spot::TweenId::Invalid;
    Spot::TweenId m_recoilTween = Spot::TweenId::Invalid;
};
