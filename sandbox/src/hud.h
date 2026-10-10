#pragma once

#include <spot.h>

#include <string>

// Draws a crosshair, the time of day, the frame rate and the number of bodies, what the player has in hand with its
// ammunition, and what the keys do.
class Hud final : public Spot::Component {
public:
    Spot::AssetRef<Spot::Font> font;

protected:
    void OnUpdate(float deltaTime) override;
    void OnDrawUi(Spot::Renderer2D& renderer, Spot::Vector2 viewSize) override;
    void Serialize(Spot::Archive& archive) override;

private:
    // What the player has in hand, as the HUD says it.
    std::string DescribeHand() const;

    std::string m_status;
    int m_frames = 0;
    float m_elapsed = 0.0f;
};
