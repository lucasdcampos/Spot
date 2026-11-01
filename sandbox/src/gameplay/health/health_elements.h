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

// Opts a living actor into the kill feed. Props and vehicles do not carry this component.
class KillFeedSubject : public Spot::Component {};

// A kill feed line with outlined names, a cause icon and a background fitted to its contents.
class KillFeedRow : public Spot::UiWidget {
public:
    Spot::AssetRef<Spot::Font> font;
    float fontSize = 18.0f;
    std::string killer;
    std::string victim;
    Spot::AssetRef<Spot::Texture> icon;
    Spot::Color killerColor = Spot::Color::White;
    Spot::Color victimColor = Spot::Color::White;
    float opacity = 0.0f;

protected:
    void DrawWidget(Spot::Renderer2D& renderer, const Spot::UiLayoutEntry& entry) override;
    void Serialize(Spot::Archive& archive) override;
};

// Who killed whom and with what, newest first. Actor deaths stay for lifetime seconds and fade over the last one.
class KillFeedElement : public HudElement {
public:
    std::vector<Spot::EntityId> rows;
    float lifetime = 6.0f;
    Spot::Color color = {0.35f, 0.75f, 1.0f, 1.0f};
    Spot::Color victimColor = {1.0f, 0.4f, 0.3f, 1.0f};
    Spot::Color accent = {1.0f, 0.72f, 0.2f, 1.0f};
    struct CauseIcon {
        std::string type;
        Spot::AssetRef<Spot::Texture> icon;
        void Serialize(Spot::Archive& archive);
    };
    std::vector<CauseIcon> causeIcons;
    Spot::AssetRef<Spot::Texture> fallbackIcon;

    struct Entry {
        std::string killer;
        std::string victim;
        Spot::AssetRef<Spot::Texture> icon;
    };
    // Captures names and the source item's icon while the source still exists; empty for non-actor victims.
    static Entry Describe(Spot::Scene& scene, const DamageApplied& death);

protected:
    bool IsWanted(const HudContext& context) override;
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Line {
        Entry entry;
        float age = 0.0f;
        bool myKill = false;
        bool myDeath = false;
    };
    // Newest first.
    std::vector<Line> m_lines;
    Spot::EntityId m_pawn = Spot::EntityId::Invalid;
    Spot::EntityId m_occupant = Spot::EntityId::Invalid;
    Spot::SignalConnection m_killed;
};

} // namespace Gameplay
