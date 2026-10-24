#pragma once

#include "runtime_interface.h"
#include <spot/scene/ui/ui_draw_listener.h>

#include <spot.h>

#include <string>

// Works out what the HUD shows and gives it to the authored interface: a crosshair that spreads as the aim gets less
// steady and hides down the sights and while nothing can be used, a hit marker, what is in hand with its icon and
// rounds or mode, the controls that matter for it, what Use would do, the player's health and hurts, the weapon wheel
// while it is open, the time of day with the sun or the moon and, on the Statistics action, the frame rate and timings.
class Hud final : public Spot::Component, public Spot::UiDrawListener {
public:
    Spot::AssetRef<Spot::Font> font;
    // Seconds the name of what is in hand shows after switching, before it fades.
    float nameTime = 2.5f;

    bool IsShowingStatistics() const { return m_showStatistics; }
    const HudState& GetState() const { return m_state; }

protected:
    void OnStart() override;
    void OnEnable() override;
    void OnDisable() override;
    void OnDestroy() override;
    void OnLateUpdate(float deltaTime) override;
    void OnUpdate(float deltaTime) override;
    void OnDrawUi(Spot::Renderer2D& renderer, Spot::Vector2 viewSize) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // What is in hand, its rounds or mode, and its controls.
    void DescribeHand();
    void DescribeHealth();
    void UpdateStatus();

    Spot::EntityId m_interface = Spot::EntityId::Invalid;
    HudState m_state;
    std::string m_status;
    float m_frameRate = 0.0f;
    bool m_showStatistics = false;
    int m_lastSlot = -1;
    float m_nameLeft = 0.0f;
    float m_time = 0.0f;
};
