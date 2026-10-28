#pragma once

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

class HeldItem;
class Pawn;

// Draws what its pawn holds in front of the eyes and moves it by hand, without an animated rig: it sways behind the
// view as the pawn looks, bobs with the pawn's walk (Pawn::GetMotion()), kicks when used, rolls aside to reload, comes
// up from below when switched to, aims down its sights, turns away from walls it would go through, and is carried
// low across the body while the pawn is "Sprinting", swinging with the stride. In a state of hiddenWhen, as dead or
// seated, it holds nothing; with its hands busy, it does not turn away from what they hold.
//
// It holds the model of the Inventory's item in hand (HeldItem::model) as the item says it is held
// (HeldItem::DescribePose()). Models say where their "Muzzle" is, where the "Eye" is when aiming down their sights,
// and optionally the point held at the "Hip", with nodes of those names: aiming puts the eye point at the eye, and
// shots leave from the muzzle. Parts named after what they are move on their own: a "Slide" cycles as it fires and
// locks back empty, a "Pump" is worked after each shot, a "Magazine" drops out and goes back in while reloading, a
// "Rocket" leaves when fired and is loaded again, and "Prongs" turn, faster while the item is working. Names count by
// their start, so "Slide Dots" move with the slide.
class ViewModel : public Spot::Component {
public:
    // Where the held item's origin, its grip, is from the eyes, along the view's right, up and back axes: in front is
    // -Z.
    Spot::Vector3 offset = {0.15f, -0.15f, -0.33f};
    float swayAmount = 0.012f;
    float maxSway = 0.03f;
    float bobAmount = 0.008f;
    float motionResponse = 12.0f;
    // Seconds to bring an item up after switching to it.
    float switchTime = 0.4f;
    std::vector<std::string> hiddenWhen = {"Dead", "Seated", "ThirdPerson"};
    std::string aimState = "Aiming";
    std::string runState = "Sprinting";
    std::string busyState = "HandsBusy";

    // The model shown now; empty when nothing is.
    const Spot::AssetRef<Spot::Model>& GetShownModel() const { return m_shown; }
    // The entities that draw its parts.
    const std::vector<Spot::EntityId>& GetPartEntities() const { return m_parts; }
    // Pushes the item back toward the eyes by the distance, as recoil does, and starts the parts that cycle.
    void Kick(float strength = 0.05f);
    float GetRecoil() const { return m_recoil; }
    // How the item lags and bobs now, in meters along the view's axes, without recoil or poses.
    Spot::Vector3 GetMotionOffset() const { return m_motion; }
    // From 0 at the hip to 1 down the sights.
    float GetAim() const { return m_aim; }
    // From 0 out of sight, after a switch, to 1 in hand.
    float GetRaise() const { return m_raise; }
    // From 0 held ready to 1 carried low, running.
    float GetSprint() const { return m_sprint; }
    // Where the muzzle of what is in hand is, in the world, as it is drawn; the eyes when nothing is shown.
    Spot::Vector3 GetMuzzlePosition() const;

protected:
    void OnUpdate(float deltaTime) override;
    void OnLateUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Part {
        Spot::EntityId entity = Spot::EntityId::Invalid;
        std::string name;
        Spot::Matrix4 transform;
    };

    // The item in hand, and its model.
    HeldItem* GetHeldItem() const;
    Spot::AssetRef<Spot::Model> GetHeldModel() const;
    Pawn* GetPawn() const;
    void Show(const Spot::AssetRef<Spot::Model>& model);
    void Clear();
    void UpdateMotion(float deltaTime, const Spot::Quaternion& view);
    // Where the item is now, relative to the eyes and the view.
    Spot::Matrix4 ComputePose() const;
    // How a part moves on its own now, in the model's space.
    Spot::Matrix4 ComputePartMotion(const Part& part, bool& visible) const;
    // How far the view is from walls in front, for turning the item away from them.
    void UpdateObstruction(float deltaTime);

    Spot::AssetRef<Spot::Model> m_shown;
    std::vector<Spot::EntityId> m_parts;
    std::vector<Part> m_partInfo;
    Spot::Vector3 m_muzzle;
    Spot::Vector3 m_eye;
    Spot::Vector3 m_hip;
    bool m_hasEye = false;
    // The pose of the item relative to the eyes, as last drawn.
    Spot::Matrix4 m_pose;
    float m_aim = 0.0f;
    float m_aimTarget = -1.0f;
    float m_raise = 1.0f;
    float m_recoil = 0.0f;
    float m_kickSide = 1.0f;
    // Seconds of simulation since the last shot, which drives the slide and the pump.
    float m_sinceShot = 10.0f;
    float m_time = 0.0f;
    float m_spin = 0.0f;
    float m_blocked = 0.0f;
    float m_sprint = 0.0f;
    // The lag the view's turning gives the item, smoothed: yaw and pitch rates.
    Spot::Vector2 m_lag;
    Spot::Quaternion m_previousView;
    Spot::Vector3 m_motion;
    float m_roll = 0.0f;
    bool m_hasPreviousView = false;
    bool m_wasGrounded = true;
    float m_jolt = 0.0f;
    float m_joltVelocity = 0.0f;
    Spot::TweenId m_aimTween = Spot::TweenId::Invalid;
    Spot::TweenId m_recoilTween = Spot::TweenId::Invalid;
    Spot::TweenId m_raiseTween = Spot::TweenId::Invalid;
};

} // namespace Gameplay
