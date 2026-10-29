#include "gameplay/health/health_elements.h"

#include "gameplay/core/pawn.h"
#include "gameplay/health/damage.h"
#include "gameplay/health/health.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace Gameplay {

SPOT_COMPONENT(HealthElement);
SPOT_COMPONENT(DamageFlashElement);
SPOT_COMPONENT(HitMarkerElement);
SPOT_COMPONENT(KillFeedElement);

namespace {

// Bars ease toward their value in about 120 milliseconds: three of these.
constexpr float StateTime = 0.04f;

} // namespace

Health* HealthElement::GetHealth(const HudContext& context) {
    // In a vehicle the player's own health shows, not the vehicle's.
    if (context.occupant) {
        if (auto* health = context.occupant->GetEntity().GetComponent<Health>()) {
            return health;
        }
    }
    return context.Find<Health>();
}

void HealthElement::Refresh(const HudContext& context) {
    const Health* health = GetHealth(context);
    const float value = health ? health->GetHealth() : 100.0f;
    const float most = health ? health->maxHealth : 100.0f;
    const float share = most > 0.0f ? std::clamp(value / most, 0.0f, 1.0f) : 0.0f;
    m_shown = m_shown < 0.0f || context.deltaTime <= 0.0f
                  ? share
                  : std::lerp(m_shown, share, 1.0f - std::exp(-context.deltaTime / StateTime));
    const bool low = health && !health->IsDead() && share <= lowShare;
    const Spot::Color tint = low ? lowColor : color;
    SetText(text, std::to_string(static_cast<int>(std::ceil(std::max(value, 0.0f)))));
    if (auto* widget = Get<Spot::UiText>(text)) {
        widget->color = tint;
    }
    if (auto* image = Get<Spot::UiImage>(icon)) {
        image->tint = tint;
    }
    if (auto* rect = Get<Spot::UiRect>(fill)) {
        rect->layout.sizeDelta.x = m_shown * fillWidth;
    }
    if (auto* image = Get<Spot::UiImage>(fill)) {
        image->tint = tint;
    }
}

void HealthElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("text", text);
    archive.Field("icon", icon);
    archive.Field("fill", fill);
    archive.Field("fillWidth", fillWidth, {.min = 0, .max = 2000, .tooltip = "The bar's full width"});
    archive.Field("lowShare", lowShare, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("color", color);
    archive.Field("lowColor", lowColor);
}

bool DamageFlashElement::IsWanted(const HudContext& context) {
    const Health* health = HealthElement::GetHealth(context);
    if (!health) {
        m_strength = 0.0f;
        return false;
    }
    const bool low = !health->IsDead() && health->GetHealth() <= health->maxHealth * lowShare;
    // Hurts flash the edges of the screen; low health keeps them beating, slowly.
    const float beat = low ? 0.3f + 0.12f * std::sin(context.time * 5.0f) : 0.0f;
    m_strength = std::max(health->GetHurt() * 0.7f, beat);
    return m_strength > 0.01f;
}

void DamageFlashElement::Refresh(const HudContext& /*context*/) {
    if (auto* image = GetEntity().GetComponent<Spot::UiImage>()) {
        image->tint.a = std::clamp(m_strength, 0.0f, 1.0f);
    }
}

void DamageFlashElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("lowShare", lowShare, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
}

bool HitMarkerElement::IsWanted(const HudContext& context) {
    // Here rather than as it starts, which waits until it is first shown.
    if (!m_applied.IsConnected()) {
        m_applied = DamageSystem::Get(GetScene()).OnDamageApplied.Connect([this](const DamageApplied& applied) {
            const Spot::EntityId by = applied.damage.instigator;
            if (by != Spot::EntityId::Invalid && (by == m_pawn || by == m_occupant) &&
                (damageTypes.empty() || std::ranges::find(damageTypes, applied.damage.type) != damageTypes.end())) {
                m_strength = 1.0f;
            }
        });
    }
    m_pawn = context.pawn ? context.pawn->GetEntity().GetId() : Spot::EntityId::Invalid;
    m_occupant = context.occupant ? context.occupant->GetEntity().GetId() : Spot::EntityId::Invalid;
    m_strength = std::max(m_strength - context.deltaTime * Spot::Time::GetTimeScale() * fadeSpeed, 0.0f);
    return true;
}

