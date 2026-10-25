#pragma once

#include "runtime_interface.h"
#include <spot/scene/ui/ui_draw_listener.h>

#include <spot.h>

#include <string>

class Car;

// Works out what the HUD shows and gives it to the authored interface: a crosshair that spreads as the aim gets less
// steady and hides down the sights and while nothing can be used, a hit marker, what is in hand with its icon and
// rounds or mode, the controls that matter for it while they do, the speed and gear of the car driven, what Use would
// do or how to place what the spawn menu gave, the player's health and hurts, the weapon wheel while it is open, the
// time of day with the sun or the moon, the controls on the Help action and, on the Statistics action, the frame rate
// and timings.
class Hud final : public Spot::Component, public Spot::UiDrawListener {
public:
    Spot::AssetRef<Spot::Font> font;
    // Seconds the name of what is in hand shows after switching, before it fades.
    float nameTime = 2.5f;
    // Seconds the controls of what is in hand, and the key that shows all of them, show as the game starts.
    float introTime = 12.0f;

    bool IsShowingStatistics() const { return m_showStatistics; }
    bool IsShowingHelp() const { return m_showHelp; }
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
    // What is in hand, its rounds or mode, and its controls; while driving, the car's speed and gear instead.
    void DescribeHand();
    void DescribeCar(const Car& car);
    void DescribeHealth();
    void UpdateStatus();

    Spot::EntityId m_interface = Spot::EntityId::Invalid;
    HudState m_state;
    std::string m_status;
    float m_frameRate = 0.0f;
    bool m_showStatistics = false;
    bool m_showHelp = false;
    // Whether the controls of what is in hand matter now, such as while reloading.
    bool m_hintsMatter = false;
    int m_lastSlot = -1;
    bool m_wasDriving = false;
    float m_nameLeft = 0.0f;
    float m_time = 0.0f;
};
