#pragma once

#include "hook_lines.h"

#include "gameplay/core/input_context.h"
#include "gameplay/core/player_input.h"
#include "gameplay/core/user_settings.h"
#include "gameplay/inventory/held_item.h"
#include "gameplay/persistence/world_save.h"

#include <spot.h>

#include <vector>

// An item in hand, the first of the player's. Holding Fire grabs the prop the player aims at, anywhere in range, and
// drives the point it grabbed toward where it was grabbed in front of the camera, as on a spring; Switch Weapon (the
// wheel, or the shoulders) pulls it closer or pushes it away, and holding Use turns it with Look. Aim freezes it where
// it is, as a kinematic body, and Unfreeze unfreezes every frozen prop. Frozen props carry the tag "Frozen". While it
// holds a prop, its hands are busy, and it takes Fire, Aim, Use and Switch Weapon from everything else, and Look while
// turning it. The dead let go and grab nothing.
//
// In a session the server grabs and moves; the player's machine sends what those controls do as intents, and every
// machine is shown what it holds, with its beam.
class PhysicsGun final : public Gameplay::HeldItem,
                         public Gameplay::PlayerInputListener,
                         public Gameplay::SaveListener,
                         public Gameplay::UserSettingsListener,
                         public Spot::Replicated {
public:
    static constexpr std::string_view FrozenTag = "Frozen";

    float range = 60.0f;
    // Particles a second its beam gives off along it.
    float beamRate = 240.0f;
    float minDistance = 1.5f;
    // Meters the wheel moves the prop per notch; a shoulder held moves it a tenth of that each frame.
    float scrollStep = 1.0f;
    float maxSpeed = 40.0f;
    // An entity whose AudioSource hums while the gun holds something, higher the faster the prop moves.
    Spot::EntityId hum;

    PhysicsGun();

    bool IsGrabbing() const { return m_grabbed != Spot::EntityId::Invalid; }
    Spot::EntityId GetGrabbed() const { return m_grabbed; }

    // Freezes the grabbed prop and lets it go.
    void Freeze();
    void Release();
    void UnfreezeAll();

    Gameplay::ItemStatus DescribeStatus() const override;
    Gameplay::ItemPose DescribePose() const override;

    void Replicate(Spot::Archive& archive) override;

protected:
    void ApplySettings(const Gameplay::UserSettings& settings) override { m_lookSensitivity = settings.sensitivity; }
    void OnUse(const Gameplay::ItemInput& input) override;
    void OnPlayerInput(Gameplay::PlayerController& player) override;
    void SerializeIntent(Spot::Archive& archive) override;
    void OnUnequip() override { Release(); }
    void OnStart() override;
    void OnBeforeLoad() override { Release(); }
    void OnFixedUpdate(float fixedDeltaTime) override;
    void OnEnable() override;
    void OnDisable() override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // What the controls taken while holding do: those of a frame, and those still to apply where it is decided.
    struct GrabIntent {
        bool hold = false;
        bool freeze = false;
        float pull = 0.0f;
        bool turning = false;
        Spot::Vector2 turn;
    };

    // Adds a frame's intent to what is still to apply.
    void Gather(const GrabIntent& intent);
    // The entity of the pawn holding it, whose view it aims with.
    Spot::Entity* GetHolder() const;
    Spot::AudioSource* GetHum() const;
    void Grab();
    Spot::Rigidbody* GetGrabbedBody() const;

    Spot::EntityId m_grabbed = Spot::EntityId::Invalid;
    // Where it was grabbed, relative to the prop.
    Spot::Vector3 m_grabPoint;
    float m_distance = 0.0f;
    GrabIntent m_frame;
    GrabIntent m_pending;
    // The prop's rotation relative to where the player faces, so it turns with the player.
    Spot::Quaternion m_rotation;
    Gameplay::InputContext m_grab{{.name = "Grab", .priority = 30, .actions = {"Fire", "Aim", "Use", "Switch Weapon"}}};
    Gameplay::InputContext m_turn{{.name = "Turn Prop", .priority = 30, .actions = {"Look"}}};
    float m_particleEmission = 0;
    // The beam to the prop held, drawn after the scene's surfaces, bright enough to glow.
    std::vector<Spot::DebugLine> m_beam;
    HookLines m_beamLines;
    Spot::RenderHookId m_beamHook = Spot::RenderHookId::Invalid;
    float m_lookSensitivity = 0.0025f;
    Spot::SignalConnection m_states;
};