void HitMarkerElement::Refresh(const HudContext& /*context*/) {
    if (auto* image = GetEntity().GetComponent<Spot::UiImage>()) {
        image->tint.a = std::clamp(m_strength, 0.0f, 1.0f);
    }
}

void HitMarkerElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("damageTypes", damageTypes, {.tooltip = "The types of damage that show it; any when empty"});
    archive.Field("fadeSpeed", fadeSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.1f});
}

std::string KillFeedElement::Describe(Spot::Scene& scene, const DamageApplied& death) {
    // Whose pawn did it and what with: the weapon, or the kind of damage when it came straight from them.
    const auto nameOf = [&scene](Spot::EntityId id) -> std::string {
        const Spot::Entity* entity = scene.GetEntity(id);
        if (!entity) {
            return {};
        }
        const Pawn* pawn = Pawn::Of(*entity);
        return pawn ? pawn->GetEntity().GetName() : entity->GetName();
    };
    const std::string victim = nameOf(death.target);
    const std::string killer = nameOf(death.damage.instigator);
    const Spot::Entity* source = scene.GetEntity(death.damage.source);
    const bool weapon = source && death.damage.source != death.damage.instigator && !source->GetName().empty();
    const std::string how = weapon ? source->GetName() : death.damage.type.empty() ? "Killed" : death.damage.type;
    if (killer.empty() || killer == victim) {
        return std::format("{}  [{}]", victim, how);
    }
    return std::format("{}  [{}]  {}", killer, how, victim);
}

bool KillFeedElement::IsWanted(const HudContext& context) {
    if (!m_killed.IsConnected()) {
        m_killed = DamageSystem::Get(GetScene()).OnKilled.Connect([this](const DamageApplied& death) {
            const auto involved = [&](Spot::EntityId id) {
                return id != Spot::EntityId::Invalid && (id == m_pawn || id == m_occupant);
            };
            const Spot::Entity* by = GetScene().GetEntity(death.damage.instigator);
            const Pawn* killer = by ? Pawn::Of(*by) : nullptr;
            m_lines.insert(m_lines.begin(),
                           {Describe(GetScene(), death), 0.0f,
                            involved(death.target) || (killer && involved(killer->GetEntity().GetId()))});
            if (m_lines.size() > rows.size()) {
                m_lines.resize(rows.size());
            }
        });
    }
    m_pawn = context.pawn ? context.pawn->GetEntity().GetId() : Spot::EntityId::Invalid;
    m_occupant = context.occupant ? context.occupant->GetEntity().GetId() : Spot::EntityId::Invalid;
    for (Line& line : m_lines) {
        line.age += context.deltaTime;
    }
    std::erase_if(m_lines, [this](const Line& line) { return line.age >= lifetime; });
    return true;
}

void KillFeedElement::Refresh(const HudContext& /*context*/) {
    for (std::size_t index = 0; index < rows.size(); ++index) {
        auto* text = Get<Spot::UiText>(rows[index]);
        if (!text) {
            continue;
        }
        if (index >= m_lines.size()) {
            text->text.clear();
            continue;
        }
        const Line& line = m_lines[index];
        text->text = line.text;
        text->color = line.mine ? accent : color;
        text->color.a *= std::clamp(lifetime - line.age, 0.0f, 1.0f);
    }
}

void KillFeedElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("rows", rows, {.tooltip = "The UiTexts of its lines, newest at the first"});
    archive.Field("lifetime", lifetime, {.min = 0.5f, .max = 60.0f, .speed = 0.1f, .tooltip = "Seconds a line stays"});
    archive.Field("color", color);
    archive.Field("accent", accent, {.tooltip = "For lines the player took part in"});
}

} // namespace Gameplay
