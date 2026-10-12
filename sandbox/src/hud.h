#pragma once

#include <spot.h>

#include <string>

// Draws a crosshair, the time of day, the frame rate and the number of bodies, what the player has in hand with its
// ammunition, and what the keys do.
class Hud final : public Spot::Component {
public:
    Spot::AssetRef<Spot::Font> font;

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
    // What the player has in hand, as the HUD says it.
    std::string DescribeHand() const;
    void UpdateStatus();

    Spot::EntityId m_interface = Spot::EntityId::Invalid;
    std::string m_status;
    float m_frameRate = 0.0f;
};
