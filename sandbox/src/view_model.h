#pragma once

#include <spot.h>

#include <vector>

// Animates a first-person arms rig and attaches the held weapon's parts to its right hand.
class ViewModel final : public Spot::Component {
public:
    Spot::AssetRef<Spot::Model> arms;
    Spot::AssetRef<Spot::Model> physicsGun;
    Spot::AssetRef<Spot::Model> toolGun;
    // Where the item is held from the eyes, along the view's right, up and back axes: in front is -Z.
    Spot::Vector3 offset = {0.2f, -0.2f, -0.45f};
    // Where it moves to while aiming down the sights, in line with the eyes.
    Spot::Vector3 aimOffset = {0.0f, -0.12f, -0.35f};
    Spot::Vector3 gripOffset = {0.0f, 0.08f, -0.02f};
    float swayAmount = 0.015f;
    float maxSway = 0.035f;
    float bobAmount = 0.018f;
    float motionResponse = 14.0f;

    // The model shown now; empty when nothing is.
    const Spot::AssetRef<Spot::Model>& GetShownModel() const { return m_shown; }
    // The entities that draw its parts.
    const std::vector<Spot::EntityId>& GetPartEntities() const { return m_parts; }
    void Kick(float strength = 0.05f);
    bool Animate(std::string_view action, float duration = 0);
    Spot::EntityId GetArmsEntity() const { return m_armsEntity; }
    float GetRecoil() const { return m_recoil; }
    Spot::Vector3 GetMotionOffset() const { return m_motion; }

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    // The model of what is in hand.
    Spot::AssetRef<Spot::Model> GetHeldModel() const;
    void Show(const Spot::AssetRef<Spot::Model>& model);
    void Clear();
    void EnsureArms();
    Spot::Animator* GetArmsAnimator() const;
    void UpdateMotion(float deltaTime, const Spot::Quaternion& view);

    Spot::EntityId m_armsEntity = Spot::EntityId::Invalid;
    Spot::AssetRef<Spot::Model> m_shown;
    std::vector<Spot::EntityId> m_parts;
    // From 0 at the hip to 1 down the sights.
    float m_aim = 0.0f;
    float m_aimTarget = -1.0f;
    float m_recoil = 0.0f;
    Spot::Quaternion m_previousView;
    Spot::Vector3 m_motion;
    float m_bobPhase = 0;
    bool m_hasPreviousView = false;
    Spot::TweenId m_aimTween = Spot::TweenId::Invalid;
    Spot::TweenId m_recoilTween = Spot::TweenId::Invalid;
};
