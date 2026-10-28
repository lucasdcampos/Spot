#pragma once

#include "gameplay/health/damage.h"

#include "gameplay/hud/hud.h"

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

class Health;

// The Health of the HUD's pawn, or of the pawn riding in it, out of its most: a number, an icon and a bar easing toward
// it, all in the warning color while it is low.
class HealthElement : public HudElement {
public:
    Spot::EntityId text = Spot::EntityId::Invalid;
    Spot::EntityId icon = Spot::EntityId::Invalid;
    Spot::EntityId fill = Spot::EntityId::Invalid;
    // The bar's width when full, in canvas pixels.
    float fillWidth = 100.0f;
    // The share of its most below which health is low.
    float lowShare = 0.3f;
    Spot::Color color = {0.8070f, 0.8228f, 0.8469f, 1.0f};
    Spot::Color lowColor = {0.7835f, 0.0865f, 0.0704f, 1.0f};

    static Health* GetHealth(const HudContext& context);

protected:
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_shown = -1.0f;
};

// The UiImage of its entity flashes the edges of the screen as the pawn is hurt, and beats slowly while its health is
// low.
class DamageFlashElement : public HudElement {
public:
    float lowShare = 0.3f;

protected:
    bool IsWanted(const HudContext& context) override;
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_strength = 0.0f;
};

// The UiImage of its entity flashes as the HUD's pawn, or the pawn riding in it, damages something, fading out quickly.
class HitMarkerElement : public HudElement {
public:
    // The types of damage that show it; any when empty.
    std::vector<std::string> damageTypes = {"Bullet"};
    // How much of it fades each second of the game's time.
    float fadeSpeed = 4.0f;

    // From 1 as a hit lands, to 0.
    float GetStrength() const { return m_strength; }

protected:
    bool IsWanted(const HudContext& context) override;
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;

private:
    float m_strength = 0.0f;
    Spot::EntityId m_pawn = Spot::EntityId::Invalid;
    Spot::EntityId m_occupant = Spot::EntityId::Invalid;
    Spot::SignalConnection m_applied;
};

// Who killed whom and with what, a line each in the UiTexts of rows, newest first, for every death in the scene. Each
// line stays for lifetime seconds, fading out over its last one; a line the HUD's player took part in is in the
// accent color.
class KillFeedElement : public HudElement {
public:
    std::vector<Spot::EntityId> rows;
    float lifetime = 6.0f;
    Spot::Color color = {0.8070f, 0.8228f, 0.8469f, 1.0f};
    Spot::Color accent = {0.807f, 0.3663f, 0.0467f, 1.0f};

    // The line of a death, as "Player  [Rifle]  Enemy", or "Player  [Fall]" for one that killed itself.
    static std::string Describe(Spot::Scene& scene, const DamageApplied& death);

protected:
    bool IsWanted(const HudContext& context) override;
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Line {
        std::string text;
        float age = 0.0f;
        bool mine = false;
    };
    // Newest first.
    std::vector<Line> m_lines;
    Spot::EntityId m_pawn = Spot::EntityId::Invalid;
    Spot::EntityId m_occupant = Spot::EntityId::Invalid;
    Spot::SignalConnection m_killed;
};

} // namespace Gameplay
